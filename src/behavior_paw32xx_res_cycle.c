/*
 * Copyright (c) 2026 Vincent Franco
 * SPDX-License-Identifier: MIT
 *
 * Adapted from george-norton/zmk-behavior-sensor-attr-cycle (MIT, Copyright
 * (c) 2025 George Norton). That behavior drives sensor_attr_set(), which the
 * PAW3222 driver does not implement (it is a Zephyr input driver registered
 * with a NULL device API). This variant calls the driver's exported
 * paw32xx_set_resolution() instead.
 *
 * Locality is BEHAVIOR_LOCALITY_EVENT_SOURCE: the behavior runs on whichever
 * half the key was pressed on, so it can target the sensor local to that
 * half. This requires the behavior node to be enabled on BOTH halves, and the
 * node name to fit in the split transport's 16-byte behavior_dev field.
 */
#define DT_DRV_COMPAT zmk_behavior_paw32xx_res_cycle

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Exported by zmk-driver-paw3222 (include/paw3222.h); that directory is
 * private to the driver library, so declare the prototype here and let the
 * symbol resolve at link time. */
int paw32xx_set_resolution(const struct device *dev, uint16_t res_cpi);

#define SETTINGS_PREFIX "paw_res"

struct behavior_paw32xx_res_cycle_config {
    const struct device *sensor_device;
    const char *settings_key;
    int32_t save_delay;
    int32_t load_delay;
    bool persistent;
    uint8_t length;
    int32_t values[];
};

struct behavior_paw32xx_res_cycle_state {
    uint8_t index;
};

struct behavior_paw32xx_res_cycle_data {
    const struct device *dev;
#if IS_ENABLED(CONFIG_SETTINGS)
    struct k_work_delayable load_work;
    struct k_work_delayable save_work;
#endif
    struct behavior_paw32xx_res_cycle_state state;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Next",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = 1,
    },
    {
        .display_name = "Previous",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = -1,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

#if IS_ENABLED(CONFIG_SETTINGS)

static void save_work_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_paw32xx_res_cycle_data *data =
        CONTAINER_OF(dwork, struct behavior_paw32xx_res_cycle_data, save_work);
    const struct behavior_paw32xx_res_cycle_config *config = data->dev->config;

    int err = settings_save_one(config->settings_key, &data->state,
                                sizeof(struct behavior_paw32xx_res_cycle_state));
    if (err < 0) {
        LOG_ERR("Failed to save settings %d", err);
    }
}

static void load_work_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_paw32xx_res_cycle_data *data =
        CONTAINER_OF(dwork, struct behavior_paw32xx_res_cycle_data, load_work);
    const struct behavior_paw32xx_res_cycle_config *config = data->dev->config;

    int err = paw32xx_set_resolution(config->sensor_device,
                                     (uint16_t)config->values[data->state.index]);
    if (err < 0) {
        LOG_ERR("Failed to restore resolution on %s (err %d)", config->sensor_device->name, err);
    }
}

#endif

static int behavior_paw32xx_res_cycle_init(const struct device *dev) {
    struct behavior_paw32xx_res_cycle_data *data = dev->data;
    data->dev = dev;

#if IS_ENABLED(CONFIG_SETTINGS)
    const struct behavior_paw32xx_res_cycle_config *config = dev->config;
    if (config->persistent) {
        k_work_init_delayable(&data->save_work, save_work_callback);
    }
#endif
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_paw32xx_res_cycle_data *data = dev->data;
    const struct behavior_paw32xx_res_cycle_config *config = dev->config;

    int next = ((int)data->state.index + binding->param1) % (int)config->length;
    if (next < 0) {
        next += config->length;
    }
    data->state.index = (uint8_t)next;

    int32_t value = config->values[data->state.index];
    int err = paw32xx_set_resolution(config->sensor_device, (uint16_t)value);
    if (err < 0) {
        LOG_ERR("Failed to set resolution %d on %s (err %d)", value, config->sensor_device->name,
                err);
        return err;
    }
    LOG_INF("Set %s resolution to %d CPI", config->sensor_device->name, value);

#if IS_ENABLED(CONFIG_SETTINGS)
    if (config->persistent) {
        // Limit flash writes: the user will likely press this several times
        // in a row looking for a value, so debounce the save.
        k_work_reschedule(&data->save_work, K_MSEC(config->save_delay));
    }
#endif
    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return 0;
}

static const struct behavior_driver_api behavior_paw32xx_res_cycle_driver_api = {
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define RES_CYCLE_INST(n)                                                                          \
    static struct behavior_paw32xx_res_cycle_data data##n = {};                                    \
    static const struct behavior_paw32xx_res_cycle_config config##n = {                            \
        .sensor_device = DEVICE_DT_GET(DT_INST_PHANDLE(n, sensor_device)),                         \
        .length = DT_PROP_LEN(DT_DRV_INST(n), values),                                             \
        .values = DT_PROP(DT_DRV_INST(n), values),                                                 \
        .save_delay = DT_PROP(DT_DRV_INST(n), save_delay),                                         \
        .load_delay = DT_PROP(DT_DRV_INST(n), load_delay),                                         \
        .persistent = DT_PROP(DT_DRV_INST(n), persistent),                                         \
        .settings_key = SETTINGS_PREFIX "/" #n,                                                    \
    };                                                                                             \
    /* Must init after the sensor_device dependency: the PAW32xx driver       \
     * inits at POST_KERNEL/CONFIG_INPUT_INIT_PRIORITY (default 90). */       \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_paw32xx_res_cycle_init, NULL, &data##n, &config##n,        \
                            POST_KERNEL, 95, &behavior_paw32xx_res_cycle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RES_CYCLE_INST)

#if IS_ENABLED(CONFIG_SETTINGS)

#define RES_CYCLE_SETTINGS_INST(n)                                                                 \
    case n: {                                                                                      \
        data = &data##n;                                                                           \
        config = &config##n;                                                                       \
        break;                                                                                     \
    }

// Called at startup for each stored key under our prefix.
static int res_cycle_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                      void *cb_arg) {
    struct behavior_paw32xx_res_cycle_data *data = NULL;
    const struct behavior_paw32xx_res_cycle_config *config = NULL;
    char *endptr;
    long identifier = strtol(name, &endptr, 10);
    int err = 0;

    if (endptr == name) {
        return -ENOENT;
    }

    // The identifier is the devicetree instance index.
    switch (identifier) {
        DT_INST_FOREACH_STATUS_OKAY(RES_CYCLE_SETTINGS_INST)
    default:
        return -ENOENT;
    }

    if (config->persistent) {
        err = read_cb(cb_arg, &data->state, sizeof(struct behavior_paw32xx_res_cycle_state));
        if (err >= 0) {
            if (data->state.index >= config->length) {
                data->state.index = 0;
            } else {
                k_work_init_delayable(&data->load_work, load_work_callback);
                k_work_schedule(&data->load_work, K_MSEC(config->load_delay));
            }
        } else {
            LOG_ERR("Failed to load settings %d", err);
        }
    }
    return MIN(err, 0);
}

SETTINGS_STATIC_HANDLER_DEFINE(paw32xx_res_cycle, SETTINGS_PREFIX, NULL, res_cycle_settings_load_cb,
                               NULL, NULL);
#endif

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
