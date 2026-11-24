/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_tb_scroll_scaler

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include "tb_speed.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * 5 Persistent Speed Presets (at 1000 CPI + ZMK 16x USB Hi-Res Smooth Scrolling, 16 units = 1 notch = 3 lines):
 *   Preset 1 (-2.5x):  scroll 1/5 (80 counts / 3 lines = 2.03 mm), text threshold = 90 (2.29 mm)
 *   Preset 2 (-2.0x):  scroll 1/4 (64 counts / 3 lines = 1.63 mm), text threshold = 72 (1.83 mm)
 *   Preset 3 (-1.75x): scroll 2/7 (56 counts / 3 lines = 1.42 mm), text threshold = 64 (1.63 mm) [DEFAULT]
 *   Preset 4 (-1.5x):  scroll 1/3 (48 counts / 3 lines = 1.22 mm), text threshold = 54 (1.37 mm)
 *   Preset 5 ( 1.0x):  scroll 1/2 (32 counts / 3 lines = 0.81 mm), text threshold = 40 (1.02 mm)
 */
static const struct tb_speed_preset_params preset_table[5] = {
    {.scroll_mul = 1, .scroll_div = 5, .text_threshold = 90},
    {.scroll_mul = 1, .scroll_div = 4, .text_threshold = 72},
    {.scroll_mul = 2, .scroll_div = 7, .text_threshold = 64},
    {.scroll_mul = 1, .scroll_div = 3, .text_threshold = 54},
    {.scroll_mul = 1, .scroll_div = 2, .text_threshold = 40},
};

static uint8_t current_preset = TB_SPEED_DEFAULT_PRESET;

#if IS_ENABLED(CONFIG_SETTINGS)
static void tb_speed_save_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    uint8_t val = current_preset;
    int rc = settings_save_one("tb_spd/preset", &val, sizeof(val));
    if (rc != 0) {
        LOG_WRN("Failed to save tb_spd/preset (%d)", rc);
    }
}

static K_WORK_DELAYABLE_DEFINE(tb_speed_save_work, tb_speed_save_work_cb);

static int tb_speed_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "preset", &next) && !next) {
        if (len != sizeof(uint8_t)) {
            return -EINVAL;
        }
        uint8_t val = TB_SPEED_DEFAULT_PRESET;
        int rc = read_cb(cb_arg, &val, sizeof(val));
        if (rc >= 0) {
            if (val >= TB_SPEED_MIN_PRESET && val <= TB_SPEED_MAX_PRESET) {
                current_preset = val;
            } else {
                current_preset = TB_SPEED_DEFAULT_PRESET;
            }
            LOG_INF("Restored Trackball speed preset %u", current_preset);
            return 0;
        }
        return rc;
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(tb_spd, "tb_spd", NULL, tb_speed_settings_set, NULL, NULL);

static void schedule_preset_save(void) {
    k_work_reschedule(&tb_speed_save_work, K_MSEC(400));
}
#else
static void schedule_preset_save(void) {}
#endif

uint8_t tb_speed_get_preset(void) {
    uint8_t p = current_preset;
    if (p < TB_SPEED_MIN_PRESET || p > TB_SPEED_MAX_PRESET) {
        return TB_SPEED_DEFAULT_PRESET;
    }
    return p;
}

const struct tb_speed_preset_params *tb_speed_get_params(void) {
    return &preset_table[tb_speed_get_preset() - 1];
}

void tb_speed_step(int32_t delta) {
    int32_t next = (int32_t)tb_speed_get_preset() + delta;
    if (next < TB_SPEED_MIN_PRESET) {
        next = TB_SPEED_MIN_PRESET;
    } else if (next > TB_SPEED_MAX_PRESET) {
        next = TB_SPEED_MAX_PRESET;
    }
    if ((uint8_t)next != current_preset) {
        current_preset = (uint8_t)next;
        LOG_INF("Trackball speed preset -> %u", current_preset);
        schedule_preset_save();
    }
}

void tb_speed_reset_default(void) {
    if (current_preset != TB_SPEED_DEFAULT_PRESET) {
        current_preset = TB_SPEED_DEFAULT_PRESET;
        LOG_INF("Trackball speed preset reset -> %u", current_preset);
        schedule_preset_save();
    }
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct tb_scroll_scaler_config {
    int32_t idle_reset_ms;
};

struct tb_scroll_scaler_data {
    int32_t rem_wheel;
    int32_t rem_hwheel;
    int64_t last_event_ms;
    uint8_t last_preset;
    bool non_zero_in_frame;
};

static int tb_scroll_scaler_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code != INPUT_REL_WHEEL && event->code != INPUT_REL_HWHEEL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct tb_scroll_scaler_config *cfg = dev->config;
    struct tb_scroll_scaler_data *data = dev->data;
    int64_t now = k_uptime_get();
    uint8_t preset = tb_speed_get_preset();

    if (data->last_preset != preset ||
        (cfg->idle_reset_ms > 0 && data->last_event_ms > 0 &&
         (now - data->last_event_ms) > cfg->idle_reset_ms)) {
        data->rem_wheel = 0;
        data->rem_hwheel = 0;
        data->last_preset = preset;
    }
    if (event->value != 0) {
        data->last_event_ms = now;
    }

    const struct tb_speed_preset_params *params = &preset_table[preset - 1];
    int32_t *rem = (event->code == INPUT_REL_WHEEL) ? &data->rem_wheel : &data->rem_hwheel;
    int32_t delta = event->value;

    if (delta != 0 && ((*rem < 0 && delta > 0) || (*rem > 0 && delta < 0))) {
        *rem = 0;
    }

    int32_t val_mul = delta * params->scroll_mul + *rem;
    int32_t scaled = val_mul / params->scroll_div;
    *rem = val_mul - (scaled * params->scroll_div);

    event->value = (int32_t)CLAMP(scaled, INT16_MIN, INT16_MAX);
    if (event->value != 0) {
        data->non_zero_in_frame = true;
    }

    if (event->sync) {
        if (!data->non_zero_in_frame) {
            event->sync = false;
        }
        data->non_zero_in_frame = false;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int tb_scroll_scaler_init(const struct device *dev) {
    struct tb_scroll_scaler_data *data = dev->data;
    data->rem_wheel = 0;
    data->rem_hwheel = 0;
    data->last_event_ms = 0;
    data->last_preset = TB_SPEED_DEFAULT_PRESET;
    data->non_zero_in_frame = false;
    return 0;
}

static const struct zmk_input_processor_driver_api tb_scroll_scaler_driver_api = {
    .handle_event = tb_scroll_scaler_handle_event,
};

#define TB_SCROLL_SCALER_INST(n)                                                                   \
    static struct tb_scroll_scaler_data tb_scroll_scaler_data_##n = {};                            \
    static const struct tb_scroll_scaler_config tb_scroll_scaler_config_##n = {                    \
        .idle_reset_ms = DT_INST_PROP_OR(n, idle_reset_ms, 200),                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, tb_scroll_scaler_init, NULL, &tb_scroll_scaler_data_##n,              \
                          &tb_scroll_scaler_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tb_scroll_scaler_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TB_SCROLL_SCALER_INST)

#endif
