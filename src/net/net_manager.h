/**
 * @file net_manager.h
 * @brief Wifi access point for the phone, switched off when nobody uses it
 * @version 0.2
 * @date 2026-10-30
 *
 */
#pragma once
#include <Arduino.h>

namespace NetManager
{
    void start_ap();
    // Call from loop(), turns the wifi off after apMinutes without a connected phone
    void update();
    void stop();

    bool is_ap_on();
    String get_ap_ssid();
    int get_client_count();
}
