/**
 * @file recorder.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include "recorder.h"
#include "avi_writer.h"
#include "../app_config.h"
#include "../hal/hal.h"
#include "../storage/storage.h"


static TaskHandle_t _task = nullptr;
static volatile bool _stop_requested = false;
static volatile bool _is_recording = false;
static volatile bool _is_overheated = false;
static volatile bool _has_sd_error = false;
static volatile float _fps = 0;

// Current path and preview frame are read from the web server task
static SemaphoreHandle_t _mutex = nullptr;
static String _current_path;
static uint8_t* _preview_buf = nullptr;
static size_t _preview_len = 0;
static size_t _preview_cap = 0;


static void _set_current_path(const String& path)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _current_path = path;
    xSemaphoreGive(_mutex);
}


static void _set_preview(camera_fb_t* fb)
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_preview_cap < fb->len)
    {
        free(_preview_buf);
        // Room for busier scenes, saves reallocating every time
        _preview_cap = fb->len + 16 * 1024;
        _preview_buf = (uint8_t*)ps_malloc(_preview_cap);
        if (_preview_buf == nullptr)
            _preview_cap = 0;
    }
    _preview_len = 0;
    if (_preview_buf != nullptr)
    {
        memcpy(_preview_buf, fb->buf, fb->len);
        _preview_len = fb->len;
    }
    xSemaphoreGive(_mutex);
}


static void _clear_preview()
{
    xSemaphoreTake(_mutex, portMAX_DELAY);
    free(_preview_buf);
    _preview_buf = nullptr;
    _preview_len = 0;
    _preview_cap = 0;
    xSemaphoreGive(_mutex);
}


static bool _open(AviWriter& avi, const String& path)
{
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr)
        return false;
    auto& res = resolution[s->status.framesize];
    return avi.open(path, res.width, res.height, HAL::hal::GetHal()->getConfig().fps);
}


// Frames at the configured rate into an open file until the time is up or stop() is called.
// The loop recording also feeds the preview and watches the temperature. False on write errors
static bool _record_frames(AviWriter& avi, uint32_t duration_ms, bool is_loop)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    uint32_t interval = 1000 / config.fps;
    uint32_t start = millis();
    uint32_t next_frame = start;
    uint32_t last_sync = start;
    uint32_t last_preview = 0;
    uint32_t last_temp_check = start;
    uint32_t fps_since = start;
    uint32_t fps_frames = 0;
    int capture_fails = 0;

    while (!_stop_requested && millis() - start < duration_ms)
    {
        int32_t wait = (int32_t)(next_frame - millis());
        if (wait > 0)
            delay(wait);
        // Running late: carry on from now instead of catching up with a burst
        next_frame = max(next_frame + interval, (uint32_t)millis());

        camera_fb_t* fb = esp_camera_fb_get();
        if (fb == nullptr)
        {
            log_e("capture failed");
            if (++capture_fails > APP_CAPTURE_FAIL_REBOOT)
            {
                hal->requestReboot();
                return false;
            }
            continue;
        }
        capture_fails = 0;

        bool ok = avi.addFrame(fb->buf, fb->len);
        if (is_loop && millis() - last_preview > APP_PREVIEW_INTERVAL_MS)
        {
            _set_preview(fb);
            last_preview = millis();
        }
        esp_camera_fb_return(fb);

        if (!ok)
        {
            log_e("write %s failed", avi.getPath().c_str());
            return false;
        }

        if (millis() - last_sync > APP_REC_SYNC_MS)
        {
            avi.sync();
            last_sync = millis();
        }

        fps_frames++;
        if (millis() - fps_since > 5000)
        {
            _fps = fps_frames * 1000.0f / (millis() - fps_since);
            fps_since = millis();
            fps_frames = 0;
        }

        if (is_loop && millis() - last_temp_check > 5000)
        {
            last_temp_check = millis();
            float temp = hal->getTemperature();
            if (temp > config.temp_stop_c)
            {
                log_w("too hot: %.1f C, stop recording", temp);
                _is_overheated = true;
                return true;
            }
        }
    }
    return true;
}


static void _task_recorder(void* param)
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();
    AviWriter avi;

    while (!_stop_requested && !_is_overheated && !hal->isRebootRequested())
    {
        // Card missing or removed: keep trying, the web page shows the error
        if (!hal->isSdCardValid())
        {
            if (!hal->sdCardInit())
            {
                _has_sd_error = true;
                delay(5000);
                continue;
            }
            Storage::begin();
        }

        Storage::ensure_free_space();
        String path = Storage::new_path(APP_DIR_DRIVE, 'R', ".avi");
        if (!_open(avi, path))
        {
            _has_sd_error = true;
            hal->sdCardDeinit();
            delay(5000);
            continue;
        }
        _has_sd_error = false;
        _set_current_path(path);
        log_i("recording %s", path.c_str());

        bool ok = _record_frames(avi, config.segment_sec * 1000, true);
        avi.close();
        _set_current_path("");

        // Card full or pulled out, mount again on the next round
        if (!ok && !hal->isRebootRequested())
        {
            _has_sd_error = true;
            hal->sdCardDeinit();
            delay(2000);
        }
    }

    _clear_preview();
    _fps = 0;
    _is_recording = false;
    _task = nullptr;
    vTaskDelete(NULL);
}


static void _mutex_init()
{
    if (_mutex == nullptr)
        _mutex = xSemaphoreCreateMutex();
}


void Recorder::start()
{
    if (_task != nullptr)
        return;

    _mutex_init();
    _stop_requested = false;
    _is_overheated = false;
    _is_recording = true;
    xTaskCreatePinnedToCore(_task_recorder, "recorder", 8192, NULL, 3, &_task, 1);
}


void Recorder::stop()
{
    _stop_requested = true;

    // A segment closes in well under a second, the index write is the slow part
    uint32_t start = millis();
    while (_task != nullptr && millis() - start < 10000)
        delay(20);
}


bool Recorder::is_recording()
{
    return _is_recording;
}


bool Recorder::is_overheated()
{
    return _is_overheated;
}


bool Recorder::has_sd_error()
{
    return _has_sd_error;
}


String Recorder::get_current_path()
{
    if (_mutex == nullptr)
        return "";

    xSemaphoreTake(_mutex, portMAX_DELAY);
    String path = _current_path;
    xSemaphoreGive(_mutex);
    return path;
}


float Recorder::get_fps()
{
    return _fps;
}


uint8_t* Recorder::copy_preview(size_t& len)
{
    len = 0;
    if (_mutex == nullptr)
        return nullptr;

    uint8_t* copy = nullptr;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (_preview_len > 0)
    {
        copy = (uint8_t*)ps_malloc(_preview_len);
        if (copy != nullptr)
        {
            memcpy(copy, _preview_buf, _preview_len);
            len = _preview_len;
        }
    }
    xSemaphoreGive(_mutex);
    return copy;
}


bool Recorder::record_clip(const String& path, camera_fb_t* first_frame, int seconds)
{
    _mutex_init();

    AviWriter avi;
    bool ok = _open(avi, path) && avi.addFrame(first_frame->buf, first_frame->len);
    esp_camera_fb_return(first_frame);

    if (ok)
    {
        _stop_requested = false;
        ok = _record_frames(avi, seconds * 1000, false);
    }
    avi.close();
    return ok;
}
