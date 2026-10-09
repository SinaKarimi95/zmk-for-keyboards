/*
 * &glow: underglow colour and breathe/solid, separate from the key lights.
 *
 * Global locality, so every change runs on both halves. Relative commands
 * (next colour, toggle effect) are converted to absolute ones on the central
 * before relaying, the same trick ZMK's &rgb_ug uses, so the halves can't drift.
 *
 * Also owns the "lights allowed" rule. The central decides for both halves:
 * allowed while active, or while its USB is plugged in. It tells the
 * peripheral on change and every HEARTBEAT_MS. The peripheral is also allowed
 * while its own USB is plugged in, and turns off if the central goes quiet.
 */

#define DT_DRV_COMPAT sina_behavior_glow

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <hal/nrf_power.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <drivers/behavior.h>
#include <zmk/activity.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>

#include "glow.h"

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define GLOW_IS_CENTRAL 1
#else
#define GLOW_IS_CENTRAL 0
#endif

#define BREATHE_PERIOD_MS 4000
/* A multiple of BREATHE_PERIOD_MS, so re-syncing the breathe phase on each
 * heartbeat doesn't visibly jump */
#define HEARTBEAT_MS 20000
#define POLL_MS 5000
#define CENTRAL_STALE_MS 60000

struct glow_color {
    uint16_t h;
    uint8_t s;
    const char *name;
};

/* Names are at most 6 characters so "GLOW <NAME>" fits the left screen */
static const struct glow_color colors[] = {
    {0, 100, "RED"},     {25, 100, "ORANGE"}, {50, 100, "YELLOW"}, {85, 100, "LIME"},
    {120, 100, "GREEN"}, {155, 100, "MINT"},  {180, 100, "CYAN"},  {205, 100, "SKY"},
    {235, 100, "BLUE"},  {275, 100, "PURPLE"}, {320, 70, "PINK"},  {0, 0, "WHITE"},
};

#define COLOR_COUNT ARRAY_SIZE(colors)
#define DEFAULT_COLOR 8 /* BLUE */

struct glow_state {
    uint8_t color;
    uint8_t fx;
};

static struct glow_state state = {.color = DEFAULT_COLOR, .fx = GLOW_FX_BREATHE};

static atomic_t serial;
static uint8_t last_change;
static atomic_t allowed = ATOMIC_INIT(1);
static int64_t phase_base;

uint8_t lily_glow_color_count(void) { return COLOR_COUNT; }

const char *lily_glow_color_name(uint8_t index) {
    return index < COLOR_COUNT ? colors[index].name : "?";
}

uint8_t lily_glow_color(void) { return state.color; }

uint8_t lily_glow_fx(void) { return state.fx; }

uint32_t lily_glow_serial(void) { return (uint32_t)atomic_get(&serial); }

uint8_t lily_glow_last_change(void) { return last_change; }

const char *lily_color_name(uint16_t hue, uint8_t sat) {
    if (sat < 30) {
        return "WHITE";
    }

    int best = 0;
    int best_dist = 1000;
    for (int i = 0; i < COLOR_COUNT; i++) {
        if (colors[i].s == 0) {
            continue;
        }
        int dist = abs((int)hue - (int)colors[i].h);
        if (dist > 180) {
            dist = 360 - dist;
        }
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return colors[best].name;
}

bool lily_glow_lights_allowed(void) { return atomic_get(&allowed) != 0; }

/* Same conversion as ZMK's rgb_underglow.c; h in degrees, s and v 0..1 */
static struct led_rgb hsv_to_rgb(float h, float s, float v) {
    int i = (int)(h / 60.0f);
    float f = h / 60.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float r, g, b;

    switch (i % 6) {
    case 0:
        r = v, g = t, b = p;
        break;
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    default:
        r = v, g = p, b = q;
        break;
    }

    return (struct led_rgb){.r = (uint8_t)(255 * r), .g = (uint8_t)(255 * g), .b = (uint8_t)(255 * b)};
}

struct led_rgb lily_glow_pixel_value(float v) {
    const struct glow_color *c = &colors[state.color < COLOR_COUNT ? state.color : DEFAULT_COLOR];
    return hsv_to_rgb(c->h, c->s / 100.0f, v);
}

struct led_rgb lily_glow_pixel(void) {
    /* Follow the key lights' brightness, scaled the way ZMK scales it */
    struct zmk_led_hsb keys = zmk_rgb_underglow_calc_brt(0);
    float v = (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
               (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * keys.b /
                   100.0f) /
              100.0f;

    if (state.fx == GLOW_FX_BREATHE) {
        int64_t elapsed = k_uptime_get() - phase_base;
        if (elapsed < 0) {
            elapsed = 0;
        }
        float x = (float)(elapsed % BREATHE_PERIOD_MS) / BREATHE_PERIOD_MS;
        float tri = x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x;
        v *= tri * tri * (3.0f - 2.0f * tri);
    }

    return lily_glow_pixel_value(v);
}

/* Saving, debounced like ZMK's own RGB settings */

#if IS_ENABLED(CONFIG_SETTINGS)
static int glow_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }
        struct glow_state saved;
        int rc = read_cb(cb_arg, &saved, sizeof(saved));
        if (rc < 0) {
            return rc;
        }
        if (saved.color < COLOR_COUNT) {
            state.color = saved.color;
        }
        state.fx = saved.fx ? GLOW_FX_BREATHE : GLOW_FX_SOLID;
        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(lily_glow, "lily_glow", NULL, glow_settings_set, NULL, NULL);

static void save_work_cb(struct k_work *work) {
    settings_save_one("lily_glow/state", &state, sizeof(state));
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_cb);

static void save_state(void) {
    k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}
#else
static void save_state(void) {}
#endif

/* Lights allowed: always on when plugged in, off when idle on battery */

static bool vbus_present(void) { return nrf_power_usbregstatus_vbusdet_get(NRF_POWER); }

#if GLOW_IS_CENTRAL
static bool sent_valid;
static bool last_sent;

static void relay_lights(bool lights) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_DRV_INST(0)),
        .param1 = GLOW_CMD_LIGHTS,
        .param2 = lights,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    zmk_behavior_invoke_binding(&binding, event, true);
}

