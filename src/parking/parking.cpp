/**
 * @file parking.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include "parking.h"
#include <SD.h>
#include <esp_camera.h>
#include <driver/rtc_io.h>
#include "motion.h"
#include "../app_config.h"
#include "../hal/hal.h"
#include "../net/net_manager.h"
#include "../recorder/recorder.h"
#include "../storage/storage.h"


struct State_t
{
    bool has_reference;
    uint8_t reference[MOTION_CELLS];
    time_t events_window_start;
    int events_in_window;
    time_t last_photo;
    bool thermal_pause;
    Parking::Stats_t stats;
};

// Kept through deep sleep, cleared on power loss
RTC_DATA_ATTR static State_t _state = {};

// Only the parking wakes count in the awake time stats, not driving or the setup window
static bool _is_wake_cycle = false;


static bool _is_rate_limited(time_t now)
{
    // A new hour, or the clock was set from the phone in the meantime
    if (now - _state.events_window_start >= 3600 || now < _state.events_window_start)
    {
        _state.events_window_start = now;
        _state.events_in_window = 0;
    }
    return _state.events_in_window >= HAL::hal::GetHal()->getConfig().max_events_per_hour;
}


static bool _storage_ready()
{
    if (!HAL::hal::GetHal()->sdCardInit())
        return false;
    Storage::begin();
    Storage::ensure_free_space();
    return true;
}


static bool _save_jpeg(const String& path, camera_fb_t* fb)
{
    File file = SD.open(path, FILE_WRITE);
    if (!file)
        return false;
    bool ok = file.write(fb->buf, fb->len) == fb->len;
    file.close();
    return ok;
}


static camera_fb_t* _grab_settled_frame()
{
    // Auto exposure starts from scratch after every wake
    uint32_t start = millis();
    while (millis() - start < APP_CAM_WARMUP_MS)
    {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb != nullptr)
            esp_camera_fb_return(fb);
    }
    return esp_camera_fb_get();
}


static void _update_reference(camera_fb_t* fb)
{
    uint8_t grid[MOTION_CELLS];
    if (fb != nullptr && Motion::get_grid(fb->buf, fb->len, grid))
    {
        memcpy(_state.reference, grid, sizeof(grid));
        _state.has_reference = true;
    }
}


static void _watch(bool triggered, bool check_motion, bool photo_due)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();

    if (!hal->camInit())
        return;
    camera_fb_t* fb = _grab_settled_frame();
    if (fb == nullptr)
        return;

    bool motion = triggered;
    if (check_motion)
    {
        uint8_t grid[MOTION_CELLS];
        if (Motion::get_grid(fb->buf, fb->len, grid))
        {
            if (_state.has_reference)
            {
                int changed = Motion::count_changed_cells(_state.reference, grid, config.motion_threshold);
                log_i("motion check: %d of %d cells changed", changed, MOTION_CELLS);
                if (changed * 100 >= MOTION_CELLS * config.motion_area_pct)
                    motion = true;
            }
            memcpy(_state.reference, grid, sizeof(grid));
            _state.has_reference = true;
        }
    }

    time_t now = time(nullptr);
    if (motion && _is_rate_limited(now))
    {
        log_i("motion, but max events per hour reached");
        _state.stats.skipped_events++;
        motion = false;
    }

    if ((motion || photo_due) && _storage_ready())
    {
        if (motion)
        {
            _state.events_in_window++;
            _state.stats.events++;
            String path = Storage::new_path(APP_DIR_PARK, 'E', ".avi");
            log_i("event (%s), recording %s", triggered ? "sensor" : "motion", path.c_str());
            Recorder::record_clip(path, fb, config.event_clip_sec);

            // The scene after the event is the new reference
            fb = check_motion ? esp_camera_fb_get() : nullptr;
            _update_reference(fb);
        }
        else
        {
            String path = Storage::new_path(APP_DIR_PARK, 'P', ".jpg");
            log_i("time lapse photo %s", path.c_str());
            if (_save_jpeg(path, fb))
                _state.stats.photos++;
        }
        _state.last_photo = now;
    }

    if (fb != nullptr)
        esp_camera_fb_return(fb);
}


// Internal pulls stay on through the sleep with the rtc peripherals powered
static void _set_sleep_pulls(int pin, bool active_high)
{
    gpio_num_t gpio = (gpio_num_t)pin;
    if (active_high)
    {
        rtc_gpio_pullup_dis(gpio);
        rtc_gpio_pulldown_en(gpio);
    }
    else
    {
        rtc_gpio_pulldown_dis(gpio);
        rtc_gpio_pullup_en(gpio);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
}


static void _arm_ignition_wake()
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    if (config.mode != "auto" || config.acc_pin < 0)
        return;

    // Ignition already on would wake us straight away
    if (hal->isAccOn())
        return;

    _set_sleep_pulls(config.acc_pin, config.acc_active_high);
    esp_sleep_enable_ext1_wakeup(1ULL << config.acc_pin, config.acc_active_high ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
}


static void _deep_sleep(uint64_t sleep_sec, bool count_awake_time)
{
    NetManager::stop();
    HAL::hal::GetHal()->prepareDeepSleep();

    if (sleep_sec > 0)
        esp_sleep_enable_timer_wakeup(sleep_sec * 1000000ULL);
    if (count_awake_time)
        _state.stats.awake_ms += millis();

    printf("deep sleep %llu s, was awake %lu ms\n", sleep_sec, millis());
    Serial.flush();
    esp_deep_sleep_start();
}


bool Parking::should_drive()
{
    auto hal = HAL::hal::GetHal();
    auto& mode = hal->getConfig().mode;
    if (mode == "parking")
        return false;
    if (mode == "auto")
        return hal->isAccOn();
    return true;
}


void Parking::run_wake_cycle(esp_sleep_wakeup_cause_t wake)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    _state.stats.wakes++;

    time_t now = time(nullptr);
    bool triggered = wake == ESP_SLEEP_WAKEUP_EXT0;
    bool check_motion = config.park_check_sec > 0;
    bool photo_due = config.timelapse_min > 0 && now - _state.last_photo >= config.timelapse_min * 60;

    // Events used up for this hour: skip the camera (saves the most energy), start over afterwards
    if (_is_rate_limited(now))
    {
        triggered = false;
        check_motion = false;
        _state.has_reference = false;
    }

    float temp = hal->getTemperature();
    if (temp > config.temp_stop_c)
        log_w("too hot: %.1f C, skip", temp);
    else if (triggered || check_motion || photo_due)
        _watch(triggered, check_motion, photo_due);

    _is_wake_cycle = true;
    sleep();
}


void Parking::sleep()
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    time_t now = time(nullptr);

    uint64_t sleep_sec = 0;
    if (config.park_check_sec > 0)
    {
        sleep_sec = config.park_check_sec;
    }
    else if (config.timelapse_min > 0)
    {
        time_t next_photo = _state.last_photo + config.timelapse_min * 60;
        sleep_sec = next_photo > now ? next_photo - now : 1;
    }

    if (config.trigger_pin >= 0)
    {
        bool armed = true;
        if (_is_rate_limited(now))
        {
            // Sensor back on at the end of the hour
            armed = false;
            uint64_t left = 3600 - (now - _state.events_window_start);
            sleep_sec = sleep_sec > 0 ? min(sleep_sec, left) : left;
        }
        else
        {
            // A sensor still active would wake us straight away
            uint32_t start = millis();
            while (hal->isTriggerActive() && millis() - start < APP_TRIGGER_RELEASE_MS)
                delay(50);
            if (hal->isTriggerActive())
            {
                armed = false;
                sleep_sec = sleep_sec > 0 ? min(sleep_sec, (uint64_t)APP_TRIGGER_REARM_SEC) : APP_TRIGGER_REARM_SEC;
            }
        }

        if (armed)
        {
            _set_sleep_pulls(config.trigger_pin, config.trigger_active_high);
            esp_sleep_enable_ext0_wakeup((gpio_num_t)config.trigger_pin, config.trigger_active_high ? 1 : 0);
        }
    }

    _arm_ignition_wake();

    if (sleep_sec == 0 && config.trigger_pin < 0 && config.mode != "auto")
        log_w("no wake up source: motion check, time lapse and trigger are all off");

    _deep_sleep(sleep_sec, _is_wake_cycle);
}


void Parking::sleep_thermal()
{
    _state.thermal_pause = true;
    _arm_ignition_wake();
    _deep_sleep(APP_THERMAL_SLEEP_SEC, false);
}


bool Parking::is_still_too_hot()
{
    if (!_state.thermal_pause)
        return false;

    auto hal = HAL::hal::GetHal();
    float temp = hal->getTemperature();
    if (temp > hal->getConfig().temp_resume_c)
    {
        log_w("still too hot: %.1f C", temp);
        return true;
    }

    _state.thermal_pause = false;
    return false;
}


Parking::Stats_t Parking::get_stats()
{
    return _state.stats;
}
