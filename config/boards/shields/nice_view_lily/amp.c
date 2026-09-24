/*
 * Left (central) screen: guitar amp.
 *
 *   header    output (USB or BT profile + link tick) and battery
 *   VU meter  needle follows typing speed
 *   fretboard six strings = layers 0-5; the active layer's string wobbles
 *             harder and faster the faster you type, and is straight at 0 WPM
 *   footer    layer digits (active one inverted) and the layer name
 *
 * Blanks itself when ZMK goes idle (the ls0xx driver can't blank the panel).
 * Listener pattern follows ZMK's nice_view shield (MIT).
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

#include "fb.h"
#include "lily.h"

#define FRAME_MS 50
#define WPM_MAX 140
#define STRING_COUNT 6
#define STRING_TOP 72
#define STRING_BOTTOM 128
#define NAME_MAX_CHARS 11

static struct {
    uint8_t battery;
    bool charging;
    bool usb_out;
    int profile;
    bool connected;
    uint8_t layer;
    const char *label;
    uint8_t wpm;
    float wpm_smooth;
    float phase;
    bool idle;
} st;

static lv_timer_t *anim;

static void draw_header(void) {
    if (st.usb_out) {
        nv_text("USB", 2, 2, true);
    } else {
        char buf[8];
        snprintf(buf, sizeof(buf), "BT%d", st.profile + 1);
        nv_text(buf, 2, 2, true);
        int x = 2 + nv_text_width(buf) + 4;
        if (st.connected) {
            nv_check(x, 1);
        } else {
            nv_cross(x, 2);
        }
    }
    nv_battery(49, 2, st.battery, st.charging);
    nv_hline(12);
}

static void draw_meter(float wpm) {
    const int cx = 34, cy = 58;

    nv_box(2, 15, 64, 38);

    for (int d10 = -1400; d10 <= -400; d10 += 15) {
        float a = d10 * NV_PI / 1800.0f;
        nv_px(cx + nv_round(36 * nv_cos(a)), cy + nv_round(36 * nv_sin(a)), true);
    }
    for (int d = -140; d <= -40; d += 20) {
        float a = d * NV_PI / 180.0f;
        nv_line(cx + nv_round(31 * nv_cos(a)), cy + nv_round(31 * nv_sin(a)),
                cx + nv_round(36 * nv_cos(a)), cy + nv_round(36 * nv_sin(a)));
    }

    float a = (-140.0f + wpm / WPM_MAX * 100.0f) * NV_PI / 180.0f;
    nv_line(cx + nv_round(10 * nv_cos(a)), cy + nv_round(10 * nv_sin(a)),
            cx + nv_round(34 * nv_cos(a)), cy + nv_round(34 * nv_sin(a)));
    nv_rect(31, 49, 7, 3, true);
    nv_text("VU", 53, 44, true);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", st.wpm);
    nv_text("WPM", 2, 56, true);
    nv_text(buf, 66 - nv_text_width(buf), 56, true);
    nv_hline(66);
}

static void draw_fretboard(float wpm) {
    static const int frets[] = {84, 98, 112, 126};

    nv_rect(4, 70, 60, 2, true);
    for (int f = 0; f < ARRAY_SIZE(frets); f++) {
        for (int x = 4; x < 64; x++) {
            nv_px(x, frets[f], true);
        }
    }
    nv_rect(33, 90, 3, 3, true);
    nv_rect(33, 118, 3, 3, true);

    float amp = 3.5f * wpm / WPM_MAX;
    float swing = nv_sin(st.phase);

    for (int i = 0; i < STRING_COUNT; i++) {
        int x = 9 + 10 * i;
        bool active = st.layer == i;
        for (int y = STRING_TOP; y <= STRING_BOTTOM; y++) {
            if (!active) {
                nv_px(x, y, true);
                continue;
            }
            int o = 0;
            if (amp >= 0.3f) {
                float along = NV_PI * (y - STRING_TOP) / (STRING_BOTTOM - STRING_TOP);
                o = nv_round(amp * nv_sin(along) * swing);
            }
            nv_px(x + o, y, true);
            nv_px(x + o + 1, y, true);
        }
    }
}

static void draw_footer(void) {
    for (int i = 0; i < STRING_COUNT; i++) {
        int x = 7 + 10 * i;
        char d[2] = {'0' + i, '\0'};
        if (st.layer == i) {
            nv_rect(x - 3, 131, 11, 9, true);
            nv_text(d, x, 132, false);
        } else {
            nv_text(d, x, 132, true);
        }
    }

    char name[NAME_MAX_CHARS + 1];
    if (st.label == NULL || st.label[0] == '\0') {
        snprintf(name, sizeof(name), "LAYER %d", st.layer);
    } else {
        strncpy(name, st.label, NAME_MAX_CHARS);
        name[NAME_MAX_CHARS] = '\0';
    }
    nv_text(name, (NV_W - nv_text_width(name)) / 2, 145, true);
}

static void draw(void) {
    nv_clear();
    if (!st.idle) {
        float wpm = st.wpm_smooth > WPM_MAX ? WPM_MAX : st.wpm_smooth;
        draw_header();
        draw_meter(wpm);
        draw_fretboard(wpm);
        draw_footer();
    }
    nv_flush();
}

static void anim_cb(lv_timer_t *timer) {
    if (st.idle) {
        lv_timer_pause(timer);
        return;
    }

    st.wpm_smooth += ((float)st.wpm - st.wpm_smooth) * 0.15f;
    if (st.wpm == 0 && st.wpm_smooth < 0.5f) {
        st.wpm_smooth = 0.0f;
    }

    float wpm = st.wpm_smooth > WPM_MAX ? WPM_MAX : st.wpm_smooth;
    st.phase += FRAME_MS * (0.012f + wpm / WPM_MAX * 0.03f);
    while (st.phase > 2 * NV_PI) {
        st.phase -= 2 * NV_PI;
    }

    draw();

    /* String at rest and needle parked: nothing moves, so stop redrawing */
    if (st.wpm == 0 && st.wpm_smooth == 0.0f) {
        lv_timer_pause(timer);
    }
}

