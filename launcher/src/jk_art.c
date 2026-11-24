/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Jk4Speed's own art. See jk_art.h. Integer hash noise and plain arithmetic
 * only (no sin/cos/pow), so every host draws the same pixels. */
#include "jk_art.h"

#include <stdlib.h>
#include <string.h>

static uint32_t *g_bg;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
static uint32_t hash2(int x, int y, uint32_t seed) { return hash32((uint32_t)x * 0x9E3779B1u ^ hash32((uint32_t)y + seed)); }

static inline int ch(uint32_t c, int s) { return (int)((c >> s) & 255); }
static inline uint32_t rgb3(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline uint32_t mixc(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    if (a >= 255) return s;
    return rgb3((ch(d, 16) * (255 - a) + ch(s, 16) * a + 127) / 255, (ch(d, 8) * (255 - a) + ch(s, 8) * a + 127) / 255,
                (ch(d, 0) * (255 - a) + ch(s, 0) * a + 127) / 255);
}
static inline uint32_t addc(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    return rgb3(ch(d, 16) + (ch(s, 16) * a + 127) / 255, ch(d, 8) + (ch(s, 8) * a + 127) / 255, ch(d, 0) + (ch(s, 0) * a + 127) / 255);
}

/* A buffer view used for both the cache and frames. */
typedef struct Canvas { uint32_t *p; int w, h, stride; } Canvas;
static inline uint32_t *at(Canvas *c, int x, int y) { return c->p + (size_t)y * (size_t)c->stride + (size_t)x; }

static void cv_glow(Canvas *c, int cx, int cy, int rx, int ry, uint32_t rgb, int alpha)
{
    if (rx <= 0 || ry <= 0 || alpha <= 0) return;
    int y0 = cy - ry < 0 ? 0 : cy - ry, y1 = cy + ry >= c->h ? c->h - 1 : cy + ry;
    int x0 = cx - rx < 0 ? 0 : cx - rx, x1 = cx + rx >= c->w ? c->w - 1 : cx + rx;
    float irx = 1.0f / ((float)rx * (float)rx), iry = 1.0f / ((float)ry * (float)ry);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        float fy = dy * dy * iry;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float d = dx * dx * irx + fy;
            if (d >= 1.0f) continue;
            float k = (1.0f - d) * (1.0f - d);
            uint32_t *p = at(c, x, y);
            *p = addc(*p, rgb, (int)(k * (float)alpha + 0.5f));
        }
    }
}

static void cv_rect(Canvas *c, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) { uint32_t *p = at(c, xx, yy); *p = mixc(*p, rgb, alpha); }
}

static void cv_rect_add(Canvas *c, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w, y1 = y + h > c->h ? c->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) { uint32_t *p = at(c, xx, yy); *p = addc(*p, rgb, alpha); }
}

/* A light streak from x (tail) to x + len (head), additive, brightest at the
 * head when dir > 0 (else at x). 3 px tall with a soft 1 px fringe. */
