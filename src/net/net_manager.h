/**
 * @file net_manager.h
 * @brief Wifi access point for the phone, switched off when nobody uses it
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>

namespace NetManager
{
    // keep_on: ignore apMinutes (apAlwaysOn while driving)
    void start_ap(bool keep_on);
    // Call from loop(), turns the wifi off after apMinutes without a connected phone
    void update();
    void stop();

    bool is_ap_on();
    String get_ap_ssid();
    int get_client_count();
}
