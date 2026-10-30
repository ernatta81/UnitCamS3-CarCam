/**
 * @file parking.h
 * @brief Parking watch: deep sleep, wake on a timer or a sensor, look for motion, record a clip
 * @version 0.2
 * @date 2026-10-30
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

    // One deep sleep wake, no wifi: motion check, clip or photo, back to sleep. Does not return
    void run_wake_cycle(esp_sleep_wakeup_cause_t wake);

    // Deep sleep with the wake ups armed (timer, trigger sensor, ignition). Does not return.
    // Ignition on: paused until it goes off. arm_delay_sec > 0: first check only after that,
    // with a fresh reference image
    void sleep(uint32_t arm_delay_sec = 0);

    // Since power on
    Stats_t get_stats();
}
