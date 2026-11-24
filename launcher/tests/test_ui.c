/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Renderer primitives: clipping, slanted plates, text metrics (spec 6, G2). */
#include "jk_test.h"
#include "jk_ui.h"

#include <stdint.h>
#include <stdlib.h>

static JkFrame frame_new(int w, int h)
{
    JkFrame f = { calloc((size_t)w * h, 4), (uint32_t)w, (uint32_t)h, (uint32_t)w * 4 };
    return f;
}
static uint32_t px(const JkFrame *f, int x, int y) { return ((const uint32_t *)(const void *)f->pixels)[(size_t)y * f->width + x]; }

static void fill_clips_offscreen(void)
{
    JkFrame f = frame_new(64, 32);
    jk_fill(&f, -10, -10, 20, 20, 0xFFFFFF);      /* partly off screen */
    jk_fill(&f, 100, 100, 50, 50, 0xFF0000);      /* fully off screen */
    jk_fill(&f, 60, 30, 1000, 1000, 0x00FF00);    /* runs past the right and bottom */
    CHECK_EQ(px(&f, 0, 0), 0xFFFFFF);
    CHECK_EQ(px(&f, 9, 9), 0xFFFFFF);
    CHECK_EQ(px(&f, 10, 10), 0);
    CHECK_EQ(px(&f, 63, 31), 0x00FF00);
    CHECK_EQ(px(&f, 59, 31), 0);
    free(f.pixels);
}

static void fill_alpha_blends(void)
{
    JkFrame f = frame_new(4, 4);
    jk_fill(&f, 0, 0, 4, 4, 0x000000);
    jk_fill_alpha(&f, 0, 0, 4, 4, 0xFFFFFF, 128);
    CHECK_EQ(px(&f, 1, 1), 0x808080);
    free(f.pixels);
}

/* The plate leans like italic type: its top edge sits further right than its
 * bottom edge by h * tan(12 deg) = 0.2126 h. */
static void plate_slants_right(void)
{
    JkFrame f = frame_new(400, 200);
    jk_plate(&f, 100, 0, 200, 200, 0xFFFFFF, 255);
    int top_first = -1, bottom_first = -1;
    for (int x = 0; x < 400; x++) if (top_first < 0 && px(&f, x, 0) == 0xFFFFFF) top_first = x;
    for (int x = 0; x < 400; x++) if (bottom_first < 0 && px(&f, x, 199) == 0xFFFFFF) bottom_first = x;
    CHECK(top_first > bottom_first);
    int lean = top_first - bottom_first;
    CHECK(lean >= 40 && lean <= 44);              /* 199 * 0.2126 = 42.3 */
    /* width stays 200 on every row */
    int n = 0;
    for (int x = 0; x < 400; x++) n += px(&f, x, 100) == 0xFFFFFF;
    CHECK(n >= 198 && n <= 200);
    free(f.pixels);
}

static void plate_clips(void)
{
    JkFrame f = frame_new(50, 50);
    jk_plate(&f, -100, -100, 400, 400, 0x123456, 200);
    jk_plate(&f, 1000, 1000, 10, 10, 0x123456, 200);
    CHECK(px(&f, 25, 25) != 0);
    free(f.pixels);
}

static void text_width_matches_advance(void)
{
    JkFrame f = frame_new(800, 120);
    int w = jk_text_width(JK_FONT_BODY, "Ready to play");
    int adv = jk_text(&f, JK_FONT_BODY, 10, 10, "Ready to play", 0xFFFFFF);
    CHECK(w > 0);
    CHECK_EQ(w, adv);
    CHECK_EQ(jk_text_width(JK_FONT_BODY, ""), 0);
    CHECK_EQ(jk_text_width(JK_FONT_BODY, NULL), 0);
    /* invalid UTF-8 is skipped, not fatal */
    CHECK(jk_text_width(JK_FONT_BODY, "a\xff" "b") == jk_text_width(JK_FONT_BODY, "ab"));
    free(f.pixels);
}

/* Height in pixels of the inked rows of one glyph drawn alone. */
static int ink_height(JkFont font, const char *s)
{
    JkFrame f = frame_new(400, 300);
    jk_text(&f, font, 20, 20, s, 0xFFFFFF);
    int top = -1, bot = -1;
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 400; x++)
            if ((px(&f, x, y) & 0xFF) > 128) { if (top < 0) top = y; bot = y; }
    free(f.pixels);
    return top < 0 ? 0 : bot - top + 1;
}

/* G2: menu items are at least 60 px type, so the cap height is >= 48 px at
 * 1080p; hints are 30 px type with a cap height >= 20 px. */
static void menu_cap_height_ge_48(void) { CHECK(ink_height(JK_FONT_MENU, "E") >= 48); }
static void hint_cap_height_ge_20(void) { CHECK(ink_height(JK_FONT_HINT, "E") >= 20); }
static void body_cap_height_ge_24(void) { CHECK(ink_height(JK_FONT_BODY, "E") >= 24); }

static void hero_has_caps_and_digits(void)
{
    CHECK(jk_text_width(JK_FONT_HERO, "JK4SPEED") > 400);
    CHECK(jk_font_line_height(JK_FONT_HERO) >= 128);
    CHECK(jk_font_ascent(JK_FONT_MENU) > 0);
}

int main(void)
{
    RUN(fill_clips_offscreen);
    RUN(fill_alpha_blends);
    RUN(plate_slants_right);
    RUN(plate_clips);
    RUN(text_width_matches_advance);
    RUN(menu_cap_height_ge_48);
    RUN(hint_cap_height_ge_20);
    RUN(body_cap_height_ge_24);
    RUN(hero_has_caps_and_digits);
    return jk_test_report("test_ui");
}
