/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Jk4Speed launcher software renderer. See jk_ui.h. Pixel and text code
 * adapted from the JkFlow launcher (bl_ui.c). */
#include "jk_ui.h"
#include "jk_font.h"

#include <stdlib.h>
#include <string.h>

static inline uint32_t *row_ptr(const JkFrame *f, int y)
{
    return (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
}

/* Blend s over d with alpha a (0..255). */
static inline uint32_t mix(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    if (a >= 255) return s & 0xFFFFFF;
    uint32_t r = (((d >> 16) & 255) * (unsigned)(255 - a) + ((s >> 16) & 255) * (unsigned)a + 127) / 255;
    uint32_t g = (((d >> 8) & 255) * (unsigned)(255 - a) + ((s >> 8) & 255) * (unsigned)a + 127) / 255;
    uint32_t b = ((d & 255) * (unsigned)(255 - a) + (s & 255) * (unsigned)a + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

/* Add s scaled by a (0..255) to d, saturating. */
static inline uint32_t add(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    unsigned r = ((d >> 16) & 255) + (((s >> 16) & 255) * (unsigned)a + 127) / 255;
    unsigned g = ((d >> 8) & 255) + (((s >> 8) & 255) * (unsigned)a + 127) / 255;
    unsigned b = (d & 255) + ((s & 255) * (unsigned)a + 127) / 255;
    return ((r > 255 ? 255u : r) << 16) | ((g > 255 ? 255u : g) << 8) | (b > 255 ? 255u : b);
}

/* Clip [x, x+w) x [y, y+h) to the frame. Returns 0 when empty. */
static int clip(const JkFrame *f, long long x, long long y, long long w, long long h,
                int *x0, int *y0, int *x1, int *y1)
{
    if (!f || !f->pixels || w <= 0 || h <= 0) return 0;
    long long ax = x < 0 ? 0 : x, ay = y < 0 ? 0 : y;
    long long bx = x + w > (long long)f->width ? (long long)f->width : x + w;
    long long by = y + h > (long long)f->height ? (long long)f->height : y + h;
    if (ax >= bx || ay >= by) return 0;
    *x0 = (int)ax; *y0 = (int)ay; *x1 = (int)bx; *y1 = (int)by;
    return 1;
}

void jk_put(JkFrame *f, int x, int y, uint32_t rgb, int alpha)
{
    if (!f || !f->pixels || x < 0 || y < 0 || x >= (int)f->width || y >= (int)f->height) return;
    uint32_t *r = row_ptr(f, y);
    r[x] = mix(r[x], rgb, alpha);
}

void jk_fill(JkFrame *f, int x, int y, int w, int h, uint32_t rgb)
{
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = rgb & 0xFFFFFF;
    }
}

void jk_fill_alpha(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    int x0, y0, x1, y1;
    if (alpha <= 0 || !clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = mix(r[xx], rgb, alpha);
    }
}

void jk_fill_add(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    int x0, y0, x1, y1;
    if (alpha <= 0 || !clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = add(r[xx], rgb, alpha);
    }
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, int t, int n)
{
    if (n <= 0) return a;
    int ra = (int)((a >> 16) & 255), ga = (int)((a >> 8) & 255), ba = (int)(a & 255);
    int rb = (int)((b >> 16) & 255), gb = (int)((b >> 8) & 255), bb = (int)(b & 255);
    int r = ra + ((rb - ra) * t + (rb >= ra ? n / 2 : -n / 2)) / n;
    int g = ga + ((gb - ga) * t + (gb >= ga ? n / 2 : -n / 2)) / n;
    int bl = ba + ((bb - ba) * t + (bb >= ba ? n / 2 : -n / 2)) / n;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void jk_gradient_v(JkFrame *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t c = lerp_rgb(top, bottom, yy - y, h - 1);
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = c;
    }
}

void jk_plate(JkFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    if (!f || !f->pixels || w <= 0 || h <= 0 || alpha <= 0) return;
    long long lean = (long long)((float)(h - 1) * JK_SLANT) + 2;
    int x0, y0, x1, y1;
    if (!clip(f, x, y, (long long)w + lean, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        float xl = (float)x + (float)(h - 1 - (yy - y)) * JK_SLANT;
        float xr = xl + (float)w;
        int a0 = (int)xl, a1 = (int)xr;
        uint32_t *r = row_ptr(f, yy);
        for (int xx = a0 < x0 ? x0 : a0; xx <= a1 && xx < x1; xx++) {
            float cov = 1.0f;
            float lc = (float)(xx + 1) - xl, rc = xr - (float)xx;
            if (lc < cov) cov = lc;
            if (rc < cov) cov = rc;
            if (cov <= 0.0f) continue;
            r[xx] = mix(r[xx], rgb, (int)(cov * (float)alpha + 0.5f));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

/* Decode one UTF-8 code point. On an invalid or truncated sequence, consume
 * one byte and return -1. */
static long next_cp(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned c = s[0];
    if (c < 0x80) { *p = s + 1; return (long)c; }
    int n; unsigned cp, min;
    if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; min = 0x10000; }
    else { *p = s + 1; return -1; }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p = s + 1; return -1; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { *p = s + 1; return -1; }
    *p = s + 1 + n;
    return (long)cp;
}

static const JkFontData *font_of(JkFont font)
{
    if ((int)font < 0 || (int)font >= JK_FONT_DATA_COUNT) return &jk_font_data[JK_FONT_BODY];
    return &jk_font_data[font];
}

static const JkGlyph *glyph_of(const JkFontData *fd, long cp)
{
    int lo = 0, hi = (int)fd->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((long)fd->glyphs[mid].cp == cp) return &fd->glyphs[mid];
        if ((long)fd->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int jk_font_line_height(JkFont font) { return font_of(font)->line_h; }
int jk_font_ascent(JkFont font) { return font_of(font)->ascent; }

int jk_text_width(JkFont font, const char *utf8)
{
    return jk_text(NULL, font, 0, 0, utf8, 0);
}

int jk_text_alpha(JkFrame *f, JkFont font, int x, int y, const char *utf8, uint32_t rgb, int alpha)
{
    if (!utf8) return 0;
    const JkFontData *fd = font_of(font);
    const unsigned char *p = (const unsigned char *)utf8;
    long long pen = x;
    while (*p) {
        long cp = next_cp(&p);
        const JkGlyph *g = cp < 0 ? NULL : glyph_of(fd, cp);
        if (!g) continue;
        if (f && f->pixels && g->w && g->h && alpha > 0) {
            long long gx = pen + g->left, gy = (long long)y + fd->ascent - g->top;
            int x0, y0, x1, y1;
            if (clip(f, gx, gy, g->w, g->h, &x0, &y0, &x1, &y1)) {
                for (int yy = y0; yy < y1; yy++) {
                    uint32_t *row = row_ptr(f, yy);
                    const uint8_t *a = fd->alpha + g->off + (size_t)(yy - gy) * g->w + (size_t)(x0 - gx);
                    for (int xx = x0; xx < x1; xx++, a++)
                        if (*a) row[xx] = mix(row[xx], rgb, alpha >= 255 ? *a : (*a * alpha + 127) / 255);
                }
            }
        }
        pen += g->adv;
    }
    return (int)(pen - x);
}

int jk_text(JkFrame *f, JkFont font, int x, int y, const char *utf8, uint32_t rgb)
{
    return jk_text_alpha(f, font, x, y, utf8, rgb, 255);
}
