/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The loader. See jk_loader.h. */
#include "jk_loader.h"
#include "jk_art.h"
#include "jk_strings.h"

void jk_loader_draw(JkFrame *f, uint64_t t_ms)
{
    if (!f || !f->pixels) return;
    jk_background(f);
    jk_streaks(f, t_ms);
    int w = jk_text_width(JK_FONT_HERO, "JK4SPEED");
    jk_wordmark(f, (JK_W - w) / 2, 200, JK_FONT_HERO);
    /* in the dark sky, on a soft plate: readable over any glow (TV review) */
    int sw = jk_text_width(JK_FONT_HEAD, JK_S_L_STARTING), lh = jk_font_line_height(JK_FONT_HEAD);
    jk_fill_alpha(f, (JK_W - sw) / 2 - 40, 410 - 12, sw + 80, lh + 24, JK_RGB_INK, 170);
    jk_text(f, JK_FONT_HEAD, (JK_W - sw) / 2, 410, JK_S_L_STARTING, JK_RGB_CHALK);
}

void jk_loader_release(void)
{
    jk_art_release();
}
