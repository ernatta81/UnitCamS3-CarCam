/**
 * @file hal_config.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include <Arduino.h>
#include "hal.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FS.h>


using namespace HAL;


static const char* _config_file_path = "/config.json";


// Keys missing from the file keep their default value
template <typename T>
static void _copy_if_set(JsonDocument& doc, const char* key, T& value)
{
    if (!doc[key].isNull())
        value = doc[key].as<T>();
}


bool hal::isValidMode(const String& name)
{
    return name == "dashcam" || name == "parking" || name == "auto";
}


bool hal::isValidInputPin(int pin)
{
    return pin == -1 || pin == HAL_PIN_GROVE_A || pin == HAL_PIN_GROVE_B;
}


bool hal::saveConfig()
{
    // Try open
    File file = LittleFS.open(_config_file_path, "w");
    if (!file)
    {
        log_e("open %s failed", _config_file_path);
        return false;
    }

    DynamicJsonDocument doc(2048);

    doc["mode"] = _config.mode;
    doc["frameSize"] = _config.frame_size;
    doc["jpegQuality"] = _config.jpeg_quality;
    doc["fps"] = _config.fps;
    doc["segmentSec"] = _config.segment_sec;
    doc["minFreeMb"] = _config.min_free_mb;
    doc["vflip"] = _config.vflip;
    doc["hmirror"] = _config.hmirror;
    doc["led"] = _config.led;
    doc["accPin"] = _config.acc_pin;
    doc["accActiveHigh"] = _config.acc_active_high;
    doc["triggerPin"] = _config.trigger_pin;
    doc["triggerActiveHigh"] = _config.trigger_active_high;
    doc["parkCheckSec"] = _config.park_check_sec;
    doc["motionThreshold"] = _config.motion_threshold;
    doc["motionAreaPct"] = _config.motion_area_pct;
    doc["eventClipSec"] = _config.event_clip_sec;
    doc["maxEventsPerHour"] = _config.max_events_per_hour;
    doc["timelapseMin"] = _config.timelapse_min;
    doc["apSsid"] = _config.ap_ssid;
    doc["apPass"] = _config.ap_password;
    doc["apMinutes"] = _config.ap_minutes;
    doc["apAlwaysOn"] = _config.ap_always_on;
    doc["tempStopC"] = _config.temp_stop_c;
    doc["tempResumeC"] = _config.temp_resume_c;

    serializeJson(doc, file);

    file.close();
    return true;
}


void hal::_config_init()
{
    // File system
    if (!LittleFS.begin(true))
    {
        log_e("file system init failed, use default config");
        return;
    }

    // Check exist
    File file = LittleFS.open(_config_file_path, "r");
    if (!file)
    {
        log_i("%s not exist, create default", _config_file_path);
        saveConfig();
        printConfig();
        return;
    }

    // Parse json
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        log_e("parse %s failed: %s, use default config", _config_file_path, error.c_str());
        return;
    }

    _copy_if_set(doc, "mode", _config.mode);
    _copy_if_set(doc, "frameSize", _config.frame_size);
    _copy_if_set(doc, "jpegQuality", _config.jpeg_quality);
    _copy_if_set(doc, "fps", _config.fps);
    _copy_if_set(doc, "segmentSec", _config.segment_sec);
    _copy_if_set(doc, "minFreeMb", _config.min_free_mb);
    _copy_if_set(doc, "vflip", _config.vflip);
    _copy_if_set(doc, "hmirror", _config.hmirror);
    _copy_if_set(doc, "led", _config.led);
    _copy_if_set(doc, "accPin", _config.acc_pin);
    _copy_if_set(doc, "accActiveHigh", _config.acc_active_high);
    _copy_if_set(doc, "triggerPin", _config.trigger_pin);
    _copy_if_set(doc, "triggerActiveHigh", _config.trigger_active_high);
    _copy_if_set(doc, "parkCheckSec", _config.park_check_sec);
    _copy_if_set(doc, "motionThreshold", _config.motion_threshold);
    _copy_if_set(doc, "motionAreaPct", _config.motion_area_pct);
    _copy_if_set(doc, "eventClipSec", _config.event_clip_sec);
    _copy_if_set(doc, "maxEventsPerHour", _config.max_events_per_hour);
    _copy_if_set(doc, "timelapseMin", _config.timelapse_min);
    _copy_if_set(doc, "apSsid", _config.ap_ssid);
    _copy_if_set(doc, "apPass", _config.ap_password);
    _copy_if_set(doc, "apMinutes", _config.ap_minutes);
    _copy_if_set(doc, "apAlwaysOn", _config.ap_always_on);
    _copy_if_set(doc, "tempStopC", _config.temp_stop_c);
    _copy_if_set(doc, "tempResumeC", _config.temp_resume_c);

    // Sanitize hand written files
    if (!isValidFrameSize(_config.frame_size))
        _config.frame_size = "VGA";
    if (!isValidMode(_config.mode))
        _config.mode = "dashcam";
    _config.jpeg_quality = constrain(_config.jpeg_quality, 6, 63);
    _config.fps = constrain(_config.fps, 1, 25);
    _config.segment_sec = constrain(_config.segment_sec, 10, 600);
    _config.min_free_mb = constrain(_config.min_free_mb, 100, 65536);
    if (!isValidInputPin(_config.acc_pin))
        _config.acc_pin = -1;
    if (!isValidInputPin(_config.trigger_pin) || _config.trigger_pin == _config.acc_pin)
        _config.trigger_pin = -1;
    _config.park_check_sec = constrain(_config.park_check_sec, 0, 3600);
    _config.motion_threshold = constrain(_config.motion_threshold, 5, 100);
    _config.motion_area_pct = constrain(_config.motion_area_pct, 1, 100);
    _config.event_clip_sec = constrain(_config.event_clip_sec, 5, 300);
    _config.max_events_per_hour = constrain(_config.max_events_per_hour, 1, 60);
    _config.timelapse_min = constrain(_config.timelapse_min, 0, 1440);
    _config.ap_minutes = constrain(_config.ap_minutes, 1, 120);
    _config.temp_stop_c = constrain(_config.temp_stop_c, 50, 100);
    _config.temp_resume_c = constrain(_config.temp_resume_c, 30, _config.temp_stop_c - 5);

    // Auto needs the ignition input
    if (_config.mode == "auto" && _config.acc_pin < 0)
    {
        log_w("mode auto without accPin, use dashcam");
        _config.mode = "dashcam";
    }

    printConfig();
}


void hal::printConfig()
{
    printf("config:\n");
    printf("mode: %s\n", _config.mode.c_str());
    printf("frame size: %s, jpeg quality: %d, fps: %d\n", _config.frame_size.c_str(), _config.jpeg_quality, _config.fps);
    printf("segment: %d s, min free: %d MB\n", _config.segment_sec, _config.min_free_mb);
    printf("acc pin: %d, trigger pin: %d\n", _config.acc_pin, _config.trigger_pin);
    printf("park check: %d s, event clip: %d s, timelapse: %d min\n", _config.park_check_sec, _config.event_clip_sec, _config.timelapse_min);
    printf("ap: %s, %d min\n", _config.ap_ssid.c_str(), _config.ap_minutes);
}
