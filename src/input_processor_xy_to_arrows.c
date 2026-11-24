/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_xy_to_arrows

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <drivers/input_processor.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct xy_to_arrows_config {
    int32_t threshold;
    int32_t idle_reset_ms;
};

struct xy_to_arrows_data {
    int32_t acc_x;
    int32_t acc_y;
    int64_t last_event_ms;
};

K_MSGQ_DEFINE(xy_to_arrows_msgq, sizeof(uint32_t), 12, 4);

static uint32_t active_arrow_kc = 0;

static void xy_to_arrows_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    if (active_arrow_kc != 0) {
        uint32_t kc = active_arrow_kc;
        active_arrow_kc = 0;
        raise_zmk_keycode_state_changed_from_encoded(kc, false, k_uptime_get());
        if (k_msgq_num_used_get(&xy_to_arrows_msgq) > 0) {
            k_work_schedule(dwork, K_MSEC(5));
        }
        return;
    }

    uint32_t next_kc = 0;
    if (k_msgq_get(&xy_to_arrows_msgq, &next_kc, K_NO_WAIT) == 0 && next_kc != 0) {
        active_arrow_kc = next_kc;
        raise_zmk_keycode_state_changed_from_encoded(next_kc, true, k_uptime_get());
        k_work_schedule(dwork, K_MSEC(5));
    }
}

static K_WORK_DELAYABLE_DEFINE(xy_to_arrows_work, xy_to_arrows_work_cb);

static void enqueue_arrow_tap(uint32_t kc) {
    if (k_msgq_put(&xy_to_arrows_msgq, &kc, K_NO_WAIT) == 0) {
        /* k_work_schedule is a spinlock-atomic no-op if already DELAYED or QUEUED,
         * and properly re-queues if the work callback is currently K_WORK_RUNNING
         * and about to return after draining msgq. */
        k_work_schedule(&xy_to_arrows_work, K_NO_WAIT);
    }
}

static int xy_to_arrows_handle_event(const struct device *dev, struct input_event *event,
                                     uint32_t param1, uint32_t param2,
                                     struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code != INPUT_REL_X && event->code != INPUT_REL_Y) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct xy_to_arrows_config *cfg = dev->config;
    struct xy_to_arrows_data *data = dev->data;
    int64_t now = k_uptime_get();

    if (cfg->idle_reset_ms > 0 && data->last_event_ms > 0 &&
        (now - data->last_event_ms) > cfg->idle_reset_ms) {
        data->acc_x = 0;
        data->acc_y = 0;
    }
    data->last_event_ms = now;

    int32_t delta = event->value;
    bool is_sync = event->sync;
    /* Consume both value and sync so zmk_input_listener does not transmit
     * 83 Hz zero-delta BLE GATT mouse reports alongside arrow key taps. */
    event->value = 0;
    event->sync = false;

    if (event->code == INPUT_REL_X) {
        if ((delta > 0 && data->acc_x < 0) || (delta < 0 && data->acc_x > 0)) {
            data->acc_x = 0;
        }
        data->acc_x += delta;
    } else {
        if ((delta > 0 && data->acc_y < 0) || (delta < 0 && data->acc_y > 0)) {
            data->acc_y = 0;
        }
        data->acc_y += delta;
    }

    if (!is_sync) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t thr = cfg->threshold > 0 ? cfg->threshold : 36;
    int32_t ax = abs(data->acc_x);
    int32_t ay = abs(data->acc_y);

    if (ax >= thr || ay >= thr) {
        if (ax >= ay) {
            int32_t steps = data->acc_x / thr;
            data->acc_x -= steps * thr;
            data->acc_y = 0;
            uint32_t kc = steps > 0 ? RIGHT_ARROW : LEFT_ARROW;
            int32_t count = abs(steps);
            if (count > 2) {
                count = 2;
                data->acc_x = 0;
            }
            for (int32_t i = 0; i < count; i++) {
                enqueue_arrow_tap(kc);
            }
        } else {
            int32_t steps = data->acc_y / thr;
            data->acc_y -= steps * thr;
            data->acc_x = 0;
            uint32_t kc = steps > 0 ? DOWN_ARROW : UP_ARROW;
            int32_t count = abs(steps);
            if (count > 2) {
                count = 2;
                data->acc_y = 0;
            }
            for (int32_t i = 0; i < count; i++) {
                enqueue_arrow_tap(kc);
            }
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int xy_to_arrows_init(const struct device *dev) {
    struct xy_to_arrows_data *data = dev->data;
    data->acc_x = 0;
    data->acc_y = 0;
    data->last_event_ms = 0;
    return 0;
}

static const struct zmk_input_processor_driver_api xy_to_arrows_driver_api = {
    .handle_event = xy_to_arrows_handle_event,
};

#define XY_TO_ARROWS_INST(n)                                                                       \
    static struct xy_to_arrows_data xy_to_arrows_data_##n = {};                                    \
    static const struct xy_to_arrows_config xy_to_arrows_config_##n = {                            \
        .threshold = DT_INST_PROP_OR(n, threshold, 36),                                            \
        .idle_reset_ms = DT_INST_PROP_OR(n, idle_reset_ms, 150),                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, xy_to_arrows_init, NULL, &xy_to_arrows_data_##n,                      \
                          &xy_to_arrows_config_##n, POST_KERNEL,                                   \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &xy_to_arrows_driver_api);

DT_INST_FOREACH_STATUS_OKAY(XY_TO_ARROWS_INST)

#endif
