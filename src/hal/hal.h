/**
 * @file hal.h
 * @brief UnitCamS3 board support for the car camera: camera, sd card, status led,
 *        grove port inputs, die temperature, clock, persistent config.
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>
#include <time.h>
#include "../app_config.h"


#define HAL_PIN_LED         14

#define HAL_PIN_SD_CS       9
#define HAL_PIN_SD_MOSI     38
#define HAL_PIN_SD_CLK      39
#define HAL_PIN_SD_MISO     40

// The grove port (HY2.0-4P) carries the usb D- / D+, they can be used as inputs instead
#define HAL_PIN_GROVE_A     19
#define HAL_PIN_GROVE_B     20


namespace HAL
{
    class hal
    {
    public:
        struct Config_t
        {
            // "dashcam": record while powered, "parking": low power watch, "auto": ignition input picks
            String mode = "dashcam";

            // Camera and recording
            String frame_size = "VGA";
            int jpeg_quality = 12;
            int fps = 10;
            int segment_sec = 60;
            int min_free_mb = 1024;
            bool vflip = true;
            bool hmirror = true;
            bool led = true;

            // Grove port inputs (19 or 20), -1 = not used
            int acc_pin = -1;
            bool acc_active_high = false;
            int trigger_pin = -1;
            bool trigger_active_high = true;

            // Parking
            int park_check_sec = 10;
            int motion_threshold = 25;
            int motion_area_pct = 3;
            int event_clip_sec = 20;
            int max_events_per_hour = 10;
            int timelapse_min = 0;

            // Access point for the phone, empty ssid = "CarCam-xxxxxx", password shorter than 8 = open
            String ap_ssid;
            String ap_password = APP_DEFAULT_AP_PASSWORD;
            int ap_minutes = 5;
            bool ap_always_on = false;

            // Die temperature limits
            int temp_stop_c = 80;
            int temp_resume_c = 70;
        };

    private:
        Config_t _config;
        bool _is_cam_running = false;
        bool _is_sd_card_valid = false;
        volatile bool _reboot_requested = false;

        void _config_init();
        void _pins_init();

    public:
        // No camera, sd card or wifi here: deep sleep wakes only start what they need
        void init();

        void setLed(bool state);

        // Config is stored as json in LittleFS (/config.json)
        inline Config_t& getConfig() { return _config; }
        bool saveConfig();
        void printConfig();

        // Values accepted in the config
        static bool isValidFrameSize(const String& name);
        static bool isValidMode(const String& name);
        static bool isValidInputPin(int pin);

        // Camera, frame size and quality from the config
        bool camInit();
        // Sensor to standby and clock pin held low, PWDN is tied to GND on this board
        void camDeinit();
        inline bool isCamRunning() { return _is_cam_running; }

        // Sd card over spi, mounted at /sd
        bool sdCardInit();
        void sdCardDeinit();
        inline bool isSdCardValid() { return _is_sd_card_valid; }
        uint64_t sdTotalBytes();
        uint64_t sdFreeBytes();

        // Grove port inputs, false when not configured
        bool isAccOn();
        bool isTriggerActive();

        // Die temperature, well above the cabin air while recording with wifi on
        float getTemperature();

        // Clock from the phone, kept through deep sleep but lost on power loss
        void setTime(time_t epoch, int tzOffsetMin);
        bool isTimeValid();
        // strftime format, empty string while the clock is not set
        String getTimeString(const char* format);

        // Lower case hex efuse mac, e.g. "3485189a1b2c"
        String getDeviceId();

        // Camera and sd card off, led and clock pin held through the sleep
        void prepareDeepSleep();

        // Reboot is done from loop() so pending http responses can be flushed first
        inline void requestReboot() { _reboot_requested = true; }
        inline bool isRebootRequested() { return _reboot_requested; }


    private:
        static hal* _hal;
    public:
        static hal* GetHal();
    };
}
