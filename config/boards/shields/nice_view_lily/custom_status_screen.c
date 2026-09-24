/*
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include "lily.h"

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Same structure as ZMK's nice_view shield: a 160x68 container at the origin */
    lv_obj_t *widget = lv_obj_create(screen);
    lv_obj_set_size(widget, 160, 68);
    lv_obj_align(widget, LV_ALIGN_TOP_LEFT, 0, 0);

    nv_lily_screen_init(widget);

    return screen;
}
