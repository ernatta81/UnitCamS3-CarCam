/**
 * @file storage.cpp
 * @brief
 * @version 0.2
 * @date 2026-10-30
 *
 */
#include "storage.h"
#include <SD.h>
#include "../app_config.h"
#include "../hal/hal.h"
#include "../recorder/avi_writer.h"


// Next file number, kept through deep sleep so the parking wakes do not scan the card. 0 = unknown
RTC_DATA_ATTR static uint32_t _rtc_next_number = 0;


struct DirScan_t
{
    String oldest;
    String newest;
    uint32_t oldest_number = UINT32_MAX;
    uint32_t newest_number = 0;
};


// "/rec/R000042_20261029-101500.avi" -> 42, 0 for files that are not ours
static uint32_t _get_number(const String& path)
{
    int name_start = path.lastIndexOf('/') + 1;
    if ((int)path.length() < name_start + 2 || !isdigit((unsigned char)path[name_start + 1]))
        return 0;
    return path.substring(name_start + 1).toInt();
}


static DirScan_t _scan_dir(const char* dir)
{
    DirScan_t scan;
    File root = SD.open(dir);
    if (!root || !root.isDirectory())
        return scan;

    while (true)
    {
        bool is_dir = false;
        String path = root.getNextFileName(&is_dir);
        if (path == "")
            break;

        uint32_t number = _get_number(path);
        if (is_dir || number == 0)
            continue;

        if (number < scan.oldest_number)
        {
            scan.oldest_number = number;
            scan.oldest = path;
        }
        if (number > scan.newest_number)
        {
            scan.newest_number = number;
            scan.newest = path;
        }
    }
    root.close();
    return scan;
}


void Storage::begin()
{
    if (_rtc_next_number != 0)
        return;

    // Only the last clip can be the one a power cut left open
    auto scan = _scan_dir(APP_DIR_PARK);
    if (scan.newest.endsWith(".avi"))
        AviWriter::repairIfNeeded(scan.newest);

    _rtc_next_number = scan.newest_number + 1;
    log_i("next file number: %u", _rtc_next_number);
}


String Storage::new_path(const char* dir, char prefix, const char* extension)
{
    if (_rtc_next_number == 0)
        begin();

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s/%c%06u", dir, prefix, _rtc_next_number++);

    String path = buffer;
    path += HAL::hal::GetHal()->getTimeString("_%Y%m%d-%H%M%S");
    path += extension;
    return path;
}


bool Storage::ensure_free_space()
{
    auto hal = HAL::hal::GetHal();
    if (!hal->isSdCardValid())
        return false;

    // Never ask for more than a quarter of the card
    uint64_t min_free = min((uint64_t)hal->getConfig().min_free_mb << 20, hal->sdTotalBytes() / 4);

    for (int i = 0; i < 100 && hal->sdFreeBytes() < min_free; i++)
    {
        auto scan = _scan_dir(APP_DIR_PARK);
        if (scan.oldest == "")
            break;

        log_i("card full, delete %s", scan.oldest.c_str());
        if (!SD.remove(scan.oldest))
            break;
    }

    bool ok = hal->sdFreeBytes() >= min_free;
    if (!ok)
        log_w("not enough free space on the card");
    return ok;
}


String Storage::list_json(const char* dir)
{
    String json = "[";
    json.reserve(4096);

    File root = SD.open(dir);
    if (root && root.isDirectory())
    {
        bool first = true;
        while (true)
        {
            bool is_dir = false;
            String path = root.getNextFileName(&is_dir);
            if (path == "")
                break;
            if (is_dir || _get_number(path) == 0 || path.indexOf('"') >= 0 || path.indexOf('\\') >= 0)
                continue;

            if (!first)
                json += ",";
            json += "\"";
            json += path.substring(path.lastIndexOf('/') + 1);
            json += "\"";
            first = false;
        }
        root.close();
    }

    json += "]";
    return json;
}


bool Storage::is_media_path(const String& path)
{
    if (path.indexOf("..") >= 0)
        return false;
    // No sub folders
    if (!path.startsWith(APP_DIR_PARK "/") || path.indexOf('/', strlen(APP_DIR_PARK) + 1) >= 0)
        return false;
    return _get_number(path) != 0;
}
