/**
 * @file motion.h
 * @brief Motion check between two deep sleep wakes: compares a coarse brightness grid
 * @version 0.1
 * @date 2026-10-29
 *
 */
#pragma once
#include <Arduino.h>
#include "../app_config.h"

#define MOTION_CELLS (APP_MOTION_GRID_W * APP_MOTION_GRID_H)

namespace Motion
{
    // Average brightness of each grid cell, from the jpeg decoded at 1/8 scale
    bool get_grid(const uint8_t* jpeg, size_t len, uint8_t* grid);

    // Cells that changed more than threshold, after removing the overall shift (clouds, dusk, auto exposure)
    int count_changed_cells(const uint8_t* reference, const uint8_t* grid, int threshold);
}
