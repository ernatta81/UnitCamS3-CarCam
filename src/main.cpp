/**
 * @file main.cpp
 * @brief M5Stack UnitCamS3 as a parking monitor behind the windshield:
 *        deep sleep, motion / sensor wake ups, event clips on the sd card
 * @version 0.2
 * @date 2026-10-30
 *
 */
#include <Arduino.h>
#include <esp_sleep.h>
#include "app_config.h"
#include "hal/hal.h"
#include "net/net_manager.h"
#include "servers/http_server.h"
#include "storage/storage.h"
#include "parking/parking.h"


// Access point window opened by the ignition (back at the car), closes when it goes off again
static bool _is_window_by_acc = false;
static uint32_t _acc_off_since = 0;


// Power on, or ignition on: access point to aim the camera and look at the events
static void _start_window()
{
    auto hal = HAL::hal::GetHal();

    // Live view to aim the camera
    hal->camInit();
    if (hal->sdCardInit())
        Storage::begin();

    // Closes after apMinutes without a phone
    NetManager::start_ap();
    HttpServer::start();
}


void setup()
{
    // Init
    auto hal = HAL::hal::GetHal();
    hal->init();

    esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    printf("%s %s, wake cause %d\n", APP_NAME, APP_FIRMWARE_VERSION, wake);

    bool is_cold_boot = wake == ESP_SLEEP_WAKEUP_UNDEFINED;
    _is_window_by_acc = wake == ESP_SLEEP_WAKEUP_EXT1 && hal->isAccOn();

    // Timer and sensor wakes do their job without wifi and go back to sleep
    if (!is_cold_boot && !_is_window_by_acc)
        Parking::run_wake_cycle(wake);

    _start_window();
}


void loop()
{
    auto hal = HAL::hal::GetHal();
    NetManager::update();

    if (hal->isRebootRequested())
    {
        // Let the http response reach the client first
        delay(1000);
        esp_restart();
    }

    // Driver left again: watch from a fresh reference image
    if (_is_window_by_acc)
    {
        if (hal->isAccOn())
        {
            _acc_off_since = 0;
        }
        else if (_acc_off_since == 0)
        {
            _acc_off_since = millis();
        }
        else if (millis() - _acc_off_since > APP_ACC_OFF_DELAY_MS)
        {
            printf("ignition off, watch starts in %d s\n", APP_PARK_ARM_DELAY_SEC);
            Parking::sleep(APP_PARK_ARM_DELAY_SEC);
        }
    }

    // Window over: watch, or paused while the ignition is on
    if (!NetManager::is_ap_on())
    {
        printf("access point window over\n");
        Parking::sleep();
    }

    // Slow blink while the window is open
    hal->setLed((millis() / 1000) % 2);
    delay(100);
}
