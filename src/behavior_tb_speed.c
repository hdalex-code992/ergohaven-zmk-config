/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_tb_speed

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "tb_speed.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_tb_speed_config {
    int32_t direction;
    int32_t hold_reset_ms;
};

struct behavior_tb_speed_data {
    struct k_work_delayable reset_work;
    bool pressed;
    bool reset_fired;
};

static void tb_speed_hold_reset_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_tb_speed_data *data =
        CONTAINER_OF(dwork, struct behavior_tb_speed_data, reset_work);

    if (data->pressed && !data->reset_fired) {
        data->reset_fired = true;
        tb_speed_reset_default();
    }
}

static int on_tb_speed_binding_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    if (!dev) {
        return -ENODEV;
    }
    const struct behavior_tb_speed_config *cfg = dev->config;
    struct behavior_tb_speed_data *data = dev->data;

    data->pressed = true;
    data->reset_fired = false;
    int32_t hold_ms = cfg->hold_reset_ms > 0 ? cfg->hold_reset_ms : 800;
    k_work_reschedule(&data->reset_work, K_MSEC(hold_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_tb_speed_binding_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    if (!dev) {
        return -ENODEV;
    }
    const struct behavior_tb_speed_config *cfg = dev->config;
    struct behavior_tb_speed_data *data = dev->data;

    data->pressed = false;
    k_work_cancel_delayable(&data->reset_work);

    if (!data->reset_fired) {
        tb_speed_step(cfg->direction);
    }
    data->reset_fired = false;

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_tb_speed_init(const struct device *dev) {
    struct behavior_tb_speed_data *data = dev->data;
    data->pressed = false;
    data->reset_fired = false;
    k_work_init_delayable(&data->reset_work, tb_speed_hold_reset_cb);
    return 0;
}

static const struct behavior_driver_api behavior_tb_speed_driver_api = {
    .binding_pressed = on_tb_speed_binding_pressed,
    .binding_released = on_tb_speed_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define TB_SPEED_INST(n)                                                                           \
    static struct behavior_tb_speed_data behavior_tb_speed_data_##n = {};                          \
    static const struct behavior_tb_speed_config behavior_tb_speed_config_##n = {                  \
        .direction = DT_INST_PROP(n, direction),                                                   \
        .hold_reset_ms = DT_INST_PROP_OR(n, hold_reset_ms, 800),                                   \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_tb_speed_init, NULL, &behavior_tb_speed_data_##n,          \
                            &behavior_tb_speed_config_##n, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_tb_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TB_SPEED_INST)

#endif
