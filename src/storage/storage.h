/**
 * @file storage.h
 * @brief Files on the sd card: names, loop deletion of the oldest ones, listing
 * @version 0.2
 * @date 2026-10-30
 *
 */
#pragma once
#include <Arduino.h>

namespace Storage
{
    // Call once the sd card is mounted. After a power loss this scans the card for
    // the next file number and finishes the clips that were cut, deep sleep wakes skip it
    void begin();

    // e.g. "/park/E000042_20261029-101500.avi", the time part only once the clock is set
    String new_path(const char* dir, char prefix, const char* extension);

    // Deletes the oldest files until minFreeMb is free
    bool ensure_free_space();

    // Json array of the file names in a folder. Names only: sizes cost a directory search each on fat
    String list_json(const char* dir);

    // Only our files inside our folder, for the web page
    bool is_media_path(const String& path);
}
