/**
 * @file parking.h
 * @brief Parking watch: deep sleep, wake on a timer or a sensor, look for motion, record a clip
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>
#include <esp_sleep.h>

namespace Parking
{
    struct Stats_t
    {
        uint32_t wakes;
        // Awake time of the wakes, not counting the ~0.2 s of boot before the app starts
        uint32_t awake_ms;
        uint32_t events;
        uint32_t photos;
        // Motion seen while maxEventsPerHour was used up
        uint32_t skipped_events;
    };

    // Dashcam mode, or ignition on in auto mode
    bool should_drive();

    // One deep sleep wake while parked, no wifi: motion check, clip or photo, back to sleep. Does not return
    void run_wake_cycle(esp_sleep_wakeup_cause_t wake);

    // Deep sleep with the parking wake ups armed (timer, trigger sensor, ignition). Does not return
    void sleep();

    // Overheated: deep sleep for a while, then try again. Does not return
    void sleep_thermal();

    // Driving start after a thermal sleep: not cooled down yet?
    bool is_still_too_hot();

    // Since power on
    Stats_t get_stats();
}
