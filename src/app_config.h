/**
 * @file app_config.h
 * @brief Compile time settings of the car camera firmware
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once

#define APP_NAME                    "UnitCamS3 CarCam"
#define APP_FIRMWARE_VERSION        "1.0.0"

#define APP_HTTP_PORT               80

// Factory access point password, the web page nags until it is changed
#define APP_DEFAULT_AP_PASSWORD     "unitcams3"

// Folders on the sd card. File names start with a letter and a sequence number,
// so the oldest file is the one with the lowest number
#define APP_DIR_DRIVE               "/rec"
#define APP_DIR_PARK                "/park"

// Sd card is wired for spi, the user demo runs it at 10 MHz
#define APP_SD_FREQ_HZ              20000000
#define APP_SD_FREQ_FALLBACK_HZ     10000000

// Recording data is pushed to the card this often, a power cut loses at most this much video
#define APP_REC_SYNC_MS             3000
// Latest frame kept for the web preview
#define APP_PREVIEW_INTERVAL_MS     500
// Consecutive capture failures before rebooting
#define APP_CAPTURE_FAIL_REBOOT     50

// Ignition (ACC) has to stay off this long before parking starts
#define APP_ACC_OFF_DELAY_MS        5000

// Auto exposure starts from scratch after each init, frames before this are thrown away
#define APP_CAM_WARMUP_MS           700

// Motion detection compares the average brightness of a coarse grid between wakes
#define APP_MOTION_GRID_W           16
#define APP_MOTION_GRID_H           12

// Trigger sensor still active at sleep time: wait this long, then rely on the timer
#define APP_TRIGGER_RELEASE_MS      3000
#define APP_TRIGGER_REARM_SEC       30

// Overheated: deep sleep this long before checking again
#define APP_THERMAL_SLEEP_SEC       600

// Clock counts as set (from the phone) after this epoch, 2024-01-01
#define APP_TIME_VALID_AFTER        1704067200