static void cv_streak(Canvas *c, int x, int y, int len, int dir, uint32_t rgb, int alpha)
{
    for (int i = 0; i < len; i++) {
        int t = dir > 0 ? i : len - 1 - i;        /* 0 at the tail */
        int a = alpha * (t + 1) * (t + 1) / (len * len);
        int xx = x + i;
        if (xx < 0 || xx >= c->w) continue;
        for (int k = -2; k <= 2; k++) {
            int yy = y + k;
            if (yy < 0 || yy >= c->h) continue;
            int ak = k == 0 ? a : (k == -1 || k == 1) ? a * 2 / 3 : a / 5;
            uint32_t *p = at(c, xx, yy);
            *p = addc(*p, rgb, ak);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Background                                                         */
/* ------------------------------------------------------------------ */

#define HORIZON 640
#define DECK_Y 760

typedef struct Stop { int y; uint32_t c; } Stop;
static const Stop SKY[] = {
    { 0, 0x070A16 }, { 240, 0x141530 }, { 420, 0x3A1C3E }, { 560, 0x8A3A34 }, { 620, 0xD06A34 }, { HORIZON, 0xE88A42 },
};

static uint32_t sky_at(int y)
{
    const int n = (int)(sizeof SKY / sizeof SKY[0]);
    if (y <= SKY[0].y) return SKY[0].c;
    for (int i = 1; i < n; i++)
        if (y <= SKY[i].y) {
            int t = (y - SKY[i - 1].y) * 255 / (SKY[i].y - SKY[i - 1].y);
            return mixc(SKY[i - 1].c, SKY[i].c, t);
        }
    return SKY[n - 1].c;
}

/* One layer of building silhouettes standing on base_y. */
static void skyline(Canvas *c, uint32_t seed, int x_from, int base_y, int min_h, int max_h, int min_w, int max_w,
                    uint32_t col, int windows, int win_alpha)
{
    int x = x_from - (int)(hash32(seed) % 60);
    int i = 0;
    while (x < c->w) {
        uint32_t r = hash32(seed + (uint32_t)i * 7919u);
        int w = min_w + (int)(r % (uint32_t)(max_w - min_w + 1));
        int h = min_h + (int)((r >> 8) % (uint32_t)(max_h - min_h + 1));
        /* a few towers stand out */
        if (((r >> 20) & 15) == 0) h = h * 3 / 2;
        int top = base_y - h;
        cv_rect(c, x, top, w, h, col, 255);
        /* a stepped roof or antenna on some */
        if (((r >> 24) & 3) == 0) cv_rect(c, x + w / 4, top - h / 10, w / 2, h / 10, col, 255);
        if (((r >> 26) & 7) == 1) {
            cv_rect(c, x + w / 2 - 1, top - h / 4, 3, h / 4, col, 255);
            cv_glow(c, x + w / 2, top - h / 4, 7, 7, JK_RGB_SIREN_RED, 160);
        }
        if (windows) {
            for (int wy = top + 10; wy < base_y - 8; wy += 14)
                for (int wx = x + 6; wx < x + w - 8; wx += 11) {
                    uint32_t q = hash2(wx, wy, seed);
                    if ((q & 255) >= (uint32_t)windows) continue;
                    uint32_t wc = (q >> 8) % 5 == 0 ? 0x9CC4FF : (q >> 8) % 5 == 1 ? 0xFFE2B0 : 0xFFB866;
                    cv_rect(c, wx, wy, 4, 6, wc, win_alpha * (int)(128 + ((q >> 16) & 127)) / 255);
                }
        }
        x += w + (int)((r >> 12) % 6);
        i++;
    }
}

static void build_background(Canvas *c)
{
    /* sky */
    for (int y = 0; y < c->h; y++) {
        uint32_t s = sky_at(y);
        for (int x = 0; x < c->w; x++) *at(c, x, y) = s;
    }
    /* low sun behind the city, and the two police glows on the horizon */
    cv_glow(c, 1320, HORIZON + 10, 760, 240, 0xFF8A3A, 110);
    cv_glow(c, 1320, HORIZON, 260, 70, 0xFFD08A, 90);
    cv_glow(c, 300, HORIZON - 30, 340, 140, JK_RGB_SIREN_RED, 100);
    cv_glow(c, 1680, HORIZON - 30, 360, 140, JK_RGB_SIREN_BLUE, 120);

    /* city: far haze, middle, near */
    skyline(c, 11u, 0, HORIZON + 30, 30, 120, 28, 80, 0x3B2440, 0, 0);
    cv_rect(c, 0, HORIZON + 10, c->w, 30, 0x5A3040, 90);              /* haze over the far city */
    skyline(c, 23u, 0, DECK_Y + 4, 60, 230, 40, 110, 0x161423, 30, 200);
    /* near towers framing the right side */
    skyline(c, 37u, 1460, DECK_Y + 4, 300, 430, 110, 190, 0x0A0B11, 18, 160);
    /* ground behind the deck */
    for (int y = DECK_Y; y < c->h; y++) {
        int t = (y - DECK_Y) * 255 / (c->h - DECK_Y);
        uint32_t g = mixc(0x12111A, 0x050608, t);
        for (int x = 0; x < c->w; x++) *at(c, x, y) = g;
    }

    /* the elevated highway: deck, rail highlight, pillars */
    cv_rect(c, 0, DECK_Y, c->w, 46, 0x0E0F16, 255);
    cv_rect(c, 0, DECK_Y - 4, c->w, 4, 0x2A2230, 255);
    cv_rect_add(c, 0, DECK_Y - 4, c->w, 1, 0xFF9A3C, 70);
    for (int x = 40; x < c->w; x += 260) cv_rect(c, x, DECK_Y + 46, 34, c->h, 0x0B0C12, 255);
    /* street lamps along the rail */
    for (int x = 90; x < c->w; x += 180) cv_glow(c, x, DECK_Y - 18, 22, 12, 0xFFB060, 120);
    /* static traffic: tail lights going right, headlights coming left */
    for (int i = 0; i < 9; i++) {
        uint32_t r = hash32(101u + (uint32_t)i);
        int x = (int)(r % (uint32_t)c->w);
        cv_streak(c, x, DECK_Y + 14, 120 + (int)((r >> 10) % 160), 1, JK_RGB_SIREN_RED, 150);
        uint32_t q = hash32(211u + (uint32_t)i);
        int x2 = (int)(q % (uint32_t)c->w);
        cv_streak(c, x2, DECK_Y + 30, 120 + (int)((q >> 10) % 160), -1, 0xFFE6C0, 120);
    }

    /* harbour water under the deck: broken reflections of the dusk and the lights */
    for (int y = DECK_Y + 70; y < c->h - 40; y += 5) {
        int fade = 150 - (y - DECK_Y - 70) * 140 / (c->h - DECK_Y - 110);
        for (int k = 0; k < 26; k++) {
            uint32_t q = hash2(k, y, 0x5EA5u);
            int x = (int)(q % (uint32_t)c->w), len = 12 + (int)((q >> 12) % 90u);
            uint32_t col = (q >> 8) % 7 == 0 ? JK_RGB_SIREN_BLUE : (q >> 8) % 7 == 1 ? JK_RGB_SIREN_RED : 0xE88A42;
            cv_rect_add(c, x, y, len, 2, col, fade * (int)(40 + ((q >> 20) & 63)) / 255);
        }
    }

    /* left scrim: menus and panels sit on the left, keep them legible */
    for (int x = 0; x < c->w && x < 1200; x++) {
        int a = 170 * (1200 - x) / 1200;
        for (int y = 0; y < c->h; y++) { uint32_t *p = at(c, x, y); *p = mixc(*p, 0x05060A, a); }
    }
    /* vignette and grain (the grit) */
    for (int y = 0; y < c->h; y++) {
        int dy = y - c->h / 2;
        long long fy = (long long)dy * dy * 1000 / ((long long)(c->h / 2) * (c->h / 2));
        for (int x = 0; x < c->w; x++) {
            int dx = x - c->w / 2;
            long long d = (long long)dx * dx * 1000 / ((long long)(c->w / 2) * (c->w / 2)) + fy;   /* 0..2000 */
            uint32_t *p = at(c, x, y);
            if (d > 450) { int a = (int)((d - 450) * 200 / 1550); *p = mixc(*p, 0x000000, a > 200 ? 200 : a); }
            int n = (int)(hash2(x, y, 0xC0FFEEu) % 9u) - 4;
            *p = rgb3(ch(*p, 16) + n, ch(*p, 8) + n, ch(*p, 0) + n);
        }
    }
}

void jk_background(JkFrame *f)
{
    if (!f || !f->pixels) return;
    if (!g_bg) {
        g_bg = malloc((size_t)JK_W * JK_H * 4);
        if (!g_bg) { /* out of memory: a plain dark frame */
            for (uint32_t y = 0; y < f->height; y++)
                for (uint32_t x = 0; x < f->width; x++) ((uint32_t *)(void *)(f->pixels + (size_t)y * f->stride))[x] = JK_RGB_INK;
            return;
        }
        Canvas c = { g_bg, JK_W, JK_H, JK_W };
        build_background(&c);
    }
    uint32_t w = f->width < JK_W ? f->width : JK_W, h = f->height < JK_H ? f->height : JK_H;
    for (uint32_t y = 0; y < h; y++) memcpy(f->pixels + (size_t)y * f->stride, g_bg + (size_t)y * JK_W, (size_t)w * 4);
}

void jk_art_release(void) { free(g_bg); g_bg = NULL; }
size_t jk_art_cache_bytes(void) { return g_bg ? (size_t)JK_W * JK_H * 4 : 0; }

/* ------------------------------------------------------------------ */
/* Frame helpers                                                      */
/* ------------------------------------------------------------------ */

static int frame_canvas(JkFrame *f, Canvas *c)
{
    if (!f || !f->pixels) return 0;
    c->p = (uint32_t *)(void *)f->pixels; c->w = (int)f->width; c->h = (int)f->height; c->stride = (int)(f->stride / 4);
    return 1;
}

void jk_glow(JkFrame *f, int cx, int cy, int rx, int ry, uint32_t rgb, int alpha)
{
    Canvas c;
    if (frame_canvas(f, &c)) cv_glow(&c, cx, cy, rx, ry, rgb, alpha);
}

void jk_siren_bar(JkFrame *f, int x, int y, int w, int h, int phase)
{
    if (phase < 0) phase = 0;
    if (phase > 256) phase = 256;
    int a = phase > 255 ? 255 : phase;
    uint32_t left = mixc(JK_RGB_SIREN_RED, JK_RGB_SIREN_BLUE, a), right = mixc(JK_RGB_SIREN_BLUE, JK_RGB_SIREN_RED, a);
    int half = w / 2;
    jk_plate(f, x, y, half - 2, h, left, 255);
    jk_plate(f, x + half + 2, y, w - half - 2, h, right, 255);
}

int jk_wordmark(JkFrame *f, int x, int y, JkFont font)
{
    int w = jk_text(f, font, x, y, "JK", JK_RGB_CHALK);
    w += jk_text(f, font, x + w, y, "4", JK_RGB_DUSK);
    w += jk_text(f, font, x + w, y, "SPEED", JK_RGB_CHALK);
    int line = jk_font_line_height(font), asc = jk_font_ascent(font);
    int bar_h = line / 16 < 4 ? 4 : line / 16;
    int by = y + asc + line / 10;
    (void)line;
    jk_siren_bar(f, x, by, w, bar_h, 0);
    return w;
}

/* ------------------------------------------------------------------ */
/* Pad glyphs: a supersampled (4x4) shape test per pixel              */
/* ------------------------------------------------------------------ */

typedef struct Shape { JkButton b; float s; } Shape;   /* coordinates are in [0, s) */

static float seg_d2(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy, t = l2 > 0.0f ? (wx * vx + wy * vy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float dx = wx - t * vx, dy = wy - t * vy;
    return dx * dx + dy * dy;
}

static int in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy)
{
    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
    int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

/* 1: inside the symbol (ink), 0: not. Coordinates relative to the centre,
 * scaled so the cap radius is 1. */
static int symbol_at(JkButton b, float x, float y)
{
    const float t = 0.10f;                         /* stroke half-width */
    switch (b) {
    case JK_BTN_CROSS:
        return seg_d2(x, y, -0.42f, -0.42f, 0.42f, 0.42f) < t * t || seg_d2(x, y, -0.42f, 0.42f, 0.42f, -0.42f) < t * t;
    case JK_BTN_CIRCLE: {
        float d = x * x + y * y;
        return d < 0.52f * 0.52f && d > 0.32f * 0.32f;
    }
    case JK_BTN_SQUARE: {
        float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        float m = ax > ay ? ax : ay;
        return m < 0.44f && m > 0.44f - 2 * t;
    }
    case JK_BTN_TRIANGLE:
        return in_tri(x, y, 0.0f, -0.50f, -0.52f, 0.40f, 0.52f, 0.40f) && !in_tri(x, y, 0.0f, -0.24f, -0.29f, 0.27f, 0.29f, 0.27f);
    case JK_BTN_UPDOWN:
        return in_tri(x, y, 0.0f, -0.62f, -0.36f, -0.10f, 0.36f, -0.10f) || in_tri(x, y, 0.0f, 0.62f, -0.36f, 0.10f, 0.36f, 0.10f);
    case JK_BTN_LEFTRIGHT:
        return in_tri(x, y, -0.62f, 0.0f, -0.10f, -0.36f, -0.10f, 0.36f) || in_tri(x, y, 0.62f, 0.0f, 0.10f, -0.36f, 0.10f, 0.36f);
    default:
        return 0;
    }
}

void jk_button_glyph(JkFrame *f, int x, int y, int size, JkButton b)
{
    if (!f || !f->pixels || size <= 4) return;
    float r = (float)size / 2.0f;
    for (int py = 0; py < size; py++)
        for (int px = 0; px < size; px++) {
            int cap = 0, ink = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float fx = ((float)px + ((float)sx + 0.5f) / 4.0f - r) / r;
                    float fy = ((float)py + ((float)sy + 0.5f) / 4.0f - r) / r;
                    if (fx * fx + fy * fy < 1.0f) {
                        cap++;
                        if (symbol_at(b, fx, fy)) ink++;
                    }
                }
            if (!cap) continue;
            jk_put(f, x + px, y + py, JK_RGB_CHALK, cap * 255 / 16);
            if (ink) jk_put(f, x + px, y + py, JK_RGB_INK, ink * 255 / 16);
        }
}

static int icon_at(JkIcon icon, float x, float y)
{
    const float t = 0.17f;
    float d = x * x + y * y;
    switch (icon) {
    case JK_ICON_OK:
        return seg_d2(x, y, -0.62f, 0.02f, -0.18f, 0.48f) < t * t || seg_d2(x, y, -0.18f, 0.48f, 0.66f, -0.50f) < t * t;
    case JK_ICON_BAD:
        return seg_d2(x, y, -0.52f, -0.52f, 0.52f, 0.52f) < t * t || seg_d2(x, y, -0.52f, 0.52f, 0.52f, -0.52f) < t * t;
    case JK_ICON_TODO: {
        float sx = x + y * JK_SLANT;               /* slanted like the plates */
        return sx > -0.45f && sx < 0.45f && y > -0.45f && y < 0.45f;
    }
    case JK_ICON_PENDING:
        return d < 0.50f * 0.50f && d > 0.30f * 0.30f;
    case JK_ICON_ACTIVE:
        return (d < 0.70f * 0.70f && d > 0.52f * 0.52f) || d < 0.28f * 0.28f;
    default:
        return 0;
    }
}

void jk_status_icon(JkFrame *f, int x, int y, int size, JkIcon icon)
{
    static const uint32_t colour[JK_ICON_COUNT] = { JK_RGB_OK, JK_RGB_SIREN_RED, JK_RGB_DUSK, JK_RGB_SMOKE, JK_RGB_DUSK };
    if (!f || !f->pixels || size <= 4 || (int)icon < 0 || icon >= JK_ICON_COUNT) return;
    float r = (float)size / 2.0f;
    for (int py = 0; py < size; py++)
        for (int px = 0; px < size; px++) {
            int ink = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float fx = ((float)px + ((float)sx + 0.5f) / 4.0f - r) / r;
                    float fy = ((float)py + ((float)sy + 0.5f) / 4.0f - r) / r;
                    ink += icon_at(icon, fx, fy);
                }
            if (ink) jk_put(f, x + px, y + py, colour[icon], ink * 255 / 16);
        }
}

/* ------------------------------------------------------------------ */
/* Loader traffic                                                     */
/* ------------------------------------------------------------------ */

void jk_streaks(JkFrame *f, uint64_t t_ms)
{
    Canvas c;
    if (!frame_canvas(f, &c)) return;
    const int span = JK_W + 600;                       /* a car travels this far per loop */
    int ph = (int)(t_ms % 3000u);
    for (int i = 0; i < 6; i++) {
        uint32_t r = hash32(401u + (uint32_t)i);
        int len = 180 + (int)(r % 220u);
        int laps = 1 + (int)((r >> 9) & 1u);           /* 1 or 2 laps per 3 s: the loop is exact */
        int start = (int)((r >> 12) % (uint32_t)span);
        int pos = (start + (int)((long long)ph * span * laps / 3000)) % span - 300;
        if (i & 1)
            cv_streak(&c, JK_W - pos - len, DECK_Y + 30, len, -1, 0xFFF0D0, 200);
        else
            cv_streak(&c, pos, DECK_Y + 14, len, 1, JK_RGB_SIREN_RED, 230);
    }
}
