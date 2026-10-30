/**
 * @file app_config.h
 * @brief Compile time settings of the parking monitor firmware
 * @version 0.2
 * @date 2026-10-30
 *
 */
#pragma once

#define APP_NAME                    "UnitCamS3 Parking Monitor"
#define APP_FIRMWARE_VERSION        "2.0.0"

#define APP_HTTP_PORT               80

// Factory access point password, the web page nags until it is changed
#define APP_DEFAULT_AP_PASSWORD     "unitcams3"

// Folder on the sd card. File names start with a letter and a sequence number,
// so the oldest file is the one with the lowest number
#define APP_DIR_PARK                "/park"

// Sd card is wired for spi, the user demo runs it at 10 MHz
#define APP_SD_FREQ_HZ              20000000
#define APP_SD_FREQ_FALLBACK_HZ     10000000

// Clip data is pushed to the card this often, a power cut loses at most this much video
#define APP_REC_SYNC_MS             3000

// Ignition (ACC) has to stay off this long to close the access point window
#define APP_ACC_OFF_DELAY_MS        5000
// After the ignition goes off the watch starts this much later, so the driver walking away is not an event
#define APP_PARK_ARM_DELAY_SEC      60

// Auto exposure starts from scratch after each init, frames before this are thrown away
#define APP_CAM_WARMUP_MS           700

// Motion detection compares the average brightness of a coarse grid between wakes
#define APP_MOTION_GRID_W           16
#define APP_MOTION_GRID_H           12

// Trigger sensor still active at sleep time: wait this long, then rely on the timer
#define APP_TRIGGER_RELEASE_MS      3000
#define APP_TRIGGER_REARM_SEC       30

// Clock counts as set (from the phone) after this epoch, 2024-01-01
#define APP_TIME_VALID_AFTER        1704067200
