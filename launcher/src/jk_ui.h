/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Jk4Speed launcher software renderer (design spec section 6).
 *
 * Draws into a CPU frame in BGRX byte order (one uint32_t per pixel, value
 * 0x00RRGGBB on little-endian). Every function clips to the frame, so callers
 * may pass rectangles that are partly or fully off screen. Colours are
 * 0xRRGGBB, alpha 0..255. Deterministic: no sin/cos/pow, so host goldens match
 * exactly.
 */
#ifndef JK_UI_H
#define JK_UI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct JkFrame {
    uint8_t *pixels;       /* first byte of row 0 */
    uint32_t width, height;
    uint32_t stride;       /* bytes per row, >= width * 4 */
} JkFrame;

enum { JK_W = 1920, JK_H = 1080 };

/* Title-safe area (90 %): nothing but the background is drawn outside it. */
enum { JK_SAFE_X0 = 96, JK_SAFE_Y0 = 54, JK_SAFE_X1 = 1824, JK_SAFE_Y1 = 1026 };

/* Palette, spec 6.2. */
enum {
    JK_RGB_INK        = 0x0C0E13,
    JK_RGB_ASPHALT    = 0x1A1D24,
    JK_RGB_CHALK      = 0xF2EEE6,
    JK_RGB_SMOKE      = 0xB8BEC8,   /* 7:1 or better on panels (TV review) */
    JK_RGB_DUSK       = 0xFF9A3C,
    JK_RGB_SIREN_RED  = 0xFF2B3A,   /* bars, dots, large headings */
    JK_RGB_RED_TEXT   = 0xFF6B73,   /* red body text: readable on a TV */
    JK_RGB_SIREN_BLUE = 0x2F6BFF,
    JK_RGB_OK         = 0x58D68D
};

typedef enum JkFont {
    JK_FONT_HINT = 0,  /* 30 px, Condensed SemiBold */
    JK_FONT_BODY,      /* 36 px, Condensed SemiBold */
    JK_FONT_HEAD,      /* 48 px, Condensed ExtraBold Italic */
    JK_FONT_MENU,      /* 72 px, Condensed ExtraBold Italic */
    JK_FONT_HERO,      /* 128 px, Condensed ExtraBold Italic, A-Z 0-9 . space only */
    JK_FONT_COUNT
} JkFont;

/* Slant of plates and of the italic faces: tan(12 deg). */
#define JK_SLANT 0.21256f

void jk_fill(JkFrame *f, int x, int y, int w, int h, uint32_t rgb);
void jk_fill_alpha(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha);
/* Additive: adds rgb scaled by alpha, saturating. */
void jk_fill_add(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha);
void jk_gradient_v(JkFrame *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom);

/* A parallelogram leaning right like italic type: row r of h has its left
 * edge at x + (h - 1 - r) * JK_SLANT, width w, anti-aliased sides. */
void jk_plate(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha);

/* Text. (x, y) is the top-left of the line box; the baseline is at
 * y + ascent. Returns the advance in pixels (also for clipped text). Invalid
 * UTF-8 bytes and code points without a glyph are skipped. f may be NULL. */
int jk_text(JkFrame *f, JkFont font, int x, int y, const char *utf8, uint32_t rgb);
int jk_text_alpha(JkFrame *f, JkFont font, int x, int y, const char *utf8, uint32_t rgb, int alpha);
int jk_text_width(JkFont font, const char *utf8);
int jk_font_line_height(JkFont font);
int jk_font_ascent(JkFont font);

/* Blend one pixel (clipped). */
void jk_put(JkFrame *f, int x, int y, uint32_t rgb, int alpha);

#ifdef __cplusplus
}
#endif

#endif
