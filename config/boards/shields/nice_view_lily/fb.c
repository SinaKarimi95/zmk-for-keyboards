/*
 * 1-bit drawing helpers for the nice!view.
 *
 * The panel is 160x68 landscape; it is mounted portrait with the status end
 * (native x = 159) at the top, the same orientation ZMK's nice_view shield
 * uses. Portrait (x, y) maps to native (159 - y, x).
 */

#include <string.h>

#include "fb.h"

#define NATIVE_W 160
#define NATIVE_H 68

static lv_obj_t *canvas;
static lv_color_t cbuf[NATIVE_W * NATIVE_H];
static lv_color_t ink;
static lv_color_t paper;

/* Classic 5x7 font, ASCII 32..95, one byte per column, bit 0 = top row */
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /*   */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* [ */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* \ */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* ] */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
};

/* 5x7 lightning bolt, one byte per row, bit 4 = leftmost column */
static const uint8_t bolt5x7[7] = {0x03, 0x06, 0x0C, 0x1F, 0x06, 0x0C, 0x18};

void nv_init(lv_obj_t *parent) {
    ink = lv_color_black();
    paper = lv_color_white();

    canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, cbuf, NATIVE_W, NATIVE_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    nv_clear();
}

void nv_clear(void) {
    for (int i = 0; i < NATIVE_W * NATIVE_H; i++) {
        cbuf[i] = paper;
    }
}

void nv_flush(void) { lv_obj_invalidate(canvas); }

void nv_px(int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= NV_W || y >= NV_H) {
        return;
    }
    cbuf[x * NATIVE_W + (NATIVE_W - 1 - y)] = on ? ink : paper;
}

void nv_rect(int x, int y, int w, int h, bool on) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            nv_px(i, j, on);
        }
    }
}

void nv_box(int x, int y, int w, int h) {
    for (int i = x; i < x + w; i++) {
        nv_px(i, y, true);
        nv_px(i, y + h - 1, true);
    }
    for (int j = y; j < y + h; j++) {
        nv_px(x, j, true);
        nv_px(x + w - 1, j, true);
    }
}

void nv_hline(int y) {
    for (int x = 0; x < NV_W; x++) {
        nv_px(x, y, true);
    }
}

void nv_line(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        nv_px(x0, y0, true);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int nv_text_width(const char *s) {
    int n = strlen(s);
    return n > 0 ? n * 6 - 1 : 0;
}

void nv_text(const char *s, int x, int y, bool on) {
    for (; *s; s++, x += 6) {
        char c = *s;
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
        if (c < ' ' || c > '_') {
            c = '?';
        }
        const uint8_t *g = font5x7[c - ' '];
        for (int i = 0; i < 5; i++) {
            for (int b = 0; b < 7; b++) {
                if (g[i] & (1 << b)) {
                    nv_px(x + i, y + b, on);
                }
            }
        }
    }
}

void nv_battery(int x, int y, uint8_t pct, bool charging) {
    if (pct > 100) {
        pct = 100;
    }
    nv_box(x, y, 15, 7);
    nv_rect(x + 15, y + 2, 2, 3, true);
    nv_rect(x + 2, y + 2, (11 * pct + 50) / 100, 3, true);

    if (charging) {
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 5; c++) {
                if (bolt5x7[r] & (0x10 >> c)) {
                    nv_px(x - 8 + c, y + r, true);
                }
            }
        }
    }
}

void nv_check(int x, int y) {
    static const int8_t pts[8][2] = {{0, 3}, {1, 4}, {2, 5}, {3, 4},
                                     {4, 3}, {5, 2}, {6, 1}, {7, 0}};
    for (int i = 0; i < 8; i++) {
        nv_px(x + pts[i][0], y + pts[i][1] + 1, true);
        nv_px(x + pts[i][0], y + pts[i][1] + 2, true);
    }
}

void nv_cross(int x, int y) {
    for (int i = 0; i < 6; i++) {
        nv_px(x + i, y + i, true);
        nv_px(x + 5 - i, y + i, true);
    }
}

int nv_round(float f) { return (int)(f < 0 ? f - 0.5f : f + 0.5f); }

/* Bhaskara I approximation, good to ~0.2%, avoids pulling in libm */
float nv_sin(float a) {
    while (a > NV_PI) {
        a -= 2 * NV_PI;
    }
    while (a < -NV_PI) {
        a += 2 * NV_PI;
    }
    bool neg = a < 0;
    if (neg) {
        a = -a;
    }
    float p = a * (NV_PI - a);
    float s = 16.0f * p / (5.0f * NV_PI * NV_PI - 4.0f * p);
    return neg ? -s : s;
}

float nv_cos(float a) { return nv_sin(a + NV_PI / 2); }
