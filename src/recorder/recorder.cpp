/**
 * @file recorder.cpp
 * @brief
 * @version 0.2
 * @date 2026-10-30
 *
 */
#include "recorder.h"
#include "avi_writer.h"
#include "../app_config.h"
#include "../hal/hal.h"


static bool _open(AviWriter& avi, const String& path)
{
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr)
        return false;
    auto& res = resolution[s->status.framesize];
    return avi.open(path, res.width, res.height, HAL::hal::GetHal()->getConfig().fps);
}


// Frames at the configured rate into an open file until the time is up. False on write errors
static bool _record_frames(AviWriter& avi, uint32_t duration_ms)
{
    uint32_t interval = 1000 / HAL::hal::GetHal()->getConfig().fps;
    uint32_t start = millis();
    uint32_t next_frame = start;
    uint32_t last_sync = start;

    while (millis() - start < duration_ms)
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
            continue;
        }
        bool ok = avi.addFrame(fb->buf, fb->len);
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
    }
    return true;
}


bool Recorder::record_clip(const String& path, camera_fb_t* first_frame, int seconds)
{
    AviWriter avi;
    bool ok = _open(avi, path) && avi.addFrame(first_frame->buf, first_frame->len);
    esp_camera_fb_return(first_frame);

    if (ok)
        ok = _record_frames(avi, seconds * 1000);
    avi.close();
    return ok;
}
