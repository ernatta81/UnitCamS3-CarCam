/**
 * @file hal.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include <Arduino.h>
#include <sys/time.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "hal.h"
#include "../app_config.h"


using namespace HAL;


// Time zone from the phone, the clock itself keeps running on the rtc timer through deep sleep
RTC_DATA_ATTR static int _rtc_tz_offset_min = 0;
RTC_DATA_ATTR static bool _rtc_tz_valid = false;

static SemaphoreHandle_t _temp_mutex = nullptr;
static float _temp_c = 0;
static uint32_t _temp_read_ms = 0;


static void _apply_time_zone(int offset_min)
{
    // Same sign as javascript getTimezoneOffset(): minutes to add to local time to get utc
    char tz[16];
    int abs_min = abs(offset_min);
    snprintf(tz, sizeof(tz), "LOC%c%02d:%02d", offset_min < 0 ? '-' : '+', abs_min / 60, abs_min % 60);
    setenv("TZ", tz, 1);
    tzset();
}


static void _input_init(int pin, bool active_high)
{
    if (pin < 0)
        return;

    // The last deep sleep left it as an rtc pin for the wake up
    rtc_gpio_deinit((gpio_num_t)pin);
    // Also turns the usb pad off on this pin
    pinMode(pin, active_high ? INPUT_PULLDOWN : INPUT_PULLUP);
}


static bool _is_input_active(int pin, bool active_high)
{
    if (pin < 0)
        return false;
    return digitalRead(pin) == (active_high ? HIGH : LOW);
}


void hal::init()
{
    Serial.begin(115200);
    log_i("hal init");

    _temp_mutex = xSemaphoreCreateMutex();

    // Led, still held from the deep sleep
    gpio_hold_dis(GPIO_NUM_14);
    gpio_reset_pin(GPIO_NUM_14);
    pinMode(HAL_PIN_LED, OUTPUT);
    setLed(false);

    // Config first, pins and camera are set up from it
    _config_init();
    _pins_init();

    if (_rtc_tz_valid)
        _apply_time_zone(_rtc_tz_offset_min);
}


void hal::_pins_init()
{
    _input_init(_config.acc_pin, _config.acc_active_high);
    _input_init(_config.trigger_pin, _config.trigger_active_high);
}


void hal::setLed(bool state)
{
    // Active low
    digitalWrite(HAL_PIN_LED, state ? 0 : 1);
}


bool hal::isAccOn()
{
    return _is_input_active(_config.acc_pin, _config.acc_active_high);
}


bool hal::isTriggerActive()
{
    return _is_input_active(_config.trigger_pin, _config.trigger_active_high);
}


float hal::getTemperature()
{
    // Read from the recorder and the web server tasks, the sensor driver is not reentrant
    xSemaphoreTake(_temp_mutex, portMAX_DELAY);
    if (_temp_read_ms == 0 || millis() - _temp_read_ms > 1000)
    {
        _temp_c = temperatureRead();
        _temp_read_ms = millis();
    }
    float temp = _temp_c;
    xSemaphoreGive(_temp_mutex);
    return temp;
}


void hal::setTime(time_t epoch, int tzOffsetMin)
{
    struct timeval tv = {};
    tv.tv_sec = epoch;
    settimeofday(&tv, nullptr);

    _rtc_tz_offset_min = tzOffsetMin;
    _rtc_tz_valid = true;
    _apply_time_zone(tzOffsetMin);
}


bool hal::isTimeValid()
{
    return time(nullptr) > APP_TIME_VALID_AFTER;
}


String hal::getTimeString(const char* format)
{
    if (!isTimeValid())
        return "";

    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char buffer[40];
    strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}


String hal::getDeviceId()
{
    // Stored lsb first
    uint64_t mac = ESP.getEfuseMac();
    char buffer[13];
    snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x%02x%02x",
             (uint8_t)(mac), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
             (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    return buffer;
}


void hal::prepareDeepSleep()
{
    camDeinit();
    sdCardDeinit();

    // Led off through the sleep, a floating pin can make it glow. Rtc pin, the hold lasts through deep sleep
    setLed(false);
    gpio_hold_en(GPIO_NUM_14);
}


hal* hal::_hal = nullptr;

hal* hal::GetHal()
{
    if (_hal == nullptr)
    {
        _hal = new hal;
    }
    return _hal;
}
