/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Writes PNG previews of the art primitives to build/preview/ (by eye only).
 * Run: launcher/tests/run.sh preview_ui */
#include "jk_art.h"
#include "jk_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void)
{
    JkFrame f = { calloc((size_t)JK_W * JK_H, 4), JK_W, JK_H, JK_W * 4 };
    mkdir(JK_ROOT_DIR "/build/preview", 0755);
    jk_background(&f);
    jk_png_write(JK_ROOT_DIR "/build/preview/art-background.png", &f);

    jk_wordmark(&f, 120, 80, JK_FONT_HEAD);
    int y = 240;
    jk_text(&f, JK_FONT_MENU, 140, y, "START", JK_RGB_CHALK); y += 100;
    jk_text(&f, JK_FONT_MENU, 140, y, "PATCH", JK_RGB_CHALK); y += 100;
    jk_text(&f, JK_FONT_HEAD, 140, y, "READY TO INSTALL", JK_RGB_DUSK); y += 70;
    jk_text(&f, JK_FONT_BODY, 140, y, "Installs the Jk4Speed files. Your game files are not changed.", JK_RGB_CHALK); y += 60;
    jk_text(&f, JK_FONT_HINT, 140, y, "Select \xE2\x80\xA2 Back \xE2\x80\xB9 Enhanced \xE2\x80\xBA", JK_RGB_SMOKE); y += 60;
    for (int b = 0; b < JK_BTN_COUNT; b++) jk_button_glyph(&f, 140 + b * 60, y, 44, (JkButton)b);
    jk_png_write(JK_ROOT_DIR "/build/preview/art-type.png", &f);

    jk_background(&f);
    jk_wordmark(&f, 600, 400, JK_FONT_HERO);
    jk_streaks(&f, 1200);
    jk_png_write(JK_ROOT_DIR "/build/preview/art-loader.png", &f);
    free(f.pixels);
    jk_art_release();
    puts("previews in build/preview");
    return 0;
}
