/**
 * @file avi_writer.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include "avi_writer.h"
#include <SD.h>
#include <unistd.h>


// Fixed size header: RIFF / hdrl (avih, strl (strh, strf)) / movi, frames start right after it.
// Offsets of the fields that are only known when the file is closed
#define AVI_OFS_RIFF_SIZE       4
#define AVI_OFS_US_PER_FRAME    32
#define AVI_OFS_MAX_BYTES_SEC   36
#define AVI_OFS_TOTAL_FRAMES    48
#define AVI_OFS_SUGGESTED_BUF   60
#define AVI_OFS_STRH_SCALE      128
#define AVI_OFS_STRH_LENGTH     140
#define AVI_OFS_STRH_SUGG_BUF   144
#define AVI_OFS_MOVI_SIZE       216
#define AVI_OFS_MOVI_FOURCC     220
#define AVI_HEADER_SIZE         224

#define AVIF_HASINDEX           0x10
#define AVIIF_KEYFRAME          0x10

// Anything bigger is not one of our frames
#define AVI_MAX_FRAME_SIZE      (2 * 1024 * 1024)

// Mount point of the SD library, for the posix calls
#define AVI_SD_MOUNT_POINT      "/sd"


static void _put_u16(uint8_t* p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}


static void _put_u32(uint8_t* p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}


static uint32_t _get_u32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}


static void _put_fourcc(uint8_t* p, const char* fourcc)
{
    memcpy(p, fourcc, 4);
}


static void _build_header(uint8_t* h, int width, int height, uint32_t us_per_frame)
{
    memset(h, 0, AVI_HEADER_SIZE);

    _put_fourcc(h + 0, "RIFF");
    _put_fourcc(h + 8, "AVI ");

    _put_fourcc(h + 12, "LIST");
    _put_u32(h + 16, 192);
    _put_fourcc(h + 20, "hdrl");

    // Main header
    _put_fourcc(h + 24, "avih");
    _put_u32(h + 28, 56);
    _put_u32(h + 32, us_per_frame);
    _put_u32(h + 44, AVIF_HASINDEX);
    _put_u32(h + 56, 1);
    _put_u32(h + 64, width);
    _put_u32(h + 68, height);

    _put_fourcc(h + 88, "LIST");
    _put_u32(h + 92, 116);
    _put_fourcc(h + 96, "strl");

    // Stream header, rate / scale = fps
    _put_fourcc(h + 100, "strh");
    _put_u32(h + 104, 56);
    _put_fourcc(h + 108, "vids");
    _put_fourcc(h + 112, "MJPG");
    _put_u32(h + 128, us_per_frame);
    _put_u32(h + 132, 1000000);
    _put_u32(h + 148, 0xFFFFFFFF);
    _put_u16(h + 160, width);
    _put_u16(h + 162, height);

    // Stream format
    _put_fourcc(h + 164, "strf");
    _put_u32(h + 168, 40);
    _put_u32(h + 172, 40);
    _put_u32(h + 176, width);
    _put_u32(h + 180, height);
    _put_u16(h + 184, 1);
    _put_u16(h + 186, 24);
    _put_fourcc(h + 188, "MJPG");
    _put_u32(h + 192, width * height * 3);

    _put_fourcc(h + 212, "LIST");
    _put_u32(h + 216, 4);
    _put_fourcc(h + 220, "movi");
}


static bool _patch_u32(File& file, uint32_t offset, uint32_t value)
{
    uint8_t buffer[4];
    _put_u32(buffer, value);
    return file.seek(offset) && file.write(buffer, 4) == 4;
}


// Appends the index after the frames and fills in the header counters.
// us_per_frame 0 keeps the rate written at open. Returns the file length, 0 on error
static uint32_t _finalize(File& file, const std::vector<uint32_t>& frame_sizes, uint32_t us_per_frame)
{
    uint32_t frames = frame_sizes.size();
    uint32_t movi_end = AVI_HEADER_SIZE;
    uint32_t max_frame = 0;
    for (auto size : frame_sizes)
    {
        movi_end += 8 + size + (size & 1);
        max_frame = max(max_frame, size);
    }

    // Index, offsets count from the "movi" fourcc
    uint8_t buffer[16 * 32];
    _put_fourcc(buffer, "idx1");
    _put_u32(buffer + 4, frames * 16);
    if (!file.seek(movi_end) || file.write(buffer, 8) != 8)
        return 0;

    uint32_t offset = AVI_HEADER_SIZE - AVI_OFS_MOVI_FOURCC;
    size_t used = 0;
    for (auto size : frame_sizes)
    {
        uint8_t* entry = buffer + used;
        _put_fourcc(entry, "00dc");
        _put_u32(entry + 4, AVIIF_KEYFRAME);
        _put_u32(entry + 8, offset);
        _put_u32(entry + 12, size);
        offset += 8 + size + (size & 1);

        used += 16;
        if (used == sizeof(buffer))
        {
            if (file.write(buffer, used) != used)
                return 0;
            used = 0;
        }
    }
    if (used > 0 && file.write(buffer, used) != used)
        return 0;
    uint32_t file_end = movi_end + 8 + frames * 16;

    bool ok = _patch_u32(file, AVI_OFS_RIFF_SIZE, file_end - 8);
    ok &= _patch_u32(file, AVI_OFS_TOTAL_FRAMES, frames);
    ok &= _patch_u32(file, AVI_OFS_SUGGESTED_BUF, max_frame + 8);
    ok &= _patch_u32(file, AVI_OFS_STRH_LENGTH, frames);
    ok &= _patch_u32(file, AVI_OFS_STRH_SUGG_BUF, max_frame + 8);
    ok &= _patch_u32(file, AVI_OFS_MOVI_SIZE, movi_end - AVI_OFS_MOVI_FOURCC);
    if (us_per_frame > 0)
    {
        uint64_t data_bytes = movi_end - AVI_HEADER_SIZE;
        ok &= _patch_u32(file, AVI_OFS_US_PER_FRAME, us_per_frame);
        ok &= _patch_u32(file, AVI_OFS_STRH_SCALE, us_per_frame);
        ok &= _patch_u32(file, AVI_OFS_MAX_BYTES_SEC, data_bytes * 1000000 / ((uint64_t)frames * us_per_frame));
    }
    return ok ? file_end : 0;
}


bool AviWriter::open(const String& path, int width, int height, int fps)
{
    _file = SD.open(path, FILE_WRITE);
    if (!_file)
    {
        log_e("open %s failed", path.c_str());
        return false;
    }

    _path = path;
    _fps = max(fps, 1);
    _frame_sizes.clear();
    _first_frame_ms = 0;
    _last_frame_ms = 0;

    uint8_t header[AVI_HEADER_SIZE];
    _build_header(header, width, height, 1000000 / _fps);
    if (_file.write(header, sizeof(header)) != sizeof(header))
    {
        log_e("write %s failed", path.c_str());
        _file.close();
        return false;
    }
    return true;
}


bool AviWriter::addFrame(const uint8_t* jpeg, size_t len)
{
    uint8_t chunk[8];
    _put_fourcc(chunk, "00dc");
    _put_u32(chunk + 4, len);
    if (_file.write(chunk, 8) != 8 || _file.write(jpeg, len) != len)
        return false;

    // Chunks are word aligned
    if (len & 1)
    {
        uint8_t pad = 0;
        if (_file.write(&pad, 1) != 1)
            return false;
    }

    uint32_t now = millis();
    if (_frame_sizes.empty())
        _first_frame_ms = now;
    _last_frame_ms = now;
    _frame_sizes.push_back(len);
    return true;
}


void AviWriter::sync()
{
    if (_file)
        _file.flush();
}


bool AviWriter::close()
{
    if (!_file)
        return false;

    // Real rate, the camera and the card do not always keep up with the requested one
    uint32_t frames = _frame_sizes.size();
    uint32_t us_per_frame = 1000000 / _fps;
    if (frames > 1 && _last_frame_ms > _first_frame_ms)
        us_per_frame = (uint64_t)(_last_frame_ms - _first_frame_ms) * 1000 / (frames - 1);

    bool ok = frames > 0 && _finalize(_file, _frame_sizes, us_per_frame) > 0;
    _file.close();

    if (frames == 0)
        SD.remove(_path);
    else
        log_i("closed %s, %u frames, %.1f fps", _path.c_str(), frames, 1000000.0f / us_per_frame);

    _frame_sizes.clear();
    _frame_sizes.shrink_to_fit();
    return ok;
}


bool AviWriter::repairIfNeeded(const String& path)
{
    File file = SD.open(path, "r+");
    if (!file)
        return false;

    uint8_t header[AVI_HEADER_SIZE];
    uint32_t file_size = file.size();
    bool is_ours = file.read(header, sizeof(header)) == sizeof(header) &&
                   memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "AVI ", 4) == 0 &&
                   memcmp(header + AVI_OFS_MOVI_FOURCC, "movi", 4) == 0;
    if (!is_ours)
    {
        file.close();
        // Power went before the header made it
        if (file_size < AVI_HEADER_SIZE)
            SD.remove(path);
        return false;
    }

    // Closed properly
    if (_get_u32(header + AVI_OFS_TOTAL_FRAMES) != 0)
    {
        file.close();
        return true;
    }

    log_i("repair %s", path.c_str());
    std::vector<uint32_t> frame_sizes;
    uint32_t pos = AVI_HEADER_SIZE;
    uint8_t chunk[8];
    while (pos + 8 <= file_size)
    {
        if (!file.seek(pos) || file.read(chunk, 8) != 8 || memcmp(chunk, "00dc", 4) != 0)
            break;

        // Last chunk cut short by the power loss
        uint32_t size = _get_u32(chunk + 4);
        uint32_t next = pos + 8 + size + (size & 1);
        if (size > AVI_MAX_FRAME_SIZE || next > file_size)
            break;

        frame_sizes.push_back(size);
        pos = next;
    }

    uint32_t file_end = frame_sizes.empty() ? 0 : _finalize(file, frame_sizes, 0);
    file.close();

    if (frame_sizes.empty())
    {
        SD.remove(path);
        return false;
    }

    // Drop what is left of the cut frame after the index (players stop at the riff size anyway)
    if (file_end > 0)
    {
        String full_path = AVI_SD_MOUNT_POINT + path;
        truncate(full_path.c_str(), file_end);
    }

    log_i("repaired, %u frames", frame_sizes.size());
    return file_end > 0;
}
