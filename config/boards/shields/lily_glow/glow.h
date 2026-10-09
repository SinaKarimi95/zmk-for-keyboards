/*
 * Underglow colour/effect, separate from the key lights, plus the
 * "lights allowed" rule: always on when plugged in, off when idle on battery.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

/* &glow param1 values; keep in sync with the #defines in config/lily58.keymap */
#define GLOW_CMD_NEXT 0      /* next colour (relative, converted on the central) */
#define GLOW_CMD_PREV 1      /* previous colour (relative) */
#define GLOW_CMD_FX_TOGGLE 2 /* breathe <-> solid (relative) */
#define GLOW_CMD_SET_COLOR 3 /* param2 = colour index */
#define GLOW_CMD_SET_FX 4    /* param2 = GLOW_FX_* */
#define GLOW_CMD_LIGHTS 5    /* param2 = 1 if lights are allowed; central -> peripheral */

#define GLOW_FX_SOLID 0
#define GLOW_FX_BREATHE 1

#define GLOW_CHANGE_COLOR 0
#define GLOW_CHANGE_FX 1

uint8_t lily_glow_color_count(void);
const char *lily_glow_color_name(uint8_t index);
uint8_t lily_glow_color(void);
uint8_t lily_glow_fx(void);

/* Bumped on every colour/effect change made with a key; the left screen
 * watches it to show what changed */
uint32_t lily_glow_serial(void);
uint8_t lily_glow_last_change(void);

/* Nearest colour name for a ZMK hue (0-359) and saturation (0-100) */
const char *lily_color_name(uint16_t hue, uint8_t sat);

/* False while the keyboard is idle on battery */
bool lily_glow_lights_allowed(void);

/* Current underglow pixel (already scaled by ZMK's brightness) */
struct led_rgb lily_glow_pixel(void);
