/**
 * @file hal_sd.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <soc/gpio_periph.h>
#include "hal.h"
#include "../app_config.h"


using namespace HAL;


static SPIClass _sd_spi(HSPI);


bool hal::sdCardInit()
{
    if (_is_sd_card_valid)
        return true;

    log_i("sd card init");
    _sd_spi.begin(HAL_PIN_SD_CLK, HAL_PIN_SD_MISO, HAL_PIN_SD_MOSI, HAL_PIN_SD_CS);

    // Stronger drive and pull ups, from the user demo ("better card compatibility")
    uint8_t pin_map[] = {HAL_PIN_SD_CLK, HAL_PIN_SD_MISO, HAL_PIN_SD_MOSI};
    for (auto gpio : pin_map)
    {
        *(volatile uint32_t*)(GPIO_PIN_MUX_REG[gpio]) |= FUN_DRV_M;
        gpio_pulldown_dis((gpio_num_t)gpio);
        gpio_pullup_en((gpio_num_t)gpio);
    }

    // Recording needs the bandwidth, slower clock if the card does not like it
    bool ok = SD.begin(HAL_PIN_SD_CS, _sd_spi, APP_SD_FREQ_HZ);
    if (!ok)
    {
        log_w("sd card init failed at %d Hz, retry slower", APP_SD_FREQ_HZ);
        ok = SD.begin(HAL_PIN_SD_CS, _sd_spi, APP_SD_FREQ_FALLBACK_HZ);
    }
    if (!ok)
    {
        log_e("sd card init failed");
        _sd_spi.end();
        return false;
    }

    for (auto dir : {APP_DIR_DRIVE, APP_DIR_PARK})
    {
        if (!SD.exists(dir) && !SD.mkdir(dir))
            log_e("create %s failed", dir);
    }

    _is_sd_card_valid = true;
    log_i("sd card ok, %llu MB, %llu MB free", sdTotalBytes() >> 20, sdFreeBytes() >> 20);
    return true;
}


void hal::sdCardDeinit()
{
    if (!_is_sd_card_valid)
        return;

    _is_sd_card_valid = false;
    SD.end();
    _sd_spi.end();
}


uint64_t hal::sdTotalBytes()
{
    return _is_sd_card_valid ? SD.totalBytes() : 0;
}


uint64_t hal::sdFreeBytes()
{
    return _is_sd_card_valid ? SD.totalBytes() - SD.usedBytes() : 0;
}
