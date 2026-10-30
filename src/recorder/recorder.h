/**
 * @file recorder.h
 * @brief Event clips for the parking watch
 * @version 0.2
 * @date 2026-10-30
 *
 */
#pragma once
#include <Arduino.h>
#include <esp_camera.h>

namespace Recorder
{
    // One clip at the configured fps, starting with first_frame which is given back here
    bool record_clip(const String& path, camera_fb_t* first_frame, int seconds);
}
