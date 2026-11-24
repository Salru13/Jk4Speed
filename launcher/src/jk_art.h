/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Jk4Speed's own art, drawn in code (spec 6.1, 6.4): a harbour city at dusk
 * with police-light glows and grain, the JK4SPEED wordmark, pad button glyphs
 * and the loader's traffic streaks. No image files; deterministic.
 */
#ifndef JK_ART_H
#define JK_ART_H

#include "jk_ui.h"

typedef enum JkButton {
    JK_BTN_CROSS = 0, JK_BTN_CIRCLE, JK_BTN_SQUARE, JK_BTN_TRIANGLE,
    JK_BTN_UPDOWN, JK_BTN_LEFTRIGHT, JK_BTN_COUNT
} JkButton;

/* The whole-frame background. Built once into a cache (JK_W x JK_H x 4 bytes),
 * then copied; a smaller frame gets its top-left part. */
void jk_background(JkFrame *f);
/* Frees the cache. */
void jk_art_release(void);
size_t jk_art_cache_bytes(void);

/* "JK4SPEED" with the 4 in DUSK and a red|blue siren bar under it. (x, y) is
 * the top-left of the line box. Returns the width of the text. */
int jk_wordmark(JkFrame *f, int x, int y, JkFont font);

/* A round key cap (CHALK) of size x size pixels with the button's symbol (INK). */
void jk_button_glyph(JkFrame *f, int x, int y, int size, JkButton b);

typedef enum JkIcon { JK_ICON_OK = 0, JK_ICON_BAD, JK_ICON_TODO, JK_ICON_PENDING, JK_ICON_ACTIVE, JK_ICON_COUNT } JkIcon;
/* Checklist marks: tick (OK green), cross (red), filled slanted square
 * (DUSK: to do), small ring (SMOKE: not reached), ring with dot (DUSK: now). */
void jk_status_icon(JkFrame *f, int x, int y, int size, JkIcon icon);

/* The police accent: a slanted bar, left half red and right half blue;
 * phase 0..256 cross-fades the two halves (0: red|blue, 256: blue|red). */
void jk_siren_bar(JkFrame *f, int x, int y, int w, int h, int phase);

/* Moving traffic light streaks on the highway (loader). Loops every 3000 ms. */
void jk_streaks(JkFrame *f, uint64_t t_ms);

/* Soft additive elliptical glow. */
void jk_glow(JkFrame *f, int cx, int cy, int rx, int ry, uint32_t rgb, int alpha);

#endif