static void evaluate(bool heartbeat) {
    bool lights = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE || vbus_present();
    atomic_set(&allowed, lights ? 1 : 0);

    if (!sent_valid || lights != last_sent || heartbeat) {
        phase_base = k_uptime_get();
        relay_lights(lights);
        sent_valid = true;
        last_sent = lights;
    }
}
#else
static bool central_allowed = true;
static int64_t central_at;

static void evaluate(bool heartbeat) {
    bool fresh = (k_uptime_get() - central_at) < CENTRAL_STALE_MS;
    bool lights = vbus_present() || (central_allowed && fresh);
    atomic_set(&allowed, lights ? 1 : 0);
}
#endif

static void poll_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_work_cb);

static int64_t last_heartbeat;

static void poll_work_cb(struct k_work *work) {
    int64_t now = k_uptime_get();
    bool heartbeat = now - last_heartbeat >= HEARTBEAT_MS;
    if (heartbeat) {
        last_heartbeat = now;
    }

    evaluate(heartbeat);
    k_work_schedule(&poll_work, K_MSEC(POLL_MS));
}

#if GLOW_IS_CENTRAL
static int glow_listener(const zmk_event_t *eh) {
    k_work_reschedule(&poll_work, K_NO_WAIT);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lily_glow, glow_listener);
ZMK_SUBSCRIPTION(lily_glow, zmk_activity_state_changed);
#endif

static int glow_init(void) {
#if !GLOW_IS_CENTRAL
    central_at = k_uptime_get();
#endif
    /* Give BLE time to come up before the first relay */
    k_work_schedule(&poll_work, K_SECONDS(3));
    return 0;
}

SYS_INIT(glow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* The &glow behavior */

static int glow_convert(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case GLOW_CMD_NEXT:
        binding->param1 = GLOW_CMD_SET_COLOR;
        binding->param2 = (state.color + 1) % COLOR_COUNT;
        break;
    case GLOW_CMD_PREV:
        binding->param1 = GLOW_CMD_SET_COLOR;
        binding->param2 = (state.color + COLOR_COUNT - 1) % COLOR_COUNT;
        break;
    case GLOW_CMD_FX_TOGGLE:
        binding->param1 = GLOW_CMD_SET_FX;
        binding->param2 = state.fx == GLOW_FX_BREATHE ? GLOW_FX_SOLID : GLOW_FX_BREATHE;
        break;
    }
    return 0;
}

static int glow_pressed(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    /* Normally already converted on the central; convert here if not */
    glow_convert(binding, event);

    switch (binding->param1) {
    case GLOW_CMD_SET_COLOR:
        if (binding->param2 < COLOR_COUNT) {
            state.color = binding->param2;
            last_change = GLOW_CHANGE_COLOR;
            atomic_inc(&serial);
            save_state();
        }
        break;
    case GLOW_CMD_SET_FX:
        state.fx = binding->param2 ? GLOW_FX_BREATHE : GLOW_FX_SOLID;
        phase_base = k_uptime_get();
        last_change = GLOW_CHANGE_FX;
        atomic_inc(&serial);
        save_state();
        break;
    case GLOW_CMD_LIGHTS:
#if !GLOW_IS_CENTRAL
        central_allowed = binding->param2 != 0;
        central_at = k_uptime_get();
        phase_base = central_at;
        evaluate(false);
#endif
        break;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int glow_released(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api glow_driver_api = {
    .binding_convert_central_state_dependent_params = glow_convert,
    .binding_pressed = glow_pressed,
    .binding_released = glow_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &glow_driver_api);
