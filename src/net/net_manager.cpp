/**
 * @file net_manager.cpp
 * @brief
 * @version 0.2
 * @date 2026-10-30
 *
 */
#include "net_manager.h"
#include <WiFi.h>
#include "../hal/hal.h"


static bool _is_ap_on = false;
static uint32_t _last_client_ms = 0;
static String _ssid;


void NetManager::start_ap()
{
    auto hal = HAL::hal::GetHal();
    auto& config = hal->getConfig();

    _ssid = config.ap_ssid != "" ? config.ap_ssid : "ParkCam-" + hal->getDeviceId().substring(6);
    // WPA2 needs at least 8 characters, open network otherwise
    const char* password = config.ap_password.length() >= 8 ? config.ap_password.c_str() : nullptr;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(_ssid.c_str(), password);
    // The phone is a meter away, full power only heats the module up
    WiFi.setTxPower(WIFI_POWER_11dBm);

    _is_ap_on = true;
    _last_client_ms = millis();
    printf("ap: %s (%s), open http://%s/\n", _ssid.c_str(), password ? "wpa2" : "open", WiFi.softAPIP().toString().c_str());
}


void NetManager::update()
{
    if (!_is_ap_on)
        return;

    if (WiFi.softAPgetStationNum() > 0)
    {
        _last_client_ms = millis();
        return;
    }

    uint32_t timeout_ms = HAL::hal::GetHal()->getConfig().ap_minutes * 60000UL;
    if (millis() - _last_client_ms > timeout_ms)
    {
        log_i("no phone for %lu min, wifi off", timeout_ms / 60000);
        stop();
    }
}


void NetManager::stop()
{
    if (_is_ap_on)
        WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _is_ap_on = false;
}


bool NetManager::is_ap_on()
{
    return _is_ap_on;
}


String NetManager::get_ap_ssid()
{
    return _ssid;
}


int NetManager::get_client_count()
{
    return _is_ap_on ? WiFi.softAPgetStationNum() : 0;
}
