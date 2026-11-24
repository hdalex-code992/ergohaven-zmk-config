/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define TB_SPEED_MIN_PRESET     1
#define TB_SPEED_MAX_PRESET     5
#define TB_SPEED_DEFAULT_PRESET 3

struct tb_speed_preset_params {
    int32_t scroll_mul;
    int32_t scroll_div;
    int32_t text_threshold;
};

uint8_t tb_speed_get_preset(void);
const struct tb_speed_preset_params *tb_speed_get_params(void);
void tb_speed_step(int32_t delta);
void tb_speed_reset_default(void);
