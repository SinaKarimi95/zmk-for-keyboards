/*
 * LED strip that ZMK's underglow draws into (chosen zmk,underglow). Passes
 * ZMK's key-light frames through to the real strip, puts the &glow colour on
 * the underglow LEDs, and blanks everything while lights aren't allowed.
 *
 * ZMK redraws every 50 ms while its lighting is on, which also animates the
 * underglow breathe.
 */

#define DT_DRV_COMPAT sina_led_strip_zones

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/rgb_underglow.h>

#include "glow.h"

/* Order of rgb_underglow.c's effect enum: 0 solid, 1 breathe (as &rgb_ug RGB_EFS_CMD 1) */
#define ZMK_EFFECT_BREATHE 1

struct zones_config {
    const struct device *strip;
    struct led_rgb *buf;
    size_t length;
    const uint8_t *glow;
    size_t glow_count;
};

struct zones_data {
    bool dark;
    size_t key_index; /* a key light (not underglow) to read the breathe level from */
};

static bool is_glow_index(const struct zones_config *cfg, size_t index) {
    for (size_t i = 0; i < cfg->glow_count; i++) {
        if (cfg->glow[i] == index) {
            return true;
        }
    }
    return false;
}

static struct led_rgb glow_pixel(const struct zones_config *cfg, const struct zones_data *data,
                                 const struct led_rgb *pixels, size_t n) {
    /* Both on breathe: take the brightness ZMK just gave a key light, so the
     * underglow breathes in step with the keys */
    if (lily_glow_fx() == GLOW_FX_BREATHE && zmk_rgb_underglow_calc_effect(0) == ZMK_EFFECT_BREATHE &&
        data->key_index < n) {
        const struct led_rgb *key = &pixels[data->key_index];
        uint8_t v = MAX(key->r, MAX(key->g, key->b));
        return lily_glow_pixel_value(v / 255.0f);
    }
    return lily_glow_pixel();
}

static int zones_update_rgb(const struct device *dev, struct led_rgb *pixels, size_t num_pixels) {
    const struct zones_config *cfg = dev->config;
    struct zones_data *data = dev->data;
    size_t n = MIN(num_pixels, cfg->length);

    if (!lily_glow_lights_allowed()) {
        /* Send one black frame, then skip until lights are allowed again */
        if (data->dark) {
            return 0;
        }
        memset(cfg->buf, 0, n * sizeof(struct led_rgb));
        data->dark = true;
        return led_strip_update_rgb(cfg->strip, cfg->buf, n);
    }
    data->dark = false;

    /* ZMK's off frame is all black; leave the underglow off with it */
    bool on = false;
    zmk_rgb_underglow_get_state(&on);
    struct led_rgb glow = {0};
    if (on) {
        glow = glow_pixel(cfg, data, pixels, n);
    }

    memcpy(cfg->buf, pixels, n * sizeof(struct led_rgb));

    if (on) {
        for (size_t i = 0; i < cfg->glow_count; i++) {
            if (cfg->glow[i] < n) {
                cfg->buf[cfg->glow[i]] = glow;
            }
        }
    }

    return led_strip_update_rgb(cfg->strip, cfg->buf, n);
}

static int zones_update_channels(const struct device *dev, uint8_t *channels, size_t num_channels) {
    return -ENOTSUP;
}

static const struct led_strip_driver_api zones_api = {
    .update_rgb = zones_update_rgb,
    .update_channels = zones_update_channels,
};

static int zones_init(const struct device *dev) {
    const struct zones_config *cfg = dev->config;
    struct zones_data *data = dev->data;

    if (!device_is_ready(cfg->strip)) {
        LOG_ERR("LED strip %s is not ready", cfg->strip->name);
        return -ENODEV;
    }

    data->key_index = cfg->length;
    for (size_t i = 0; i < cfg->length; i++) {
        if (!is_glow_index(cfg, i)) {
            data->key_index = i;
            break;
        }
    }
    return 0;
}

/* Priority 95: after the real strip (CONFIG_LED_STRIP_INIT_PRIORITY, 90),
 * before ZMK's underglow, which starts at APPLICATION level */
#define ZONES_INIT(n)                                                                              \
    static struct led_rgb zones_buf_##n[DT_INST_PROP(n, chain_length)];                            \
    static const uint8_t zones_glow_##n[] = DT_INST_PROP(n, underglow_indices);                    \
    static struct zones_data zones_data_##n;                                                       \
    static const struct zones_config zones_config_##n = {                                          \
        .strip = DEVICE_DT_GET(DT_INST_PHANDLE(n, led_strip)),                                     \
        .buf = zones_buf_##n,                                                                      \
        .length = DT_INST_PROP(n, chain_length),                                                   \
        .glow = zones_glow_##n,                                                                    \
        .glow_count = DT_INST_PROP_LEN(n, underglow_indices),                                      \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, zones_init, NULL, &zones_data_##n, &zones_config_##n, POST_KERNEL,    \
                          95, &zones_api);

DT_INST_FOREACH_STATUS_OKAY(ZONES_INIT)
