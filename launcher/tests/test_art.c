/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Original art: dusk city background, wordmark, pad glyphs, traffic (spec 6). */
#include "jk_test.h"
#include "jk_art.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static JkFrame frame_new(void)
{
    JkFrame f = { calloc((size_t)JK_W * JK_H, 4), JK_W, JK_H, JK_W * 4 };
    return f;
}
static uint32_t px(const JkFrame *f, int x, int y) { return ((const uint32_t *)(const void *)f->pixels)[(size_t)y * f->width + x]; }
static int lum(uint32_t c) { return (int)(((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) / 10; }

static void background_deterministic(void)
{
    JkFrame a = frame_new(), b = frame_new();
    jk_background(&a);
    jk_art_release();                          /* rebuilt from scratch */
    jk_background(&b);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) == 0);
    free(a.pixels); free(b.pixels);
}

/* Sky dark at the top, warm (amber) near the horizon. */
static void sky_dark_top_amber_horizon(void)
{
    JkFrame f = frame_new();
    jk_background(&f);
    uint32_t top = px(&f, 960, 20);
    CHECK(lum(top) < 40);
    int warm = 0;
    /* right of the menu scrim: the dusk horizon shows between buildings */
    for (int y = 300; y < 700; y += 4)
        for (int x = 1200; x < 1880; x += 8) {
            uint32_t c = px(&f, x, y);
            if (((c >> 16) & 255) > (c & 255) + 40 && lum(c) > 60) warm = 1;
        }
    CHECK(warm);
    /* the bottom of the frame is dark asphalt */
    CHECK(lum(px(&f, 960, 1070)) < 40);
    free(f.pixels);
}

/* The background never draws over a frame larger than itself: a smaller
 * frame is clipped, not overrun (ASan checks the bounds). */
static void background_small_frame(void)
{
    JkFrame f = { calloc(64 * 32, 4), 64, 32, 64 * 4 };
    jk_background(&f);
    CHECK(px(&f, 0, 0) != 0 || px(&f, 63, 31) != 0);
    free(f.pixels);
}

static void wordmark_four_is_dusk(void)
{
    JkFrame f = frame_new();
    memset(f.pixels, 0, (size_t)JK_W * JK_H * 4);
    int w = jk_wordmark(&f, 100, 100, JK_FONT_HEAD);
    CHECK(w > 150);
    int dusk = 0, chalk = 0;
    for (int y = 100; y < 180; y++)
        for (int x = 100; x < 100 + w + 20; x++) {
            uint32_t c = px(&f, x, y);
            if (c == JK_RGB_DUSK) dusk++;
            if (c == JK_RGB_CHALK) chalk++;
        }
    CHECK(dusk > 20);
    CHECK(chalk > dusk);
    free(f.pixels);
}

static void glyph_shapes_differ(void)
{
    JkFrame f = frame_new();
    uint32_t sums[JK_BTN_COUNT];
    for (int b = 0; b < JK_BTN_COUNT; b++) {
        memset(f.pixels, 0, (size_t)JK_W * JK_H * 4);
        jk_button_glyph(&f, 10, 10, 44, (JkButton)b);
        uint32_t s = 0;
        for (int y = 0; y < 70; y++) for (int x = 0; x < 70; x++) s = s * 31u + px(&f, x, y);
        sums[b] = s;
        CHECK(px(&f, 32, 32) != 0 || px(&f, 20, 32) != 0);   /* something is drawn */
    }
    for (int i = 0; i < JK_BTN_COUNT; i++)
        for (int j = i + 1; j < JK_BTN_COUNT; j++) CHECK(sums[i] != sums[j]);
    free(f.pixels);
}

/* Traffic streaks loop every 3 s. */
static void streaks_loop_3s(void)
{
    JkFrame a = frame_new(), b = frame_new(), c = frame_new();
    jk_background(&a); jk_background(&b); jk_background(&c);
    jk_streaks(&a, 0);
    jk_streaks(&b, 500);
    jk_streaks(&c, 3000);
    size_t n = (size_t)JK_W * JK_H * 4;
    CHECK(memcmp(a.pixels, b.pixels, n) != 0);
    CHECK(memcmp(a.pixels, c.pixels, n) == 0);
    free(a.pixels); free(b.pixels); free(c.pixels);
}

static void status_icons_coloured(void)
{
    JkFrame f = frame_new();
    static const uint32_t want[JK_ICON_COUNT] = { JK_RGB_OK, JK_RGB_SIREN_RED, JK_RGB_DUSK, JK_RGB_SMOKE, JK_RGB_DUSK };
    for (int i = 0; i < JK_ICON_COUNT; i++) {
        memset(f.pixels, 0, (size_t)JK_W * JK_H * 4);
        jk_status_icon(&f, 10, 10, 40, (JkIcon)i);
        int hits = 0;
        for (int y = 10; y < 50; y++) for (int x = 10; x < 50; x++) hits += px(&f, x, y) == want[i];
        CHECK(hits > 8);
    }
    free(f.pixels);
}

int main(void)
{
    RUN(status_icons_coloured);
    RUN(background_deterministic);
    RUN(sky_dark_top_amber_horizon);
    RUN(background_small_frame);
    RUN(wordmark_four_is_dusk);
    RUN(glyph_shapes_differ);
    RUN(streaks_loop_3s);
    jk_art_release();
    return jk_test_report("test_art");
}
