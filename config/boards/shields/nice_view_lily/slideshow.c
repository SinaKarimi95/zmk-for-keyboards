/*
 * Right (peripheral) screen: Hammerbeam art slideshow.
 *
 *   header    link to the left half (tick or cross) and battery, laid out
 *             like the left screen's header
 *   art       Hammerbeam's 30 1-bit pictures (art.c, from GPeye's
 *             hammerbeam-slideshow, MIT, see LICENSE-hammerbeam-slideshow),
 *             changing every CONFIG_NICE_VIEW_LILY_SLIDE_MS with a "rain drip"
 *             transition: each column reveals the next picture top-down,
 *             starting at a random delay, led by a falling drop
 *
 * Blanks itself when ZMK goes idle (the ls0xx driver can't blank the panel).
 * Listener pattern follows ZMK's nice_view shield (MIT).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "fb.h"
#include "lily.h"

LV_IMG_DECLARE(hammerbeam1);
LV_IMG_DECLARE(hammerbeam2);
LV_IMG_DECLARE(hammerbeam3);
LV_IMG_DECLARE(hammerbeam4);
LV_IMG_DECLARE(hammerbeam5);
LV_IMG_DECLARE(hammerbeam6);
LV_IMG_DECLARE(hammerbeam7);
LV_IMG_DECLARE(hammerbeam8);
LV_IMG_DECLARE(hammerbeam9);
LV_IMG_DECLARE(hammerbeam10);
LV_IMG_DECLARE(hammerbeam11);
LV_IMG_DECLARE(hammerbeam12);
LV_IMG_DECLARE(hammerbeam13);
LV_IMG_DECLARE(hammerbeam14);
LV_IMG_DECLARE(hammerbeam15);
LV_IMG_DECLARE(hammerbeam16);
LV_IMG_DECLARE(hammerbeam17);
LV_IMG_DECLARE(hammerbeam18);
LV_IMG_DECLARE(hammerbeam19);
LV_IMG_DECLARE(hammerbeam20);
LV_IMG_DECLARE(hammerbeam21);
LV_IMG_DECLARE(hammerbeam22);
LV_IMG_DECLARE(hammerbeam23);
LV_IMG_DECLARE(hammerbeam24);
LV_IMG_DECLARE(hammerbeam25);
LV_IMG_DECLARE(hammerbeam26);
LV_IMG_DECLARE(hammerbeam27);
LV_IMG_DECLARE(hammerbeam28);
LV_IMG_DECLARE(hammerbeam29);
LV_IMG_DECLARE(hammerbeam30);

static const lv_img_dsc_t *const art[] = {
    &hammerbeam1,  &hammerbeam2,  &hammerbeam3,  &hammerbeam4,  &hammerbeam5,
    &hammerbeam6,  &hammerbeam7,  &hammerbeam8,  &hammerbeam9,  &hammerbeam10,
    &hammerbeam11, &hammerbeam12, &hammerbeam13, &hammerbeam14, &hammerbeam15,
    &hammerbeam16, &hammerbeam17, &hammerbeam18, &hammerbeam19, &hammerbeam20,
    &hammerbeam21, &hammerbeam22, &hammerbeam23, &hammerbeam24, &hammerbeam25,
    &hammerbeam26, &hammerbeam27, &hammerbeam28, &hammerbeam29, &hammerbeam30,
};

/* Each picture is 140x68 LV_IMG_CF_INDEXED_1BIT: an 8-byte palette, then
 * 68 rows of 18 bytes, MSB first; bit set = palette 1 = white = paper. */
#define ART_W 140
#define ART_H 68
#define ART_STRIDE 18
#define ART_PALETTE_BYTES 8
#define ART_TOP 16

#define FRAME_MS 50
#define DRIP_MAX_DELAY 0.5f

static struct {
    uint8_t battery;
    bool charging;
    bool connected;
    bool idle;
    uint8_t cur;
    uint8_t next;
    bool transition;
    uint16_t t_ms;
    float delay[ART_H];
} st;

static lv_timer_t *slide_timer;
static uint32_t rng_state;

static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static inline bool art_ink(uint8_t k, int row, int col) {
    const uint8_t *bits = art[k]->data + ART_PALETTE_BYTES;
    return !((bits[row * ART_STRIDE + (col >> 3)] >> (7 - (col & 7))) & 1);
}

static void draw_header(void) {
    if (st.connected) {
        nv_check(3, 1);
    } else {
        nv_cross(4, 2);
    }
    nv_battery(49, 2, st.battery, st.charging);
    nv_hline(12);
}

