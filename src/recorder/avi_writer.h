/**
 * @file avi_writer.h
 * @brief Motion jpeg avi files on the sd card (plays in VLC, Windows, ffmpeg)
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>


class AviWriter
{
private:
    File _file;
    String _path;
    std::vector<uint32_t> _frame_sizes;
    int _fps = 10;
    uint32_t _first_frame_ms = 0;
    uint32_t _last_frame_ms = 0;

public:
    bool open(const String& path, int width, int height, int fps);
    bool addFrame(const uint8_t* jpeg, size_t len);
    // Pushes data and file size to the card, a power cut then only loses what came after
    void sync();
    // Writes the index and the measured frame rate, the file is a normal avi after this
    bool close();

    inline bool isOpen() { return (bool)_file; }
    inline uint32_t getFrameCount() { return _frame_sizes.size(); }
    inline const String& getPath() { return _path; }

    // A file left open by a power cut has no index and zero frames in the header:
    // rebuild them from the frame chunks that made it to the card
    static bool repairIfNeeded(const String& path);
};
