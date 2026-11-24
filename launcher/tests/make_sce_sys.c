/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The package's sce_sys art, drawn with the launcher's own art (no EA art):
 * build/sce_sys/icon0.png (512x512) and build/sce_sys/pic1.png (1920x1080).
 * Run: launcher/tests/run.sh make_sce_sys (launcher/title/make_pkg.sh does). */
#include "jk_art.h"
#include "jk_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void)
{
    JkFrame f = { calloc((size_t)JK_W * JK_H, 4), JK_W, JK_H, JK_W * 4 };
    mkdir(JK_ROOT_DIR "/build", 0755);
    mkdir(JK_ROOT_DIR "/build/sce_sys", 0755);
    jk_background(&f);
    int rc = jk_png_write(JK_ROOT_DIR "/build/sce_sys/pic1.png", &f);

    /* icon: a 512x512 window on the skyline with the wordmark in its middle */
    const int x0 = 1300, y0 = 260, size = 512;
    jk_fill_alpha(&f, x0, y0, size, size, JK_RGB_INK, 110);
    int w = jk_text_width(JK_FONT_MENU, "JK4SPEED");
    jk_wordmark(&f, x0 + (size - w) / 2, y0 + (size - jk_font_line_height(JK_FONT_MENU)) / 2, JK_FONT_MENU);
    JkFrame icon = { f.pixels + (size_t)y0 * f.stride + (size_t)x0 * 4, (uint32_t)size, (uint32_t)size, f.stride };
    rc |= jk_png_write(JK_ROOT_DIR "/build/sce_sys/icon0.png", &icon);
    free(f.pixels);
    jk_art_release();
    printf("sce_sys art in build/sce_sys (rc=%d)\n", rc);
    return rc ? 1 : 0;
}
