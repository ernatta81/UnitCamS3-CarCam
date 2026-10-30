/**
 * @file motion.cpp
 * @brief
 * @version 0.1
 * @date 2026-10-29
 *
 */
#include "motion.h"
#include <esp_jpg_decode.h>


struct GridDecoder_t
{
    const uint8_t* jpeg;
    size_t len;
    // Decoded (scaled) image size
    uint16_t width;
    uint16_t height;
    uint32_t sum[MOTION_CELLS];
    uint16_t count[MOTION_CELLS];
};


static size_t _jpg_read(void* arg, size_t index, uint8_t* buf, size_t len)
{
    auto* decoder = (GridDecoder_t*)arg;
    if (index >= decoder->len)
        return 0;
    len = min(len, decoder->len - index);
    if (buf != nullptr)
        memcpy(buf, decoder->jpeg + index, len);
    return len;
}


static bool _jpg_write(void* arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t* data)
{
    auto* decoder = (GridDecoder_t*)arg;

    // First call (at 0, 0) gives the output size, the last one has no data either
    if (data == nullptr)
    {
        if (x == 0 && y == 0)
        {
            decoder->width = w;
            decoder->height = h;
        }
        return true;
    }
    if (decoder->width == 0 || decoder->height == 0)
        return false;

    for (uint16_t iy = 0; iy < h; iy++)
    {
        int cy = min((y + iy) * APP_MOTION_GRID_H / decoder->height, APP_MOTION_GRID_H - 1);
        for (uint16_t ix = 0; ix < w; ix++)
        {
            int cx = min((x + ix) * APP_MOTION_GRID_W / decoder->width, APP_MOTION_GRID_W - 1);
            // 3 bytes per pixel, green counts double so the red / blue order does not matter
            const uint8_t* p = data + (iy * w + ix) * 3;
            int cell = cy * APP_MOTION_GRID_W + cx;
            decoder->sum[cell] += (p[0] + 2 * p[1] + p[2]) >> 2;
            decoder->count[cell]++;
        }
    }
    return true;
}


bool Motion::get_grid(const uint8_t* jpeg, size_t len, uint8_t* grid)
{
    auto* decoder = (GridDecoder_t*)calloc(1, sizeof(GridDecoder_t));
    if (decoder == nullptr)
        return false;
    decoder->jpeg = jpeg;
    decoder->len = len;

    // 1/8 scale: VGA becomes 80x60, plenty for a 16x12 grid and fast to decode
    esp_err_t err = esp_jpg_decode(len, JPG_SCALE_8X, _jpg_read, _jpg_write, decoder);
    bool ok = err == ESP_OK;
    for (int i = 0; ok && i < MOTION_CELLS; i++)
    {
        if (decoder->count[i] == 0)
            ok = false;
        else
            grid[i] = decoder->sum[i] / decoder->count[i];
    }

    if (!ok)
        log_e("jpeg decode failed: 0x%x", err);
    free(decoder);
    return ok;
}


int Motion::count_changed_cells(const uint8_t* reference, const uint8_t* grid, int threshold)
{
    int sum_reference = 0;
    int sum_grid = 0;
    for (int i = 0; i < MOTION_CELLS; i++)
    {
        sum_reference += reference[i];
        sum_grid += grid[i];
    }
    int shift = (sum_grid - sum_reference) / MOTION_CELLS;

    int changed = 0;
    for (int i = 0; i < MOTION_CELLS; i++)
    {
        if (abs(grid[i] - reference[i] - shift) > threshold)
            changed++;
    }
    return changed;
}
