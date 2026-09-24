/*
 * 1-bit drawing helpers for the nice!view, in portrait coordinates:
 * x 0..67 left to right, y 0..159 top to bottom, as the screen is mounted.
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define NV_W 68
#define NV_H 160
#define NV_PI 3.14159265f

void nv_init(lv_obj_t *parent);
void nv_clear(void);
void nv_flush(void);

void nv_px(int x, int y, bool on);
void nv_rect(int x, int y, int w, int h, bool on);
void nv_box(int x, int y, int w, int h);
void nv_hline(int y);
void nv_line(int x0, int y0, int x1, int y1);

int nv_text_width(const char *s);
void nv_text(const char *s, int x, int y, bool on);

void nv_battery(int x, int y, uint8_t pct, bool charging);
void nv_check(int x, int y);
void nv_cross(int x, int y);

int nv_round(float f);
float nv_sin(float rad);
float nv_cos(float rad);
