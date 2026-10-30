/**
 * @file recorder.h
 * @brief Loop recording while driving, single clips for the parking events
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>
#include <esp_camera.h>

namespace Recorder
{
    // Segments of segmentSec on a task, until stop(). Camera must be running
    void start();
    // Closes the current file, returns once it is on the card
    void stop();

    bool is_recording();
    // Die temperature went over the limit, the task stopped by itself
    bool is_overheated();
    // Card missing, full or failing, the task keeps retrying
    bool has_sd_error();
    // File being written, empty between segments
    String get_current_path();
    float get_fps();

    // Copy of the latest recorded frame for the web preview, free() it. nullptr when not recording
    uint8_t* copy_preview(size_t& len);

    // One clip in the calling task (parking events), starting with first_frame which is given back here
    bool record_clip(const String& path, camera_fb_t* first_frame, int seconds);
}