static void draw_art(void) {
    float p = 0.0f;
    if (st.transition) {
        p = (float)st.t_ms / CONFIG_NICE_VIEW_LILY_TRANSITION_MS;
        if (p > 1.0f) {
            p = 1.0f;
        }
    }

    /* Portrait column x is picture row x; portrait row y is picture column 139 - y */
    for (int x = 0; x < ART_H; x++) {
        int depth = -1;
        if (st.transition) {
            float d = st.delay[x];
            float u = (p - d) / (1.0f - d);
            if (u < 0.0f) {
                u = 0.0f;
            } else if (u > 1.0f) {
                u = 1.0f;
            }
            depth = (int)(u * (ART_W + 1));
        }

        for (int y = 0; y < ART_W; y++) {
            uint8_t k = (st.transition && y < depth) ? st.next : st.cur;
            bool ink = art_ink(k, x, ART_W - 1 - y);
            if (st.transition && depth > 0 && depth <= ART_W && (y == depth || y == depth + 3)) {
                ink = true;
            }
            if (ink) {
                nv_px(x, ART_TOP + y, true);
            }
        }
    }
}

static void draw(void) {
    nv_clear();
    if (!st.idle) {
        draw_header();
        draw_art();
    }
    nv_flush();
}

static void start_transition(void) {
    st.next = (st.cur + 1) % ARRAY_SIZE(art);
    st.transition = true;
    st.t_ms = 0;
    for (int x = 0; x < ART_H; x++) {
        st.delay[x] = (rng() % 1000) / 1000.0f * DRIP_MAX_DELAY;
    }
}

static void end_transition(void) {
    if (st.transition) {
        st.cur = st.next;
        st.transition = false;
    }
}

static void slide_cb(lv_timer_t *timer) {
    if (st.idle) {
        return;
    }

    if (!st.transition) {
        start_transition();
        lv_timer_set_period(timer, FRAME_MS);
    } else {
        st.t_ms += FRAME_MS;
        if (st.t_ms >= CONFIG_NICE_VIEW_LILY_TRANSITION_MS) {
            end_transition();
            lv_timer_set_period(timer, CONFIG_NICE_VIEW_LILY_SLIDE_MS);
        }
    }

    draw();
}

/* Battery */

struct ss_battery_state {
    uint8_t level;
    bool usb_present;
};

static void battery_update_cb(struct ss_battery_state state) {
    st.battery = state.level;
    st.charging = state.usb_present;
    draw();
}

static struct ss_battery_state battery_get_state(const zmk_event_t *eh) {
    return (struct ss_battery_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(ss_battery, struct ss_battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(ss_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(ss_battery, zmk_usb_conn_state_changed);
#endif

/* Link to the left half */

struct ss_link_state {
    bool connected;
};

static void link_update_cb(struct ss_link_state state) {
    st.connected = state.connected;
    draw();
}

static struct ss_link_state link_get_state(const zmk_event_t *eh) {
    return (struct ss_link_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(ss_link, struct ss_link_state, link_update_cb, link_get_state)
ZMK_SUBSCRIPTION(ss_link, zmk_split_peripheral_status_changed);

/* Idle timeout: blank the screen and pause the slideshow */

struct ss_activity_state {
    bool idle;
};

static void activity_update_cb(struct ss_activity_state state) {
    bool was_idle = st.idle;
    st.idle = state.idle;

    if (slide_timer != NULL) {
        if (st.idle) {
            lv_timer_pause(slide_timer);
        } else if (was_idle) {
            end_transition();
            lv_timer_set_period(slide_timer, CONFIG_NICE_VIEW_LILY_SLIDE_MS);
            lv_timer_reset(slide_timer);
            lv_timer_resume(slide_timer);
        }
    }
    draw();
}

static struct ss_activity_state activity_get_state(const zmk_event_t *eh) {
    return (struct ss_activity_state){.idle = zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE};
}

ZMK_DISPLAY_WIDGET_LISTENER(ss_activity, struct ss_activity_state, activity_update_cb,
                            activity_get_state)
ZMK_SUBSCRIPTION(ss_activity, zmk_activity_state_changed);

void nv_lily_screen_init(lv_obj_t *parent) {
    nv_init(parent);

    rng_state = k_cycle_get_32() | 1;
    st.cur = rng() % ARRAY_SIZE(art);

    slide_timer = lv_timer_create(slide_cb, CONFIG_NICE_VIEW_LILY_SLIDE_MS, NULL);

    ss_battery_init();
    ss_link_init();
    ss_activity_init();

    draw();
}
