/**
 * @file main.cpp
 * @brief M5Stack UnitCamS3 as a car camera behind the windshield:
 *        loop recording on the sd card while driving, low power motion watch while parked
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include <Arduino.h>
#include <esp_sleep.h>
#include "app_config.h"
#include "hal/hal.h"
#include "net/net_manager.h"
#include "servers/http_server.h"
#include "storage/storage.h"
#include "recorder/recorder.h"
#include "parking/parking.h"


enum AppState_t
{
    // Loop recording, access point for a few minutes
    app_state_driving = 0,
    // Power on while parked: access point to aim the camera and get the files, then parking
    app_state_setup,
};
static AppState_t _app_state = app_state_driving;
static uint32_t _acc_off_since = 0;


static void _start_driving()
{
    auto hal = HAL::hal::GetHal();
    if (Parking::is_still_too_hot())
        Parking::sleep_thermal();

    _app_state = app_state_driving;
    if (!hal->camInit())
    {
        // Retry
        log_i("reboot..");
        delay(1000);
        esp_restart();
    }
    // The recorder keeps retrying the card if it is missing
    if (hal->sdCardInit())
        Storage::begin();

    NetManager::start_ap(hal->getConfig().ap_always_on);
    HttpServer::start();
    Recorder::start();
}


static void _start_setup_window()
{
    auto hal = HAL::hal::GetHal();

    _app_state = app_state_setup;
    // Live view to aim the camera, files from the last parking
    hal->camInit();
    if (hal->sdCardInit())
        Storage::begin();

    // Closes after apMinutes without a phone, then the parking starts
    NetManager::start_ap(false);
    HttpServer::start();
}


static void _update_led()
{
    auto hal = HAL::hal::GetHal();
    uint32_t now = millis();

    if (_app_state == app_state_setup)
    {
        // Slow blink: waiting for the phone before parking
        hal->setLed((now / 1000) % 2);
    }
    else if (!hal->getConfig().led)
    {
        hal->setLed(false);
    }
    else if (Recorder::has_sd_error())
    {
        // Fast blink: not recording
        hal->setLed((now / 250) % 2);
    }
    else
    {
        hal->setLed(Recorder::is_recording());
    }
}


static void _update_driving()
{
    auto hal = HAL::hal::GetHal();

    // Ignition off for a while: finish the clip and park
    if (hal->getConfig().mode == "auto")
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
            printf("ignition off, parking\n");
            Recorder::stop();
            Parking::sleep();
        }
    }

    if (Recorder::is_overheated())
    {
        printf("overheated, sleep for %d s\n", APP_THERMAL_SLEEP_SEC);
        Recorder::stop();
        Parking::sleep_thermal();
    }
}


static void _update_setup()
{
    auto hal = HAL::hal::GetHal();

    // Ignition on: start over in driving mode
    if (hal->getConfig().mode == "auto" && hal->isAccOn())
        hal->requestReboot();

    if (!NetManager::is_ap_on())
    {
        printf("setup window over, parking\n");
        Parking::sleep();
    }
}


void setup()
{
    // Init
    HAL::hal::GetHal()->init();

    esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    printf("%s %s, wake cause %d\n", APP_NAME, APP_FIRMWARE_VERSION, wake);

    if (Parking::should_drive())
    {
        _start_driving();
        return;
    }

    // Parked: timer and sensor wakes do their job without wifi and go back to sleep
    if (wake != ESP_SLEEP_WAKEUP_UNDEFINED)
        Parking::run_wake_cycle(wake);

    // Power on while parked
    _start_setup_window();
}


void loop()
{
    NetManager::update();

    if (HAL::hal::GetHal()->isRebootRequested())
    {
        // Close the clip, let the http response reach the client
        Recorder::stop();
        delay(1000);
        esp_restart();
    }

    if (_app_state == app_state_driving)
        _update_driving();
    else
        _update_setup();

    _update_led();
    delay(100);
}