static void kick_anim(void) {
    if (anim != NULL && !st.idle) {
        lv_timer_resume(anim);
    }
}

/* Battery */

struct amp_battery_state {
    uint8_t level;
    bool usb_present;
};

static void battery_update_cb(struct amp_battery_state state) {
    st.battery = state.level;
    st.charging = state.usb_present;
    draw();
}

static struct amp_battery_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct amp_battery_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(amp_battery, struct amp_battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(amp_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(amp_battery, zmk_usb_conn_state_changed);
#endif

/* Output: USB or BLE profile */

struct amp_output_state {
    bool usb_out;
    int profile;
    bool connected;
};

static void output_update_cb(struct amp_output_state state) {
    st.usb_out = state.usb_out;
    st.profile = state.profile;
    st.connected = state.connected;
    draw();
}

static struct amp_output_state output_get_state(const zmk_event_t *eh) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    return (struct amp_output_state){
        .usb_out = ep.transport == ZMK_TRANSPORT_USB,
#if IS_ENABLED(CONFIG_ZMK_BLE)
        .profile = zmk_ble_active_profile_index(),
        .connected = zmk_ble_active_profile_is_connected(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(amp_output, struct amp_output_state, output_update_cb,
                            output_get_state)
ZMK_SUBSCRIPTION(amp_output, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(amp_output, zmk_usb_conn_state_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(amp_output, zmk_ble_active_profile_changed);
#endif

/* Layer */

struct amp_layer_state {
    uint8_t index;
    const char *label;
};

static void layer_update_cb(struct amp_layer_state state) {
    st.layer = state.index;
    st.label = state.label;
    draw();
}

static struct amp_layer_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct amp_layer_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(amp_layer, struct amp_layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(amp_layer, zmk_layer_state_changed);

/* WPM */

struct amp_wpm_state {
    uint8_t wpm;
};

static void wpm_update_cb(struct amp_wpm_state state) {
    st.wpm = state.wpm;
    kick_anim();
}

static struct amp_wpm_state wpm_get_state(const zmk_event_t *eh) {
    return (struct amp_wpm_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(amp_wpm, struct amp_wpm_state, wpm_update_cb, wpm_get_state)
ZMK_SUBSCRIPTION(amp_wpm, zmk_wpm_state_changed);

/* Idle timeout: blank the screen and stop animating */

struct amp_activity_state {
    bool idle;
};

static void activity_update_cb(struct amp_activity_state state) {
    st.idle = state.idle;
    if (st.idle && anim != NULL) {
        lv_timer_pause(anim);
    }
    draw();
    kick_anim();
}

static struct amp_activity_state activity_get_state(const zmk_event_t *eh) {
    return (struct amp_activity_state){.idle = zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE};
}

ZMK_DISPLAY_WIDGET_LISTENER(amp_activity, struct amp_activity_state, activity_update_cb,
                            activity_get_state)
ZMK_SUBSCRIPTION(amp_activity, zmk_activity_state_changed);

void nv_lily_screen_init(lv_obj_t *parent) {
    nv_init(parent);

    anim = lv_timer_create(anim_cb, FRAME_MS, NULL);
    lv_timer_pause(anim);

    amp_battery_init();
    amp_output_init();
    amp_layer_init();
    amp_wpm_init();
    amp_activity_init();

    draw();
}
