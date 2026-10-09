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

struct zones_config {
    const struct device *strip;
    struct led_rgb *buf;
    size_t length;
    const uint8_t *glow;
    size_t glow_count;
};

struct zones_data {
    bool dark;
};

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

    memcpy(cfg->buf, pixels, n * sizeof(struct led_rgb));

    /* ZMK's off frame is all black; leave the underglow off with it */
    bool on = false;
    zmk_rgb_underglow_get_state(&on);
    if (on) {
        struct led_rgb glow = lily_glow_pixel();
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

    if (!device_is_ready(cfg->strip)) {
        LOG_ERR("LED strip %s is not ready", cfg->strip->name);
        return -ENODEV;
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
