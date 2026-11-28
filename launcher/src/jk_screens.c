/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The screens. See jk_screens.h and spec section 7. */
#include "jk_screens.h"
#include "jk_art.h"
#include "jk_strings.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared pieces                                                      */
/* ------------------------------------------------------------------ */

#define HINT_Y 956
#define HINT_GLYPH 44
#define PANEL_Y 280
#define PANEL_H 600
#define TITLE_Y 156
#define CHIPS_Y 112

typedef struct Hint { JkButton b; const char *label; } Hint;

/* The button hints, bottom left; the build id bottom right. */
static void hint_bar(JkFrame *f, const Hint *h, int n, const char *build_id)
{
    int x = 130;
    int ty = HINT_Y + (HINT_GLYPH - jk_font_line_height(JK_FONT_HINT)) / 2;
    for (int i = 0; i < n; i++) {
        jk_button_glyph(f, x, HINT_Y, HINT_GLYPH, h[i].b);
        x += HINT_GLYPH + 14;
        x += jk_text(f, JK_FONT_HINT, x, ty, h[i].label, JK_RGB_CHALK) + 44;
    }
    if (build_id) {
        int w = jk_text_width(JK_FONT_HINT, build_id);
        jk_text(f, JK_FONT_HINT, 1794 - w, ty, build_id, JK_RGB_SMOKE);
    }
}

/* A plain message line: hint bar position, no glyph (while installing). */
static void hint_text(JkFrame *f, const char *text, const char *build_id)
{
    int ty = HINT_Y + (HINT_GLYPH - jk_font_line_height(JK_FONT_HINT)) / 2;
    jk_text(f, JK_FONT_HINT, 130, ty, text, JK_RGB_SMOKE);
    if (build_id) {
        int w = jk_text_width(JK_FONT_HINT, build_id);
        jk_text(f, JK_FONT_HINT, 1794 - w, ty, build_id, JK_RGB_SMOKE);
    }
}

/* A pad button inside a text (JK_K_*): drawn as its glyph, like JkFlow's
 * "press □ Help". Returns the button and sets *len, or -1. */
static int key_at(const char *s, size_t *len)
{
    static const struct { const char *k; JkButton b; } KEYS[] = {
        { JK_K_CROSS, JK_BTN_CROSS }, { JK_K_CIRCLE, JK_BTN_CIRCLE }, { JK_K_SQUARE, JK_BTN_SQUARE }, { JK_K_TRIANGLE, JK_BTN_TRIANGLE },
    };
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        size_t n = strlen(KEYS[i].k);
        if (!strncmp(s, KEYS[i].k, n)) { *len = n; return (int)KEYS[i].b; }
    }
    return -1;
}

static int key_size(JkFont font) { return jk_font_line_height(font) * 4 / 5; }

/* One line with its keys as glyphs (f NULL: measure). Returns the width. */
static int line_keys(JkFrame *f, JkFont font, int x, int y, const char *s, uint32_t rgb)
{
    int x0 = x, g = key_size(font), lh = jk_font_line_height(font);
    char seg[512];
    size_t n = 0;
    for (const char *p = s;; ) {
        size_t kl = 0;
        int b = *p ? key_at(p, &kl) : -1;
        if (!*p || b >= 0) {
            seg[n] = 0;
            if (n) x += f ? jk_text(f, font, x, y, seg, rgb) : jk_text_width(font, seg);
            n = 0;
            if (!*p) break;
            if (f) jk_button_glyph(f, x + 2, y + (lh - g) / 2, g, (JkButton)b);
            x += g + 4;
            p += kl;
            continue;
        }
        if (n + 1 < sizeof seg) seg[n++] = *p;
        p++;
    }
    return x - x0;
}

/* Greedy word wrap into width pixels. Returns the y below the last line. */
static int wrap(JkFrame *f, JkFont font, int x, int y, int width, const char *text, uint32_t rgb)
{
    int lh = jk_font_line_height(font) + 8;
    char line[512];
    size_t n = 0;
    const char *p = text;
    while (p && *p) {
        const char *sp = strchr(p, ' ');
        size_t wl = sp ? (size_t)(sp - p) : strlen(p);
        char trial[512];
        if (n + 1 + wl >= sizeof trial) break;
        if (n) { memcpy(trial, line, n); trial[n] = ' '; memcpy(trial + n + 1, p, wl); trial[n + 1 + wl] = 0; }
        else { memcpy(trial, p, wl); trial[wl] = 0; }
        if (n && line_keys(NULL, font, 0, 0, trial, rgb) > width) {
            line[n] = 0;
            line_keys(f, font, x, y, line, rgb);
            y += lh;
            memcpy(line, p, wl);
            n = wl;
        } else {
            n = strlen(trial);
            memcpy(line, trial, n);
        }
        p = sp ? sp + 1 : NULL;
    }
    if (n) { line[n] = 0; line_keys(f, font, x, y, line, rgb); y += lh; }
    return y;
}

/* A dark panel with a slab on its left edge in the state's colour. */
static void panel(JkFrame *f, int x, int y, int w, int h, uint32_t slab)
{
    jk_fill_alpha(f, x, y, w, h, JK_RGB_INK, 205);
    jk_fill(f, x, y, 6, h, slab);
}

/* A slanted CHALK button with a glyph and an INK label. */
static void button(JkFrame *f, int x, int y, JkButton b, const char *label)
{
    int lw = jk_text_width(JK_FONT_HEAD, label), h = 76;
    jk_plate(f, x, y, 30 + HINT_GLYPH + 16 + lw + 40, h, JK_RGB_CHALK, 255);
    int gx = x + 30, gy = y + (h - HINT_GLYPH) / 2;
    jk_button_glyph(f, gx, gy, HINT_GLYPH, b);
    jk_text(f, JK_FONT_HEAD, gx + HINT_GLYPH + 16, y + (h - jk_font_line_height(JK_FONT_HEAD)) / 2, label, JK_RGB_INK);
}

/* A secondary button: outlined, CHALK text on the dark. */
static void ghost_button(JkFrame *f, int x, int y, JkButton b, const char *label)
{
    int lw = jk_text_width(JK_FONT_HEAD, label), h = 76, w = 30 + HINT_GLYPH + 16 + lw + 40;
    jk_plate(f, x, y, w, h, JK_RGB_CHALK, 255);
    jk_plate(f, x + 4, y + 4, w - 8, h - 8, JK_RGB_ASPHALT, 255);
    int gx = x + 30, gy = y + (h - HINT_GLYPH) / 2;
    jk_button_glyph(f, gx, gy, HINT_GLYPH, b);
    jk_text(f, JK_FONT_HEAD, gx + HINT_GLYPH + 16, y + (h - jk_font_line_height(JK_FONT_HEAD)) / 2, label, JK_RGB_CHALK);
}

static void page_title(JkFrame *f, const char *title)
{
    jk_text(f, JK_FONT_MENU, 150, TITLE_Y, title, JK_RGB_CHALK);
}

/* A path or folder in a dark box. Returns the y below it. */
static int path_box(JkFrame *f, int x, int y, int w, const char *path, JkFont font)
{
    int h = jk_font_line_height(font) + 30;
    jk_fill(f, x, y, w, h, JK_RGB_ASPHALT);
    jk_text(f, font, x + 24, y + 15, path, JK_RGB_CHALK);
    return y + h + 20;
}

/* A confirm dialog over a dimmed screen. */
static void dialog(JkFrame *f, const char *head, const char *body, const char *ok)
{
    jk_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x000000, 170);
    int dx = 460, dy = 300, dw = 1000, dh = 460;
    jk_fill(f, dx, dy, dw, dh, JK_RGB_ASPHALT);
    jk_fill(f, dx, dy, 6, dh, JK_RGB_DUSK);
    jk_text(f, JK_FONT_HEAD, dx + 50, dy + 46, head, JK_RGB_DUSK);
    wrap(f, JK_FONT_BODY, dx + 50, dy + 130, dw - 100, body, JK_RGB_CHALK);
    button(f, dx + 50, dy + dh - 116, JK_BTN_CROSS, ok);
    ghost_button(f, dx + 420, dy + dh - 116, JK_BTN_CIRCLE, JK_S_H_CANCEL);
    Hint h[2] = { { JK_BTN_CROSS, ok }, { JK_BTN_CIRCLE, JK_S_H_CANCEL } };
    hint_bar(f, h, 2, NULL);
}

/* ------------------------------------------------------------------ */
/* Release badge (top right of every screen)                          */
/* ------------------------------------------------------------------ */

const char *jk_badge_text(const JkRelease *r)
{
    return r && r->state == JK_REL_OFFICIAL ? JK_S_B_OFFICIAL : JK_S_B_UNOFFICIAL;
}

/* "Official Build by XxXJoker13XxX" or "Unofficial build", right-aligned,
 * with a dot: green for official, grey otherwise. r5 (owner): no key id or
 * build id here; About shows them. */
static void badge(JkFrame *f, const JkModel *m)
{
    int official = m->release.state == JK_REL_OFFICIAL;
    const char *main_text = jk_badge_text(&m->release);
    int wm = jk_text_width(JK_FONT_HINT, main_text);
    int x = JK_BADGE_RIGHT - wm, y = JK_BADGE_Y;
    jk_plate(f, x - 30, y + 10, 16, 16, official ? JK_RGB_OK : JK_RGB_SMOKE, 255);
    jk_text(f, JK_FONT_HINT, x, y, main_text, official ? JK_RGB_CHALK : JK_RGB_SMOKE);
}

/* 1.1: the game this screen is about, under the badge (not on Home: its halves say it). */
static void game_label(JkFrame *f, const JkModel *m)
{
    if (m->ngames < 2 || m->screen == JK_SCR_HOME || m->active < 0 || m->active > 1 || !m->slot_label[m->active]) return;
    const char *t = m->slot_label[m->active];
    int w = jk_text_width(JK_FONT_HINT, t);
    int x = JK_BADGE_RIGHT - w, y = JK_BADGE_Y + 44;
    jk_plate(f, x - 30, y + 10, 16, 16, JK_RGB_DUSK, 255);
    jk_text(f, JK_FONT_HINT, x, y, t, JK_RGB_CHALK);
}

static void chrome(JkFrame *f, const JkModel *m)
{
    jk_background(f);
    jk_wordmark(f, 120, 66, JK_FONT_HEAD);
    badge(f, m);
    game_label(f, m);
}

/* ------------------------------------------------------------------ */
/* Home                                                               */
/* ------------------------------------------------------------------ */

typedef struct Chip { const char *text; uint32_t dot; } Chip;

int jk_two_folders(const JkModel *m) { return m && m->game_other && m->game_other[0]; }

static int chips_of(const JkModel *m, Chip *out)
{
    const JkCheck *c = &m->check;
    int n = 0;
    if (m->logs_on) out[n++] = (Chip){ JK_S_C_LOGS_ON, JK_RGB_DUSK };
    if (jk_two_folders(m)) { out[n++] = (Chip){ JK_S_C_TWO, JK_RGB_SIREN_RED }; return n; }
    switch (c->state) {
    case JK_ST_BAD_PACKAGE: out[n++] = (Chip){ JK_S_C_DAMAGED, JK_RGB_SIREN_RED }; break;
    case JK_ST_NO_GAME: out[n++] = (Chip){ JK_S_C_NOT_FOUND, JK_RGB_SIREN_RED }; break;
    case JK_ST_WRONG_EXE:
        out[n++] = (Chip){ JK_S_C_FOUND, JK_RGB_OK };
        out[n++] = (Chip){ JK_S_C_OTHER, JK_RGB_SIREN_RED };
        break;
    case JK_ST_MISSING_FILES:
        out[n++] = (Chip){ JK_S_C_FOUND, JK_RGB_OK };
        out[n++] = (Chip){ JK_S_C_V13, JK_RGB_OK };
        out[n++] = (Chip){ JK_S_C_MISSING, JK_RGB_SIREN_RED };
        break;
    default:
        out[n++] = (Chip){ JK_S_C_FOUND, JK_RGB_OK };
        out[n++] = m->exe_any && !c->exe_known ? (Chip){ JK_S_C_UNKNOWN_EXE, JK_RGB_DUSK } : (Chip){ JK_S_C_V13, JK_RGB_OK };
        out[n++] = c->state == JK_ST_READY ? (Chip){ JK_S_C_INSTALLED, JK_RGB_OK }
                 : c->state == JK_ST_UPDATE ? (Chip){ JK_S_C_UPDATE, JK_RGB_DUSK }
                 : c->state == JK_ST_CHANGED ? (Chip){ JK_S_C_CHANGED, JK_RGB_DUSK }
                 : (Chip){ JK_S_C_NOT_INSTALLED, JK_RGB_DUSK };
        break;
    }
    return n;
}

static void status_chips(JkFrame *f, const JkModel *m)
{
    Chip chips[5];
    int n = chips_of(m, chips);
    int x = 1794, y = CHIPS_Y;
    for (int i = n - 1; i >= 0; i--) {
        int w = jk_text_width(JK_FONT_HINT, chips[i].text);
        x -= w;
        jk_text(f, JK_FONT_HINT, x, y, chips[i].text, JK_RGB_CHALK);
        x -= 30;
        jk_plate(f, x, y + 12, 16, 16, chips[i].dot, 255);
        x -= 40;
    }
}

static const char *home_description(const JkModel *m)
{
    switch (m->focus) {
    case JK_HOME_PATCH: return JK_S_D_PATCH;
    case JK_HOME_SETTINGS: return JK_S_D_SETTINGS;
    default:
        if (jk_two_folders(m)) return JK_S_D_START_TWO;
        switch (m->check.state) {
        case JK_ST_READY: return JK_S_D_START_READY;
        case JK_ST_NOT_INSTALLED: return JK_S_D_START_INSTALL;
        case JK_ST_UPDATE: return JK_S_D_START_UPDATE;
        case JK_ST_CHANGED: return JK_S_D_START_CHANGED;
        case JK_ST_NO_GAME: return m->ngames >= 2 && m->active == 1 ? JK_S_D_START_NO_GAME_NFS : JK_S_D_START_NO_GAME;
        case JK_ST_WRONG_EXE: return JK_S_D_START_WRONG;
        case JK_ST_MISSING_FILES: return JK_S_D_START_MISSING;
        default: return JK_S_D_START_BAD;
        }
    }
}

/* One menu row: a CHALK plate with INK text when focused. value: "‹ v ›"
 * drawn after the label at value_x (NULL: none). */
static void menu_row(JkFrame *f, const JkModel *m, JkFont font, int x, int y, int h, const char *label, int focused,
                     int enabled, const char *value, int value_x, int left_on, int right_on)
{
    int lh = jk_font_line_height(font), asc = jk_font_ascent(font);
    int ty = y + (h - lh) / 2;
    int w = jk_text_width(font, label);
    if (value) w = value_x - x + jk_text_width(JK_FONT_HEAD, "\xE2\x80\xB9 ") + jk_text_width(JK_FONT_HEAD, value)
                   + jk_text_width(JK_FONT_HEAD, " \xE2\x80\xBA");
    uint32_t ink = !enabled ? JK_RGB_SMOKE : focused ? JK_RGB_INK : JK_RGB_CHALK;
    if (focused) {
        int px = x - 22, pw = w + 64;
        jk_plate(f, px - 24, y, 14, h, JK_RGB_DUSK, 255);
        jk_plate(f, px, y, pw, h, enabled ? JK_RGB_CHALK : JK_RGB_ASPHALT, 255);
        jk_siren_bar(f, px - 2, y + h + 8, pw, 6, m->siren);
    }
    jk_text(f, font, x, ty, label, ink);
    if (value) {
        int vy = ty + asc - jk_font_ascent(JK_FONT_HEAD);
        int vx = value_x;
        int arrows = focused && enabled;
        if (arrows) vx += jk_text_alpha(f, JK_FONT_HEAD, vx, vy, "\xE2\x80\xB9 ", ink, left_on ? 255 : 110);
        else vx += jk_text_width(JK_FONT_HEAD, "\xE2\x80\xB9 ");
        vx += jk_text(f, JK_FONT_HEAD, vx, vy, value, ink);
        if (arrows) jk_text_alpha(f, JK_FONT_HEAD, vx, vy, " \xE2\x80\xBA", ink, right_on ? 255 : 110);
    }
}

static const char *half_status(JkState s, uint32_t *rgb)
{
    switch (s) {
    case JK_ST_READY: *rgb = JK_RGB_OK; return JK_S_HS_READY;
    case JK_ST_NO_GAME: *rgb = JK_RGB_SMOKE; return JK_S_H_COPY_HERE;
    case JK_ST_WRONG_EXE: *rgb = JK_RGB_SIREN_RED; return JK_S_HS_WRONG;
    case JK_ST_MISSING_FILES: *rgb = JK_RGB_SIREN_RED; return JK_S_HS_MISSING;
    case JK_ST_BAD_PACKAGE: *rgb = JK_RGB_SIREN_RED; return JK_S_HS_BAD;
    default: *rgb = JK_RGB_DUSK; return JK_S_HS_PATCH;
    }
}

/* One game's half: its name, READY / PATCH NEEDED / COPY THE GAME HERE, its folder. The chosen
 * half has the accent frame; a side without a game is dimmed (and can still be chosen). */
static void home_half(JkFrame *f, const JkModel *m, int i)
{
    int x = JK_HALF_X0 + i * (JK_HALF_W + JK_HALF_GAP), y = JK_HALF_Y, w = JK_HALF_W, h = JK_HALF_H;
    JkState s = m->slot_state[i];
    int none = s == JK_ST_NO_GAME, on = m->active == i;
    uint32_t rgb;
    const char *status = half_status(s, &rgb);
    panel(f, x, y, w, h, none ? JK_RGB_SMOKE : rgb);
    if (on) {                                      /* the accent frame */
        jk_fill(f, x, y, w, 4, JK_RGB_DUSK);
        jk_fill(f, x, y + h - 4, w, 4, JK_RGB_DUSK);
        jk_fill(f, x, y, 4, h, JK_RGB_DUSK);
        jk_fill(f, x + w - 4, y, 4, h, JK_RGB_DUSK);
    }
    const char *label = m->slot_label[i] ? m->slot_label[i] : i ? JK_S_G_OG : JK_S_G_BE;
    jk_text(f, JK_FONT_HEAD, x + 40, y + 24, label, none ? JK_RGB_SMOKE : JK_RGB_CHALK);
    jk_text(f, JK_FONT_BODY, x + 40, y + 24 + jk_font_line_height(JK_FONT_HEAD) + 4, status, rgb);
    const char *dir = none || !m->slot_dir[i] ? (i ? JK_S_GAME_DIR_NFS : JK_S_GAME_DIR) : m->slot_dir[i];
    jk_text(f, JK_FONT_HINT, x + 40, y + h - jk_font_line_height(JK_FONT_HINT) - 20, dir, JK_RGB_SMOKE);
}

static void draw_home2(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    status_chips(f, m);
    home_half(f, m, 0);
    home_half(f, m, 1);
    static const char *const labels[JK_HOME_COUNT] = { JK_S_PLAY, JK_S_PATCH, JK_S_SETTINGS };
    for (int i = 0; i < JK_HOME_COUNT; i++)
        menu_row(f, m, JK_FONT_MENU, JK_HOME_X, JK_HOME2_Y0 + i * JK_HOME2_STEP, JK_HOME2_ITEM_H, labels[i], m->focus == i, 1,
                 NULL, 0, 0, 0);
    int dy = JK_HOME2_Y0 + JK_HOME_COUNT * JK_HOME2_STEP + 30;
    wrap(f, JK_FONT_BODY, JK_HOME_X, dy, 1100, home_description(m), JK_RGB_CHALK);
    Hint h[4] = { { JK_BTN_CROSS, JK_S_H_SELECT }, { JK_BTN_UPDOWN, JK_S_H_CHOOSE }, { JK_BTN_LEFTRIGHT, JK_S_H_GAME },
                  { JK_BTN_SQUARE, JK_S_H_HELP } };
    hint_bar(f, h, 4, JK_S_FOOTER);
}

static void draw_home(JkFrame *f, const JkModel *m)
{
    if (m->ngames >= 2) { draw_home2(f, m); return; }
    chrome(f, m);
    status_chips(f, m);
    static const char *const labels[JK_HOME_COUNT] = { JK_S_PLAY, JK_S_PATCH, JK_S_SETTINGS };
    for (int i = 0; i < JK_HOME_COUNT; i++)
        menu_row(f, m, JK_FONT_MENU, JK_HOME_X, JK_HOME_Y0 + i * JK_HOME_STEP, JK_HOME_ITEM_H, labels[i], m->focus == i, 1,
                 NULL, 0, 0, 0);
    int dy = JK_HOME_Y0 + JK_HOME_COUNT * JK_HOME_STEP + 40;
    wrap(f, JK_FONT_BODY, JK_HOME_X, dy, 1100, home_description(m), JK_RGB_CHALK);
    Hint h[3] = { { JK_BTN_CROSS, JK_S_H_SELECT }, { JK_BTN_UPDOWN, JK_S_H_CHOOSE }, { JK_BTN_SQUARE, JK_S_H_HELP } };
    hint_bar(f, h, 3, JK_S_FOOTER);                 /* the permanent credit (lead decision 4) */
}

/* ------------------------------------------------------------------ */
/* Settings                                                           */
/* ------------------------------------------------------------------ */

int jk_pace_enabled(const JkModel *m)
{
    return m && (m->check.state == JK_ST_READY || m->check.state == JK_ST_UPDATE || m->check.state == JK_ST_CHANGED);
}

static const char *pace_name(JkPace p)
{
    return p == JK_PACE_60 ? JK_S_FPS_60 : p == JK_PACE_OTHER ? JK_S_FPS_OTHER : JK_S_FPS_30;
}

static const char *settings_description(const JkModel *m)
{
    switch (m->set_focus) {
    case JK_SET_FRAMERATE:
        if (!jk_pace_enabled(m)) return JK_S_D_FPS_OFF;
        return m->pace == JK_PACE_60 ? JK_S_D_FPS_60 : m->pace == JK_PACE_OTHER ? JK_S_D_FPS_OTHER : JK_S_D_FPS_30;
    case JK_SET_FPS_HUD: return JK_S_D_FPS_HUD;
    case JK_SET_MODS: return JK_S_D_MODS;
    case JK_SET_VISUALS: return JK_S_D_VISUALS;
    case JK_SET_CONTROLS: return JK_S_D_CONTROLS;
    case JK_SET_SAVES: return JK_S_D_SAVES;
    case JK_SET_LOGS: return JK_S_D_LOGS;
    default: return JK_S_D_ABOUT;
    }
}

const char *jk_settings_label(int item)
{
    static const char *const labels[JK_SET_COUNT] = { JK_S_FRAMERATE, JK_S_FPS_HUD, JK_S_MODS, JK_S_VISUALS, JK_S_CONTROLS, JK_S_SAVES, JK_S_LOGS, JK_S_ABOUT };
    return item >= 0 && item < JK_SET_COUNT ? labels[item] : "";
}

const char *jk_settings_value(const JkModel *m, int item)
{
    if (!m) return NULL;
    if (item == JK_SET_FRAMERATE) return pace_name(m->pace);
    if (item == JK_SET_FPS_HUD) return m->fps_hud ? JK_S_V_ON : JK_S_V_OFF;
    return NULL;
}

/* The right-hand panel: the focused item's name and what it does (JkFlow's
 * list + panel). Show FPS / ms shows the corner as it looks in a race. */
enum { SET_PX = 1000, SET_PW = 810 };
static void settings_panel(JkFrame *f, const JkModel *m)
{
    int py = JK_SET_Y0, ph = (JK_SET_COUNT - 1 - (m->no_mods ? 2 : 0)) * JK_SET_STEP + JK_SET_ITEM_H;
    int on = m->set_focus == JK_SET_FPS_HUD ? m->fps_hud : m->set_focus == JK_SET_FRAMERATE ? jk_pace_enabled(m) : 1;
    panel(f, SET_PX, py, SET_PW, ph, on ? JK_RGB_DUSK : JK_RGB_SMOKE);
    int x = SET_PX + 44, w = SET_PW - 88, y = py + 30;
    jk_text(f, JK_FONT_HEAD, x, y, jk_settings_label(m->set_focus), JK_RGB_DUSK);
    y += jk_font_line_height(JK_FONT_HEAD) + 18;
    if (m->set_focus == JK_SET_FPS_HUD) {
        int bh = 150;
        jk_fill(f, x, y, w, bh, JK_RGB_ASPHALT);
        jk_fill(f, x, y + bh * 55 / 100, w, 4, JK_RGB_SMOKE);
        if (m->fps_hud) {
            int tw = jk_text_width(JK_FONT_HINT, JK_S_FPS_HUD_SAMPLE);
            jk_text(f, JK_FONT_HINT, x + w - tw - 14 + 2, y + 12 + 2, JK_S_FPS_HUD_SAMPLE, JK_RGB_INK);
            jk_text(f, JK_FONT_HINT, x + w - tw - 14, y + 12, JK_S_FPS_HUD_SAMPLE, JK_RGB_CHALK);
        }
        y += bh + 24;
    }
    wrap(f, JK_FONT_BODY, x, y, w, settings_description(m), JK_RGB_CHALK);
}

static void draw_settings(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_SETTINGS);
    int pace_on = jk_pace_enabled(m);
    int row = 0;
    for (int i = 0; i < JK_SET_COUNT; i++) {
        int fps = i == JK_SET_FRAMERATE, hud = i == JK_SET_FPS_HUD;
        if (m->no_mods && (i == JK_SET_MODS || i == JK_SET_VISUALS)) continue;   /* NFS: no mods, no look rows */
        menu_row(f, m, JK_FONT_HEAD, JK_SET_X, JK_SET_Y0 + row++ * JK_SET_STEP, JK_SET_ITEM_H, jk_settings_label(i), m->set_focus == i,
                 fps ? pace_on : 1, jk_settings_value(m, i), JK_SET_X + 400,
                 fps ? m->pace != JK_PACE_30 : hud ? m->fps_hud : 0, fps ? m->pace != JK_PACE_60 : hud ? !m->fps_hud : 0);
    }
    settings_panel(f, m);
    Hint h[3];
    int n = 0;
    if (m->set_focus == JK_SET_FPS_HUD || (m->set_focus == JK_SET_FRAMERATE && pace_on)) h[n++] = (Hint){ JK_BTN_LEFTRIGHT, JK_S_H_CHANGE_FPS };
    else if (m->set_focus != JK_SET_FRAMERATE) h[n++] = (Hint){ JK_BTN_CROSS, JK_S_H_SELECT };
    h[n++] = (Hint){ JK_BTN_UPDOWN, JK_S_H_CHOOSE };
    h[n++] = (Hint){ JK_BTN_CIRCLE, JK_S_H_BACK };
    hint_bar(f, h, n, m->build_id);
}

/* ------------------------------------------------------------------ */
/* Controls (read-only release layout)                                */
/* ------------------------------------------------------------------ */


static void draw_controls(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_CONTROLS);
    panel(f, 150, PANEL_Y, 1660, PANEL_H, m->ctl_listen ? JK_RGB_DUSK : JK_RGB_SMOKE);
    int lh = jk_font_line_height(JK_FONT_BODY);
    /* the eleven remappable functions: name, button (or NONE when empty) */
    const int x = 194, rw = 860, step = 47, y0 = PANEL_Y + 30;
    for (int i = 0; i < JK_ACT_N; i++) {
        int y = y0 + i * step, focused = m->ctl_focus == i;
        int btn = m->controls.btn[i];
        if (focused) {
            jk_plate(f, x - 24, y - 4, 12, lh + 8, JK_RGB_DUSK, 255);
            jk_plate(f, x - 6, y - 4, rw, lh + 8, JK_RGB_ASPHALT, 255);
        }
        jk_text(f, JK_FONT_BODY, x + 10, y, jk_controls_action_name((JkCtlAction)i), JK_RGB_CHALK);
        const char *bn = btn == JK_CB_NONE ? JK_S_CT_NONE : jk_controls_button_name((JkCtlButton)btn);
        uint32_t ink = focused && m->ctl_listen ? JK_RGB_CHALK : btn == JK_CB_NONE ? JK_RGB_RED_TEXT : JK_RGB_DUSK;
        jk_text(f, JK_FONT_BODY, x + rw - 300, y, focused && m->ctl_listen ? JK_S_CT_PRESS : bn, ink);
    }
    /* fixed, never remapped */
    const int fx = 1120;
    jk_text(f, JK_FONT_HEAD, fx, y0, JK_S_CT_FIXED_HEAD, JK_RGB_CHALK);
    static const char *const fixed[][2] = {
        { "OPTIONS", "Pause" }, { "Left stick", "Steer" }, { "Right stick back", "Look behind" }, { "D-pad", "Menus" },
    };
    for (int i = 0; i < 4; i++) {
        int y = y0 + 70 + i * step;
        jk_text(f, JK_FONT_BODY, fx, y, fixed[i][0], JK_RGB_DUSK);
        jk_text(f, JK_FONT_BODY, fx + 300, y, fixed[i][1], JK_RGB_CHALK);
    }
    int ty = y0 + JK_ACT_N * step + 10;
    if (m->ctl_listen) {
        char line[160];
        snprintf(line, sizeof line, JK_S_CT_LISTEN, jk_controls_action_name((JkCtlAction)m->ctl_focus));
        wrap(f, JK_FONT_BODY, x, ty, 1560, line, JK_RGB_CHALK);
    } else {                                               /* help in the right column, under FIXED */
        int hy = wrap(f, JK_FONT_HINT, fx, y0 + 70 + 4 * step + 30, 640, JK_S_CT_HELP, JK_RGB_SMOKE);
        wrap(f, JK_FONT_HINT, fx, hy + 10, 640, JK_S_CT_NOTE, JK_RGB_SMOKE);
    }
    if (m->ctl_listen) {
        hint_text(f, JK_S_H_LISTEN, m->build_id);
    } else {
        Hint h[3] = { { JK_BTN_CROSS, JK_S_H_CHANGE }, { JK_BTN_TRIANGLE, JK_S_H_DEFAULT }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
        hint_bar(f, h, 3, m->build_id);
    }
}

/* ------------------------------------------------------------------ */
/* Visuals (1.11): the Xbox 360 look's sliders, a scrolling list       */
/* ------------------------------------------------------------------ */

enum { VIS_ROWS = 12 };
static void draw_visuals(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_VISUALS);
    panel(f, 150, PANEL_Y, 1660, PANEL_H, JK_RGB_SMOKE);
    int lh = jk_font_line_height(JK_FONT_BODY);
    const int x = 194, rw = 1100, step = 47, y0 = PANEL_Y + 30;
    int top = m->vis_focus - VIS_ROWS / 2;                 /* the focused slider in the middle when it can be */
    if (top > JK_VIS_N - VIS_ROWS) top = JK_VIS_N - VIS_ROWS;
    if (top < 0) top = 0;
    for (int r = 0; r < VIS_ROWS && top + r < JK_VIS_N; r++) {
        int i = top + r, y = y0 + r * step, focused = m->vis_focus == i;
        if (focused) {
            jk_plate(f, x - 24, y - 4, 12, lh + 8, JK_RGB_DUSK, 255);
            jk_plate(f, x - 6, y - 4, rw, lh + 8, JK_RGB_ASPHALT, 255);
        }
        jk_text(f, JK_FONT_BODY, x + 10, y, jk_visuals_label(i), JK_RGB_CHALK);
        char val[64];
        if (focused) snprintf(val, sizeof val, "\xE2\x80\xB9 %s \xE2\x80\xBA", jk_visuals_shown(&m->vis, i));
        else snprintf(val, sizeof val, "%s", jk_visuals_shown(&m->vis, i));
        jk_text(f, JK_FONT_BODY, x + 640, y, val, focused ? JK_RGB_CHALK : JK_RGB_DUSK);
    }
    char pos[16];
    snprintf(pos, sizeof pos, "%d / %d", m->vis_focus + 1, JK_VIS_N);
    jk_text(f, JK_FONT_HINT, x + rw + 40, y0, pos, JK_RGB_SMOKE);
    wrap(f, JK_FONT_HINT, x + rw + 40, y0 + 60, 1760 - (x + rw + 40), jk_visuals_about(m->vis_focus), JK_RGB_CHALK);
    Hint h[4] = { { JK_BTN_LEFTRIGHT, JK_S_H_CHANGE }, { JK_BTN_UPDOWN, JK_S_H_CHOOSE },
                  { JK_BTN_TRIANGLE, JK_S_H_VIS_DEFAULT }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 4, m->build_id);
}

/* ------------------------------------------------------------------ */
/* Save games                                                         */
/* ------------------------------------------------------------------ */

/* The import preview (alias, date, origin) and the collision question. */
static void draw_import(JkFrame *f, const JkModel *m)
{
    const JkSavesFound *s = &m->saves_found;
    int collide = m->dialog == JK_DLG_COLLIDE;
    jk_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x000000, 170);
    int dx = 360, dy = 220, dw = 1200, dh = 620;
    jk_fill(f, dx, dy, dw, dh, JK_RGB_ASPHALT);
    jk_fill(f, dx, dy, 6, dh, JK_RGB_DUSK);
    jk_text(f, JK_FONT_HEAD, dx + 50, dy + 46, collide ? JK_S_C_HEAD : JK_S_I_HEAD, JK_RGB_DUSK);
    int y = wrap(f, JK_FONT_BODY, dx + 50, dy + 120, dw - 100, collide ? JK_S_C_BODY : JK_S_I_BODY, JK_RGB_CHALK) + 8;
    int lh = jk_font_line_height(JK_FONT_BODY) + 6, shown = 0;
    for (int i = 0; i < s->n && shown < 5; i++) {
        const JkSaveCand *c = &s->c[i];
        if (collide && !c->exists) continue;
        char date[16], line[200];
        jk_saves_date(c->mtime, date);
        if (collide) snprintf(line, sizeof line, "%s   (keep both: the copy becomes %s)", c->alias, c->keep_as);
        else snprintf(line, sizeof line, "%s   %s   %s%s", c->alias, date, c->console ? JK_S_I_CONSOLE : JK_S_I_PC,
                      c->exists ? "   - " JK_S_I_ON : "");
        jk_text(f, JK_FONT_BODY, dx + 70, y, line, c->exists && !collide ? JK_RGB_DUSK : JK_RGB_CHALK);
        y += lh;
        shown++;
    }
    if (!collide && s->rejected) {
        char line[200];
        snprintf(line, sizeof line, "Skipped %d file%s that %s not a Most Wanted save (%s).", s->rejected, s->rejected == 1 ? "" : "s",
                 s->rejected == 1 ? "is" : "are", s->rejected_name);
        y = wrap(f, JK_FONT_HINT, dx + 70, y + 4, dw - 140, line, JK_RGB_SMOKE);
    }
    if (collide) wrap(f, JK_FONT_HINT, dx + 50, y + 8, dw - 100, JK_S_C_NOTE, JK_RGB_SMOKE);
    if (collide) {
        button(f, dx + 50, dy + dh - 116, JK_BTN_CROSS, JK_S_H_OVERWRITE);
        ghost_button(f, dx + 400, dy + dh - 116, JK_BTN_SQUARE, JK_S_H_KEEP_BOTH);
        ghost_button(f, dx + 780, dy + dh - 116, JK_BTN_CIRCLE, JK_S_H_CANCEL);
        Hint h[3] = { { JK_BTN_CROSS, JK_S_H_OVERWRITE }, { JK_BTN_SQUARE, JK_S_H_KEEP_BOTH }, { JK_BTN_CIRCLE, JK_S_H_CANCEL } };
        hint_bar(f, h, 3, NULL);
    } else {
        button(f, dx + 50, dy + dh - 116, JK_BTN_CROSS, JK_S_H_IMPORT_OK);
        ghost_button(f, dx + 420, dy + dh - 116, JK_BTN_CIRCLE, JK_S_H_CANCEL);
        Hint h[2] = { { JK_BTN_CROSS, JK_S_H_IMPORT_OK }, { JK_BTN_CIRCLE, JK_S_H_CANCEL } };
        hint_bar(f, h, 2, NULL);
    }
}

static void draw_saves(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_SAVES);
    uint32_t slab = m->saves_state > 0 ? JK_RGB_OK : m->saves_state < 0 ? JK_RGB_SIREN_RED : JK_RGB_SMOKE;
    panel(f, 150, PANEL_Y, 1660, PANEL_H, slab);
    int x = 194, w = 1570;
    jk_text(f, JK_FONT_HEAD, x, PANEL_Y + 34, JK_S_G_HEAD, JK_RGB_CHALK);
    int y = wrap(f, JK_FONT_BODY, x, PANEL_Y + 110, w, JK_S_G_WHERE, JK_RGB_CHALK);
    y = path_box(f, x, y + 4, w, m->active == 1 ? JK_S_SAVES_DIR_NFS : JK_S_SAVES_DIR, JK_FONT_HINT);   /* 1.1: each game its own */
    char count[64];
    if (m->saves_profiles > 0) snprintf(count, sizeof count, "%d profile%s found.", m->saves_profiles, m->saves_profiles == 1 ? "" : "s");
    else snprintf(count, sizeof count, "%s", JK_S_G_NONE);
    jk_text(f, JK_FONT_BODY, x, y, count, m->saves_profiles > 0 ? JK_RGB_OK : JK_RGB_SMOKE);
    y += jk_font_line_height(JK_FONT_BODY) + 6;
    if (m->saves_profiles <= 0) y = wrap(f, JK_FONT_HINT, x, y, w, JK_S_G_NONE_NOTE, JK_RGB_SMOKE) + 6;
    y += 12;
    y = wrap(f, JK_FONT_BODY, x, y, w, JK_S_G_BRING, JK_RGB_CHALK);
    y = wrap(f, JK_FONT_HINT, x, y + 4, w, m->active == 1 ? JK_S_G_V13_NFS : JK_S_G_V13, JK_RGB_SMOKE);
    y += 10;
    if (m->saves_state == 1) {
        char line[400];
        snprintf(line, sizeof line, "%s %s", JK_S_G_EXPORTED, m->saves_msg);
        wrap(f, JK_FONT_BODY, x, y, w, line, JK_RGB_OK);
    } else if (m->saves_state == 2) {
        char line[400];
        if (m->saves_msg[0]) snprintf(line, sizeof line, "%s %s", JK_S_G_IMPORTED, m->saves_msg);
        else snprintf(line, sizeof line, "%s", JK_S_G_IMPORTED_NEW);
        wrap(f, JK_FONT_BODY, x, y, w, line, JK_RGB_OK);
    } else if (m->saves_state == 3) {
        char line[400];
        snprintf(line, sizeof line, "%s %s", JK_S_G_RENAMED, m->saves_msg);
        wrap(f, JK_FONT_BODY, x, y, w, line, JK_RGB_OK);
    } else if (m->saves_state < 0) {
        wrap(f, JK_FONT_BODY, x, y, w, m->saves_msg, JK_RGB_RED_TEXT);
    }
    if (m->dialog == JK_DLG_IMPORT || m->dialog == JK_DLG_COLLIDE) { draw_import(f, m); return; }
    if (m->dialog == JK_DLG_KEYBOARD) {
        jk_keyboard_draw(f, &m->kb);
        Hint kh[4] = { { JK_BTN_CROSS, "Type" }, { JK_BTN_SQUARE, "Delete" }, { JK_BTN_TRIANGLE, "Shift" },
                       { JK_BTN_CIRCLE, JK_S_H_CANCEL } };
        hint_bar(f, kh, 4, NULL);
        return;
    }
    if (m->dialog == JK_DLG_PICK) {
        jk_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x000000, 170);
        int dx = 560, dy = 240, dw = 800, dh = 600;
        jk_fill(f, dx, dy, dw, dh, JK_RGB_ASPHALT);
        jk_fill(f, dx, dy, 6, dh, JK_RGB_DUSK);
        jk_text(f, JK_FONT_HEAD, dx + 50, dy + 46, JK_S_R_PICK, JK_RGB_DUSK);
        int lh = jk_font_line_height(JK_FONT_BODY) + 14;
        for (int i = 0; i < m->save_count && i < 8; i++) {
            int yy = dy + 130 + i * lh;
            if (i == m->save_pick) jk_fill(f, dx + 40, yy - 6, dw - 80, lh - 2, JK_RGB_DUSK);
            jk_text(f, JK_FONT_BODY, dx + 60, yy, m->save_names[i], i == m->save_pick ? JK_RGB_INK : JK_RGB_CHALK);
        }
        Hint ph[2] = { { JK_BTN_CROSS, JK_S_H_RENAME }, { JK_BTN_CIRCLE, JK_S_H_CANCEL } };
        hint_bar(f, ph, 2, NULL);
        return;
    }
    Hint h[4] = { { JK_BTN_CROSS, JK_S_H_EXPORT }, { JK_BTN_SQUARE, JK_S_H_IMPORT }, { JK_BTN_TRIANGLE, JK_S_H_RENAME },
                  { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 4, m->build_id);
}

/* ------------------------------------------------------------------ */
/* About (release-signing design 5.6; verification itself comes later) */
/* ------------------------------------------------------------------ */

static int about_row(JkFrame *f, int x, int y, const char *label, const char *value)
{
    jk_text(f, JK_FONT_BODY, x, y, label, JK_RGB_SMOKE);
    jk_text(f, JK_FONT_BODY, x + 230, y, value, JK_RGB_CHALK);
    return y + jk_font_line_height(JK_FONT_BODY) + 14;
}

/* About > Credits: CREDITS.txt, one entry per line ("name<TAB>role<TAB>link"),
 * "# " and "## " headings; D-pad up/down scrolls. */
int jk_credits_lines(const char *text)
{
    int n = 0;
    for (const char *s = text; s && *s; s++) n += *s == '\n';
    return n;
}

static void draw_credits(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_CREDITS);
    panel(f, 150, PANEL_Y, 1660, PANEL_H, JK_RGB_DUSK);
    int x = 194, y = PANEL_Y + 30, bottom = PANEL_Y + PANEL_H - 30, line = 0;
    const char *s = m->credits ? m->credits : JK_S_CR_MISSING;
    while (*s && y < bottom) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char buf[400];
        snprintf(buf, sizeof buf, "%.*s", (int)(len < sizeof buf ? len : sizeof buf - 1), s);
        if (buf[0] && buf[strlen(buf) - 1] == '\r') buf[strlen(buf) - 1] = 0;
        if (line++ >= m->credits_top) {
            if (!strncmp(buf, "## ", 3)) { jk_text(f, JK_FONT_BODY, x, y, buf + 3, JK_RGB_DUSK); y += jk_font_line_height(JK_FONT_BODY) + 2; }
            else if (!strncmp(buf, "# ", 2)) { y += 6; jk_text(f, JK_FONT_HEAD, x, y, buf + 2, JK_RGB_DUSK); y += jk_font_line_height(JK_FONT_HEAD) + 2; }
            else if (!buf[0]) y += 12;
            else {
                char *t1 = strchr(buf, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
                if (t1) *t1 = 0;
                if (t2) *t2 = 0;
                jk_text(f, JK_FONT_HINT, x + 20, y, buf, JK_RGB_CHALK);
                if (t1) jk_text(f, JK_FONT_HINT, x + 560, y, t1 + 1, JK_RGB_SMOKE);
                if (t2) jk_text(f, JK_FONT_HINT, x + 1060, y, t2 + 1, JK_RGB_SMOKE);
                y += jk_font_line_height(JK_FONT_HINT) + 2;
            }
        }
        s += len + (e ? 1 : 0);
    }
    Hint h[2] = { { JK_BTN_UPDOWN, JK_S_H_SCROLL }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 2, m->build_id);
}

static void draw_about(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_ABOUT);
    const JkRelease *r = &m->release;
    const char *head = r->state == JK_REL_OFFICIAL ? JK_S_A_OFFICIAL : r->state == JK_REL_CHANGED ? JK_S_A_CHANGED : JK_S_A_UNOFFICIAL;
    const char *body = r->state == JK_REL_OFFICIAL ? JK_S_A_OFFICIAL_B : r->state == JK_REL_CHANGED ? JK_S_A_CHANGED_B : JK_S_A_UNOFFICIAL_B;
    uint32_t hc = r->state == JK_REL_OFFICIAL ? JK_RGB_OK : JK_RGB_DUSK;
    panel(f, 150, PANEL_Y, 1660, PANEL_H, hc);
    int x = 194, w = 1570;
    jk_status_icon(f, x, PANEL_Y + 40, 40, r->state == JK_REL_OFFICIAL ? JK_ICON_OK : JK_ICON_TODO);
    jk_text(f, JK_FONT_HEAD, x + 60, PANEL_Y + 34, head, hc);
    int y = wrap(f, JK_FONT_BODY, x, PANEL_Y + 110, w, body, JK_RGB_CHALK);
    if (r->state == JK_REL_UNOFFICIAL)
        y = wrap(f, JK_FONT_HINT, x, y, w, r->reason ? r->reason : JK_S_A_NO_SIG, JK_RGB_SMOKE);
    if (r->state == JK_REL_CHANGED && r->changed > 0) {
        char line[64];
        snprintf(line, sizeof line, "%d file%s differ%s from the signed list.", r->changed, r->changed == 1 ? "" : "s",
                 r->changed == 1 ? "s" : "");
        y = wrap(f, JK_FONT_HINT, x, y, w, line, JK_RGB_SMOKE);
    }
    y += 24;
    char ver[96];
    snprintf(ver, sizeof ver, "%s%s%s", m->build_id ? m->build_id : "JK4SPEED",
             r->version ? "  \xC2\xB7  " : "", r->version ? r->version : "");
    y = about_row(f, x, y, JK_S_A_VERSION, ver);
    if (r->state != JK_REL_UNOFFICIAL) {
        char key[128];
        snprintf(key, sizeof key, "%s (ID %s)", r->key_label ? r->key_label : "JK4SPEED release key", r->key_id ? r->key_id : "?");
        y = about_row(f, x, y, JK_S_A_SIGNED, key);
    }
    if (r->state == JK_REL_OFFICIAL) about_row(f, x, y, JK_S_A_OFFICIAL_ROW, JK_S_OFFICIAL_URL);
    else jk_text(f, JK_FONT_BODY, x, y, JK_S_A_GET, JK_RGB_CHALK);
    Hint h[2] = { { JK_BTN_SQUARE, JK_S_H_CREDITS }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 2, m->build_id);
}

/* ------------------------------------------------------------------ */
/* Patch                                                              */
/* ------------------------------------------------------------------ */

JkPatchAction jk_patch_primary(const JkModel *m)
{
    if (!m || m->phase == JK_PH_INSTALLING || jk_two_folders(m)) return JK_ACT_NONE;   /* two folders: no guessing */
    switch (m->check.state) {
    case JK_ST_BAD_PACKAGE: return JK_ACT_NONE;
    case JK_ST_NO_GAME:
    case JK_ST_WRONG_EXE:
    case JK_ST_MISSING_FILES: return JK_ACT_CHECK_AGAIN;
    case JK_ST_READY:                             /* after a failed PATCH, ✕ tries again (its text says so) */
        return m->phase == JK_PH_FAILED && !m->no_pristine ? JK_ACT_INSTALL : JK_ACT_START;
    default: return JK_ACT_INSTALL;
    }
}

int jk_patch_can_restore(const JkModel *m)
{
    return m && m->phase != JK_PH_INSTALLING && m->check.has_backup && m->check.state != JK_ST_NO_GAME && !jk_two_folders(m);
}

int jk_patch_square_help(const JkModel *m)
{
    return m && m->phase != JK_PH_INSTALLING && (jk_two_folders(m) || (m->phase == JK_PH_FAILED && m->no_pristine));
}

int jk_patch_can_mods(const JkModel *m)
{
    if (!m || m->phase == JK_PH_INSTALLING || m->phase == JK_PH_FAILED || jk_two_folders(m) || m->no_mods) return 0;
    switch (m->check.state) {
    case JK_ST_NOT_INSTALLED: case JK_ST_UPDATE: case JK_ST_CHANGED: case JK_ST_READY: return 1;
    default: return 0;
    }
}

void jk_two_folders_body(const JkModel *m, char *out, size_t cap)
{
    if (!out || !cap) return;
    snprintf(out, cap, JK_S_P_TWO_B, m && m->game_dir ? m->game_dir : JK_S_GAME_DIR,
             m && m->game_other ? m->game_other : "?");
}

void jk_patch_failure_body(const JkModel *m, char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (!m) return;
    const char *tail;
    switch (m->fail_kind) {
    case JK_FAIL_SEED: tail = JK_S_P_FAILED_SEED; break;
    case JK_FAIL_MODS: tail = JK_S_P_FAILED_MODS; break;
    case JK_FAIL_PENDING: tail = JK_S_P_FAILED_PENDING; break;
    case JK_FAIL_RESTORE: tail = JK_S_P_FAILED_RESTORE; break;
    case JK_FAIL_PENDING_RESTORE: tail = JK_S_P_FAILED_PENDING_RESTORE; break;
    case JK_FAIL_DAMAGED: tail = JK_S_P_FAILED_DAMAGED; break;
    case JK_FAIL_PREFIX: tail = JK_S_P_FAILED_PREFIX; break;
    default: tail = m->check.has_backup ? JK_S_P_FAILED_CORE_UNDO : JK_S_P_FAILED_B; break;
    }
    size_t n = strlen(m->msg);
    const char *sep = !n ? "" : m->msg[n - 1] == '.' ? " " : ". ";
    snprintf(out, cap, "%s%s%s", m->msg, sep, tail);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}

void jk_no_pristine_body(const JkModel *m, char *out, size_t cap)
{
    if (!out || !cap) return;
    int n = m ? m->mods.npristine_needed : 0, shown = n < 3 ? n : 3;
    size_t l = (size_t)snprintf(out, cap, "%s", JK_S_NP_BODY_A);
    for (int i = 0; i < shown && l < cap; i++)
        l += (size_t)snprintf(out + l, cap - l, "%s%s", i ? ", " : "", m->mods.pristine_needed[i]);
    if (n > shown && l < cap) l += (size_t)snprintf(out + l, cap - l, JK_S_NP_MORE, n - shown);
    if (l < cap) snprintf(out + l, cap - l, "%s", JK_S_NP_BODY_B);
}

/* ------------------------------------------------------------------ */
/* Mods (SETTINGS > MODS): four mods, found or not, ON or OFF          */
/* ------------------------------------------------------------------ */

enum { MODS_ROW_Y0 = PANEL_Y + 84, MODS_ROW_STEP = 62, MODS_ROW_H = 54 };

static void progress_bar(JkFrame *f, const JkModel *m, int x, int y, int w);

const char *jk_mods_row_text(int row)
{
    switch (row) {
    case JK_MS_ROW_ON_INSTALLED: return JK_S_M_ROW_ON_INSTALLED;
    case JK_MS_ROW_ON_PENDING: return JK_S_M_ROW_ON_PENDING;
    case JK_MS_ROW_ON_NO_ARCHIVE: return JK_S_M_ROW_ON_NO_ARCHIVE;
    case JK_MS_ROW_OFF_PENDING: return JK_S_M_ROW_OFF_PENDING;
    case JK_MS_ROW_BAD: return JK_S_M_ROW_BAD;
    default: return JK_S_M_ROW_OFF;
    }
}

/* What the row says (the scan's JK_MS_ROW_*), and its colour. */
static const char *mod_state_text(const JkModel *m, int k, uint32_t *rgb)
{
    int row = m->mods.row[k];
    switch (row) {
    case JK_MS_ROW_ON_INSTALLED: *rgb = JK_RGB_OK; break;
    case JK_MS_ROW_ON_PENDING: case JK_MS_ROW_OFF_PENDING: *rgb = JK_RGB_DUSK; break;
    case JK_MS_ROW_ON_NO_ARCHIVE: case JK_MS_ROW_BAD: *rgb = JK_RGB_RED_TEXT; break;
    default: *rgb = JK_RGB_SMOKE; break;
    }
    return jk_mods_row_text(row);
}

static void draw_mods(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_MODS);
    uint32_t slab = m->mods_state == 1 ? JK_RGB_OK : m->mods_state < 0 ? JK_RGB_SIREN_RED : JK_RGB_SMOKE;
    panel(f, 150, PANEL_Y, 1660, PANEL_H, slab);
    int x = 194, w = 1570;
    wrap(f, JK_FONT_HINT, x, PANEL_Y + 30, w, JK_S_M_BODY, JK_RGB_CHALK);
    if (m->phase == JK_PH_INSTALLING) {
        int y = MODS_ROW_Y0;
        jk_text(f, JK_FONT_BODY, x, y, m->prog_file, JK_RGB_CHALK);
        y += jk_font_line_height(JK_FONT_BODY) + 14;
        progress_bar(f, m, x, y, w);
        wrap(f, JK_FONT_HINT, x, y + 44, w, JK_S_P_PATIENCE, JK_RGB_CHALK);
        hint_text(f, JK_S_H_WAIT, m->build_id);
        return;
    }
    static const char *const names[JK_MOD_N] = { JK_S_M_ROW_X, JK_S_M_ROW_H, JK_S_M_ROW_R };
    static const char *const where[JK_MOD_N] = { JK_S_M_WHERE_X, JK_S_M_WHERE_H, JK_S_M_WHERE_R };
    int lh = jk_font_line_height(JK_FONT_BODY);
    for (int k = 0; k < JK_MOD_N; k++) {
        int y = MODS_ROW_Y0 + k * MODS_ROW_STEP, ty = y + (MODS_ROW_H - lh) / 2;
        int focused = m->mods_focus == k, on = (m->mods.on & JK_MS_BIT(k)) != 0;
        int can = !(m->x360_required && k == JK_MOD_X);  /* r5: the pack is required */
        uint32_t rgb;
        const char *state = mod_state_text(m, k, &rgb);
        if (focused) {
            jk_plate(f, x - 24, y, 12, MODS_ROW_H, JK_RGB_DUSK, 255);
            jk_plate(f, x - 6, y, w + 12, MODS_ROW_H, JK_RGB_ASPHALT, 255);
            jk_siren_bar(f, x - 6, y + MODS_ROW_H + 2, w + 12, 4, m->siren);
        }
        jk_fill(f, x + 4, ty + 12, 14, 14, rgb);
        jk_text(f, JK_FONT_BODY, x + 34, ty, names[k], JK_RGB_CHALK);
        jk_text(f, JK_FONT_BODY, x + 640, ty, state, rgb);
        const char *v = !can ? JK_S_M_REQUIRED : on ? JK_S_M_ON : JK_S_M_OFF;
        uint32_t vink = !can ? JK_RGB_SMOKE : on ? JK_RGB_OK : JK_RGB_CHALK;
        int vx = x + 1330;
        if (focused && can) vx += jk_text(f, JK_FONT_BODY, vx, ty, "\xE2\x80\xB9 ", JK_RGB_CHALK);
        else vx += jk_text_width(JK_FONT_BODY, "\xE2\x80\xB9 ");
        vx += jk_text(f, JK_FONT_BODY, vx, ty, v, vink);
        if (focused && can) jk_text(f, JK_FONT_BODY, vx, ty, " \xE2\x80\xBA", JK_RGB_CHALK);
    }
    /* the focused mod: where to get it, and why it is skipped */
    int k = m->mods_focus >= 0 && m->mods_focus < JK_MOD_N ? m->mods_focus : 0;
    const JkMsMod *md = &m->mods.mod[k];
    int y = MODS_ROW_Y0 + JK_MOD_N * MODS_ROW_STEP + 14;
    jk_text(f, JK_FONT_HINT, x, y, JK_S_M_WHERE_HEAD, JK_RGB_SMOKE);
    y = wrap(f, JK_FONT_HINT, x + 240, y, w - 240, where[k], JK_RGB_CHALK);
    y = wrap(f, JK_FONT_HINT, x + 240, y, w - 240, JK_S_M_PUT, JK_RGB_SMOKE);
    if (md->note[0]) y = wrap(f, JK_FONT_HINT, x + 240, y, w - 240, md->note, md->state == JK_MS_BAD ? JK_RGB_RED_TEXT : JK_RGB_DUSK);
    if (m->mods.unknown) {
        char line[200];
        snprintf(line, sizeof line, JK_S_M_UNKNOWN, m->mods.unknown_name, m->mods.unknown > 1 ? JK_S_M_UNKNOWN_MORE : "");
        y = wrap(f, JK_FONT_HINT, x, y + 4, w, line, JK_RGB_SMOKE);
    }
    /* why the next PATCH would refuse, before it is pressed (the first one) */
    if (m->mods.problems > 0 && m->mods_state >= 0) {
        char text[200];
        snprintf(text, sizeof text, "%s", m->mods.problem[0]);
        if (text[0] >= 'a' && text[0] <= 'z') text[0] = (char)(text[0] - 32);
        y = wrap(f, JK_FONT_HINT, x, y + 4, w, text, JK_RGB_RED_TEXT);
    }
    y += 8;
    if (m->mods_state == 1) {
        char line[200];
        snprintf(line, sizeof line, "%s %d file%s.", JK_S_M_DONE, m->mods_done, m->mods_done == 1 ? "" : "s");
        wrap(f, JK_FONT_BODY, x, y, w, m->mods_done ? line : JK_S_M_SAME, JK_RGB_OK);
    } else if (m->mods_state < 0) {
        char text[sizeof m->mods_msg];
        snprintf(text, sizeof text, "%s", m->mods_msg);
        if (text[0] >= 'a' && text[0] <= 'z') text[0] = (char)(text[0] - 32);
        wrap(f, JK_FONT_BODY, x, y, w, text, JK_RGB_RED_TEXT);
    } else {
        wrap(f, JK_FONT_HINT, x, y, w, m->mods_state == 2 ? JK_S_M_SAVED : JK_S_M_APPLY, m->mods_state == 2 ? JK_RGB_OK : JK_RGB_SMOKE);
    }
    /* △ Help: the reasons above point to Help > Troubleshooting */
    Hint h[5] = { { JK_BTN_CROSS, JK_S_H_TOGGLE }, { JK_BTN_SQUARE, JK_S_H_MODS_INSTALL }, { JK_BTN_TRIANGLE, JK_S_H_HELP },
                  { JK_BTN_UPDOWN, JK_S_H_CHOOSE }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 5, m->build_id);
}


static void check_row(JkFrame *f, int i, JkIcon icon, const char *label, const char *sub)
{
    int y = PANEL_Y + 44 + i * 104;
    jk_status_icon(f, 196, y + 4, 36, icon);
    jk_text(f, JK_FONT_BODY, 256, y - 4, label, JK_RGB_CHALK);
    if (!sub) return;
    /* a long sub (the unknown exe) takes two lines */
    const int maxw = 620;
    char first[160];
    snprintf(first, sizeof first, "%s", sub);
    const char *rest = "";
    while (jk_text_width(JK_FONT_HINT, first) > maxw) {
        char *sp = strrchr(first, ' ');
        if (!sp) break;
        *sp = 0;
        rest = sub + (sp - first) + 1;
    }
    jk_text(f, JK_FONT_HINT, 256, y + 40, first, JK_RGB_SMOKE);
    if (rest[0]) jk_text(f, JK_FONT_HINT, 256, y + 40 + jk_font_line_height(JK_FONT_HINT) - 4, rest, JK_RGB_SMOKE);
}

static void draw_checklist(JkFrame *f, const JkModel *m)
{
    panel(f, 150, PANEL_Y, 740, PANEL_H, JK_RGB_SMOKE);
    JkState s = m->check.state;
    int bad = s == JK_ST_BAD_PACKAGE;
    int found = !bad && s != JK_ST_NO_GAME;
    int version = found && s != JK_ST_WRONG_EXE;
    int files = version && s != JK_ST_MISSING_FILES;
    int done = files && (s == JK_ST_READY || m->phase == JK_PH_DONE);

    if (jk_two_folders(m)) {                      /* nothing past the folder is looked at */
        check_row(f, 0, JK_ICON_BAD, JK_S_K_FOLDER, JK_S_K_TWO);
        check_row(f, 1, JK_ICON_PENDING, JK_S_K_VERSION, JK_S_K_VERSION_SUB);
        check_row(f, 2, JK_ICON_PENDING, JK_S_K_FILES, NULL);
        check_row(f, 3, JK_ICON_PENDING, JK_S_K_OURS, NULL);
        check_row(f, 4, JK_ICON_PENDING, JK_S_K_BACKUP, NULL);
        return;
    }
    check_row(f, 0, found ? JK_ICON_OK : bad ? JK_ICON_PENDING : JK_ICON_BAD, JK_S_K_FOLDER, m->game_dir ? m->game_dir : JK_S_GAME_DIR);
    if (m->exe_any) {                              /* the original game: any speed.exe; v1.3 is told apart */
        int known = version && m->check.exe_known;
        check_row(f, 1, known ? JK_ICON_OK : JK_ICON_PENDING, JK_S_K_VERSION,
                  !version ? JK_S_K_VERSION_SUB : known ? JK_S_K_VERSION_V13 : JK_S_K_VERSION_UNKNOWN);
    } else
    check_row(f, 1, version ? JK_ICON_OK : s == JK_ST_WRONG_EXE ? JK_ICON_BAD : JK_ICON_PENDING, JK_S_K_VERSION,
              s == JK_ST_WRONG_EXE ? JK_S_K_VERSION_BAD : JK_S_K_VERSION_SUB);
    char sub[64];
    if (s == JK_ST_MISSING_FILES) snprintf(sub, sizeof sub, "%d missing", m->check.missing_count);
    else snprintf(sub, sizeof sub, "%s", files ? JK_S_K_FILES_OK : "");
    check_row(f, 2, files ? JK_ICON_OK : s == JK_ST_MISSING_FILES ? JK_ICON_BAD : JK_ICON_PENDING, JK_S_K_FILES, sub[0] ? sub : NULL);
    const char *ours = !files ? NULL
                     : m->phase == JK_PH_INSTALLING ? JK_S_K_INSTALLING
                     : done ? JK_S_K_INSTALLED
                     : s == JK_ST_UPDATE ? JK_S_K_UPDATE
                     : s == JK_ST_CHANGED ? JK_S_K_CHANGED : JK_S_K_NOT_INSTALLED;
    JkIcon oi = !files ? JK_ICON_PENDING : m->phase == JK_PH_INSTALLING ? JK_ICON_ACTIVE
              : m->phase == JK_PH_FAILED && !done ? JK_ICON_BAD : done ? JK_ICON_OK : JK_ICON_TODO;
    check_row(f, 3, oi, JK_S_K_OURS, ours);
    check_row(f, 4, m->check.has_backup ? JK_ICON_OK : JK_ICON_PENDING, JK_S_K_BACKUP,
              m->check.has_backup ? JK_S_K_BACKUP_YES : JK_S_K_BACKUP_NO);
}

static void draw_patch(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_PATCH);
    draw_checklist(f, m);

    const int px = 930, pw = 880, tx = 974, tw = 790;
    const char *head, *body;
    uint32_t hc = JK_RGB_DUSK;
    char text[600] = "";
    JkState s = m->check.state;
    int two = jk_two_folders(m), np = m->phase == JK_PH_FAILED && m->no_pristine;
    if (two) {
        head = JK_S_P_TWO; hc = JK_RGB_SIREN_RED;
        jk_two_folders_body(m, text, sizeof text);
        body = text;
    }
    else if (np) {
        head = JK_S_NP_HEAD; hc = JK_RGB_SIREN_RED;
        jk_no_pristine_body(m, text, sizeof text);
        body = text;
    }
    else if (m->phase == JK_PH_INSTALLING) { head = JK_S_P_INSTALLING; body = NULL; hc = JK_RGB_CHALK; }
    else if (m->phase == JK_PH_DONE) { head = JK_S_P_DONE; body = JK_S_P_DONE_B; hc = JK_RGB_OK; }
    else if (m->phase == JK_PH_FAILED) {
        head = m->fail_kind == JK_FAIL_RESTORE || m->fail_kind == JK_FAIL_PENDING_RESTORE ? JK_S_P_RESTORE_FAILED : JK_S_P_FAILED; hc = JK_RGB_SIREN_RED;
        jk_patch_failure_body(m, text, sizeof text);
        body = text;
    }
    else if (m->phase == JK_PH_RESTORED) { head = JK_S_P_RESTORED; jk_restored_body(m, text, sizeof text); body = text; hc = JK_RGB_OK; }
    else switch (s) {
    case JK_ST_NO_GAME: head = JK_S_P_NO_GAME; body = m->ngames >= 2 && m->active == 1 ? JK_S_P_NO_GAME_B_NFS : JK_S_P_NO_GAME_B; hc = JK_RGB_SIREN_RED; break;
    case JK_ST_WRONG_EXE: head = JK_S_P_WRONG; body = JK_S_P_WRONG_B; hc = JK_RGB_SIREN_RED; break;
    case JK_ST_MISSING_FILES:
        head = JK_S_P_MISSING; hc = JK_RGB_SIREN_RED;
        if (m->check.missing_count > 1)
            snprintf(text, sizeof text, "%s First missing: %s (and %d more).", JK_S_P_MISSING_B, m->check.first_missing, m->check.missing_count - 1);
        else
            snprintf(text, sizeof text, "%s Missing: %s.", JK_S_P_MISSING_B, m->check.first_missing);
        body = text;
        break;
    case JK_ST_UPDATE: head = JK_S_P_UPDATE; body = JK_S_P_UPDATE_B; break;
    case JK_ST_CHANGED: head = JK_S_P_CHANGED; body = JK_S_P_CHANGED_B; break;
    case JK_ST_READY: head = JK_S_P_DONE; body = JK_S_P_INSTALLED_B; hc = JK_RGB_OK; break;
    case JK_ST_BAD_PACKAGE: head = JK_S_P_BAD; body = JK_S_P_BAD_B; hc = JK_RGB_SIREN_RED; break;
    default: head = JK_S_P_READY; body = m->no_mods ? JK_S_P_READY_B_NFS : JK_S_P_READY_B; hc = JK_RGB_CHALK; break;
    }
    panel(f, px, PANEL_Y, pw, PANEL_H, hc == JK_RGB_CHALK ? JK_RGB_SMOKE : hc);
    jk_text(f, JK_FONT_HEAD, tx, PANEL_Y + 40, head, hc);
    int y = PANEL_Y + 120;
    if (body) y = wrap(f, JK_FONT_BODY, tx, y, tw, body, JK_RGB_CHALK);
    if (!two && m->phase == JK_PH_IDLE && s == JK_ST_NO_GAME) {
        jk_fill(f, tx, y + 16, tw, 70, JK_RGB_ASPHALT);
        jk_text(f, JK_FONT_HEAD, tx + 24, y + 16 + (70 - jk_font_line_height(JK_FONT_HEAD)) / 2,
                m->ngames >= 2 && m->active == 1 ? JK_S_GAME_DIR_NFS : JK_S_GAME_DIR, JK_RGB_CHALK);
    }
    if (!two && m->phase == JK_PH_IDLE && s == JK_ST_WRONG_EXE && m->check.found_exe[0]) {
        char found[96];
        snprintf(found, sizeof found, "%s %s\xE2\x80\xA6", JK_S_P_WRONG_FOUND, m->check.found_exe);
        jk_text(f, JK_FONT_HINT, tx, y + 8, found, JK_RGB_SMOKE);
    }
    if (!two && m->phase == JK_PH_IDLE && s == JK_ST_BAD_PACKAGE && m->check.reason[0])
        wrap(f, JK_FONT_HINT, tx, y + 16, tw, m->check.reason, JK_RGB_SMOKE);
    if (m->phase == JK_PH_INSTALLING) {
        int by = PANEL_Y + 250;
        jk_text(f, JK_FONT_BODY, tx, by - 70, m->prog_file, JK_RGB_CHALK);
        progress_bar(f, m, tx, by, tw);
        char count[48];
        if (m->prog_done > 0) snprintf(count, sizeof count, "%d of %d", m->prog_done, m->prog_total);
        else snprintf(count, sizeof count, "%s", JK_S_P_WORKING);
        jk_text(f, JK_FONT_HINT, tx, by + 34, count, JK_RGB_SMOKE);
        wrap(f, JK_FONT_HINT, tx, by + 84, tw, JK_S_P_PATIENCE, JK_RGB_CHALK);
    }

    JkPatchAction a = jk_patch_primary(m);
    const char *alabel = a == JK_ACT_CHECK_AGAIN ? JK_S_H_CHECK
                       : a == JK_ACT_START ? JK_S_H_START
                       : a == JK_ACT_INSTALL ? (m->phase == JK_PH_FAILED ? JK_S_H_TRY : JK_S_H_INSTALL) : NULL;
    if (alabel && !m->dialog) button(f, tx, PANEL_Y + PANEL_H - 116, JK_BTN_CROSS, alabel);

    if (m->dialog == JK_DLG_RESTORE) { dialog(f, JK_S_R_HEAD, JK_S_R_BODY, JK_S_H_CONFIRM); return; }
    if (m->phase == JK_PH_INSTALLING) { hint_text(f, JK_S_H_WAIT, m->build_id); return; }
    Hint h[4];
    int n = 0;
    if (alabel) h[n++] = (Hint){ JK_BTN_CROSS, alabel };
    if (jk_patch_square_help(m)) h[n++] = (Hint){ JK_BTN_SQUARE, JK_S_H_HELP };
    else if (m->phase == JK_PH_FAILED || (m->phase == JK_PH_IDLE && m->check.state == JK_ST_BAD_PACKAGE))
        h[n++] = (Hint){ JK_BTN_SQUARE, JK_S_H_BUNDLE };
    else if (jk_patch_can_mods(m)) h[n++] = (Hint){ JK_BTN_SQUARE, JK_S_H_MODS };
    if (jk_patch_can_restore(m)) h[n++] = (Hint){ JK_BTN_TRIANGLE, JK_S_H_RESTORE };
    h[n++] = (Hint){ JK_BTN_CIRCLE, JK_S_H_BACK };
    hint_bar(f, h, n, m->build_id);
}

/* ------------------------------------------------------------------ */
/* Support and Start failed                                           */
/* ------------------------------------------------------------------ */

static int bundle_result(JkFrame *f, const JkModel *m, int x, int y, int w)
{
    if (m->bundle_state > 0) {
        jk_text(f, JK_FONT_BODY, x, y, JK_S_S_SAVED, JK_RGB_SMOKE);
        y += jk_font_line_height(JK_FONT_BODY) + 6;
        jk_fill(f, x, y, w, 76, JK_RGB_ASPHALT);
        jk_text(f, JK_FONT_HEAD, x + 24, y + (76 - jk_font_line_height(JK_FONT_HEAD)) / 2, m->bundle_dir, JK_RGB_CHALK);
        y += 100;
        if (m->bundle_usb) { jk_text(f, JK_FONT_BODY, x, y, JK_S_S_USB, JK_RGB_OK); y += jk_font_line_height(JK_FONT_BODY) + 8; }
        else y = wrap(f, JK_FONT_BODY, x, y, w, JK_S_S_FIND, JK_RGB_CHALK);
    } else if (m->bundle_state < 0) {
        y = wrap(f, JK_FONT_BODY, x, y, w, JK_S_S_FAILED, JK_RGB_RED_TEXT);
    }
    return y;
}

/* LOGS & SUPPORT: a short list (Enable logs ‹ON/OFF›, Clear logs, Save
 * support bundle) on a panel, the log summary, the focused row's note and
 * the last result. */
enum { LOG_ROW_Y0 = PANEL_Y + 40, LOG_ROW_STEP = 84, LOG_ROW_H = 66 };

static void draw_logs(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_LOGS);
    uint32_t slab = m->bundle_state > 0 ? JK_RGB_OK : m->bundle_state < 0 || m->logs_msg_bad ? JK_RGB_SIREN_RED
                  : m->logs_on ? JK_RGB_DUSK : JK_RGB_SMOKE;
    panel(f, 150, PANEL_Y, 1660, PANEL_H, slab);
    int x = 194, w = 1570;
    static const char *const labels[JK_LOG_COUNT] = { JK_S_L_ENABLE, JK_S_L_CLEAR, JK_S_L_BUNDLE };
    for (int i = 0; i < JK_LOG_COUNT; i++) {
        const char *v = i == JK_LOG_ENABLE ? (m->logs_on ? JK_S_V_ON : JK_S_V_OFF) : NULL;
        menu_row(f, m, JK_FONT_HEAD, x + 22, LOG_ROW_Y0 + i * LOG_ROW_STEP, LOG_ROW_H, labels[i], m->logs_focus == i, 1, v,
                 x + 22 + 420, m->logs_on, !m->logs_on);
    }
    int y = LOG_ROW_Y0 + JK_LOG_COUNT * LOG_ROW_STEP + 30;      /* clear of the focused row's siren bar */
    char sum[96], size[32];
    if (m->log_bytes < 1024LL * 1024LL) snprintf(size, sizeof size, "%lld KB", m->log_bytes > 0 ? (m->log_bytes + 1023) / 1024 : 0);
    else snprintf(size, sizeof size, "%.1f MB", (double)m->log_bytes / (1024.0 * 1024.0));
    if (m->log_files <= 0) snprintf(sum, sizeof sum, "%s", JK_S_L_SUMMARY_NONE);
    else if (m->log_files == 1) snprintf(sum, sizeof sum, JK_S_L_SUMMARY_ONE, size);
    else snprintf(sum, sizeof sum, JK_S_L_SUMMARY, m->log_files, size);
    jk_text(f, JK_FONT_BODY, x, y, sum, JK_RGB_CHALK);
    y += jk_font_line_height(JK_FONT_BODY) + 14;
    static const char *const notes[JK_LOG_COUNT] = { JK_S_L_D_ENABLE, JK_S_L_D_CLEAR, JK_S_L_D_BUNDLE };
    int k = m->logs_focus >= 0 && m->logs_focus < JK_LOG_COUNT ? m->logs_focus : 0;
    if (m->bundle_state) {
        bundle_result(f, m, x, y, w);                               /* the result says it all: no note */
    } else {
        y = wrap(f, JK_FONT_HINT, x, y, w, notes[k], JK_RGB_SMOKE) + 6;
        if (m->logs_msg[0]) wrap(f, JK_FONT_BODY, x, y, w, m->logs_msg, m->logs_msg_bad ? JK_RGB_RED_TEXT : JK_RGB_OK);
    }
    Hint h[3];
    int n = 0;
    if (k == JK_LOG_ENABLE) h[n++] = (Hint){ JK_BTN_LEFTRIGHT, JK_S_H_CHANGE_FPS };
    else h[n++] = (Hint){ JK_BTN_CROSS, k == JK_LOG_CLEAR ? JK_S_H_CLEAR : m->bundle_state ? JK_S_H_SAVE_AGAIN : JK_S_H_SAVE };
    h[n++] = (Hint){ JK_BTN_UPDOWN, JK_S_H_CHOOSE };
    h[n++] = (Hint){ JK_BTN_CIRCLE, JK_S_H_BACK };
    hint_bar(f, h, n, m->build_id);
}

static void draw_failed(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    panel(f, 150, PANEL_Y - 100, 1660, PANEL_H + 100, JK_RGB_SIREN_RED);
    int x = 194, w = 1100;
    jk_text(f, JK_FONT_HEAD, x, PANEL_Y - 60, JK_S_F_HEAD, JK_RGB_SIREN_RED);
    int y = PANEL_Y + 20;
    if (m->failure) y = wrap(f, JK_FONT_BODY, x, y, w, m->failure, JK_RGB_CHALK);
    if (m->failure_log) {
        int lx = x + jk_text(f, JK_FONT_BODY, x, y, JK_S_F_LOG, JK_RGB_SMOKE) + 12;
        jk_text(f, JK_FONT_BODY, lx, y, m->failure_log, JK_RGB_CHALK);
        y += jk_font_line_height(JK_FONT_BODY) + 8;
    }
    bundle_result(f, m, x, y + 30, w);
    Hint h[3] = { { JK_BTN_CROSS, JK_S_H_MENU }, { JK_BTN_SQUARE, JK_S_H_SAVE_BUNDLE }, { JK_BTN_TRIANGLE, JK_S_H_HELP } };
    hint_bar(f, h, 3, JK_S_FOOTER);
}

/* "Patch was interrupted": once, at the open after a PATCH that was cut off. */
/* The PATCH bar: the counted steps, and before the first one a segment that
 * moves at every progress update (reading, building: minutes of work that
 * are not steps), so it never looks stuck. */
static void progress_bar(JkFrame *f, const JkModel *m, int x, int y, int w)
{
    jk_fill(f, x, y, w, 18, JK_RGB_ASPHALT);
    if (m->prog_total > 0 && m->prog_done > 0) {
        jk_fill(f, x, y, (int)((long long)w * m->prog_done / m->prog_total), 18, JK_RGB_DUSK);
        return;
    }
    int seg = w / 5, span = w - seg;
    int pos = span > 0 ? (int)(((long long)m->prog_ticks * 47) % (2 * span)) : 0;
    if (pos > span) pos = 2 * span - pos;                   /* back and forth */
    jk_fill(f, x + pos, y, seg, 18, JK_RGB_DUSK);
}

/* r5: the one-time setup at open (the Wine prefix), with progress. */
static void draw_setup(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_SETUP_HEAD);
    panel(f, 150, PANEL_Y, 1660, PANEL_H, JK_RGB_DUSK);
    int x = 194, w = 1570;
    int y = wrap(f, JK_FONT_BODY, x, PANEL_Y + 40, w, JK_S_SETUP_BODY, JK_RGB_CHALK) + 30;
    progress_bar(f, m, x, y, w);
    hint_text(f, JK_S_H_WAIT, m->build_id);
}

static void draw_interrupted(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    panel(f, 150, PANEL_Y - 100, 1660, PANEL_H + 100, JK_RGB_DUSK);
    int x = 194, w = 1300;
    jk_status_icon(f, x, PANEL_Y - 54, 40, JK_ICON_TODO);
    jk_text(f, JK_FONT_HEAD, x + 60, PANEL_Y - 60, JK_S_INT_HEAD, JK_RGB_DUSK);
    wrap(f, JK_FONT_BODY, x, PANEL_Y + 20, w, JK_S_INT_BODY, JK_RGB_CHALK);
    int by = PANEL_Y + PANEL_H - 116;
    button(f, x, by, JK_BTN_CROSS, JK_S_H_PATCH_AGAIN);
    int bw = 30 + HINT_GLYPH + 16 + jk_text_width(JK_FONT_HEAD, JK_S_H_PATCH_AGAIN) + 40;
    ghost_button(f, x + bw + 40, by, JK_BTN_CIRCLE, JK_S_H_HOME);
    Hint h[3] = { { JK_BTN_CROSS, JK_S_H_PATCH_AGAIN }, { JK_BTN_SQUARE, JK_S_H_HELP }, { JK_BTN_CIRCLE, JK_S_H_HOME } };
    hint_bar(f, h, 3, JK_S_FOOTER);
}

/* ------------------------------------------------------------------ */
/* Help: four pages (← →), JkFlow's paragraphs                        */
/* ------------------------------------------------------------------ */

typedef enum ParaKind { P_HEAD = 0, P_BODY, P_PATH, P_ITEM } ParaKind;
typedef struct Para { ParaKind kind; const char *text; } Para;

static const Para HELP_FOLDER[] = {
    { P_HEAD, JK_S_HF_HEAD }, { P_BODY, JK_S_HF_1 }, { P_PATH, JK_S_GAME_DIR }, { P_BODY, JK_S_HF_2 },
    { P_PATH, JK_S_HF_EXE }, { P_BODY, JK_S_HF_3 }, { P_BODY, JK_S_HF_4 },
};
static const Para HELP_MODS[] = {
    { P_HEAD, JK_S_HM_HEAD }, { P_BODY, JK_S_HM_1 }, { P_PATH, JK_S_MODS_DIR }, { P_PATH, JK_S_HM_USB },
    { P_BODY, JK_S_HM_2 }, { P_BODY, JK_S_HM_3 }, { P_BODY, JK_S_HM_4 },
};
static const Para HELP_SAVES[] = {
    { P_HEAD, JK_S_HS_HEAD }, { P_BODY, JK_S_HS_1 }, { P_PATH, JK_S_SAVES_ROOT }, { P_BODY, JK_S_HS_2 },
    { P_BODY, JK_S_HS_3 }, { P_BODY, JK_S_HS_4 },
};
static const Para HELP_TROUBLE[] = {
    { P_HEAD, JK_S_HT_HEAD }, { P_ITEM, JK_S_HT_FOLDER }, { P_ITEM, JK_S_HT_TWO }, { P_ITEM, JK_S_HT_VERSION },
    { P_ITEM, JK_S_HT_PRISTINE }, { P_ITEM, JK_S_HT_ARCHIVE }, { P_ITEM, JK_S_HT_INTERRUPTED }, { P_ITEM, JK_S_HT_SPACE },
    { P_ITEM, JK_S_HT_START }, { P_ITEM, JK_S_HT_RESTORE },
};
static const struct { const char *title; const Para *p; int n; } HELP[JK_HELP_COUNT] = {
    { JK_S_HP_FOLDER, HELP_FOLDER, (int)(sizeof HELP_FOLDER / sizeof HELP_FOLDER[0]) },
    { JK_S_HP_MODS, HELP_MODS, (int)(sizeof HELP_MODS / sizeof HELP_MODS[0]) },
    { JK_S_HP_SAVES, HELP_SAVES, (int)(sizeof HELP_SAVES / sizeof HELP_SAVES[0]) },
    { JK_S_HP_TROUBLE, HELP_TROUBLE, (int)(sizeof HELP_TROUBLE / sizeof HELP_TROUBLE[0]) },
};

const char *jk_help_title(int page) { return page >= 0 && page < JK_HELP_COUNT ? HELP[page].title : ""; }

int jk_help_count(int page) { return page >= 0 && page < JK_HELP_COUNT ? HELP[page].n : 0; }

/* A paragraph's words; on the original-game side the folder, saves and troubleshooting texts name that game's folders. */
const char *jk_help_text(const JkModel *m, int page, int i)
{
    if (page < 0 || page >= JK_HELP_COUNT || i < 0 || i >= HELP[page].n) return "";
    const char *t = HELP[page].p[i].text;
    if (!m || m->ngames < 2 || m->active != 1) return t;
    static const char *const swap[][2] = {
        { JK_S_GAME_DIR, JK_S_GAME_DIR_NFS }, { JK_S_HF_1, JK_S_HF_1_NFS }, { JK_S_HF_EXE, JK_S_HF_EXE_NFS },
        { JK_S_HF_4, JK_S_HF_4_NFS }, { JK_S_SAVES_ROOT, JK_S_SAVES_ROOT_NFS }, { JK_S_HT_FOLDER, JK_S_HT_FOLDER_NFS },
        { JK_S_HT_TWO, JK_S_HT_TWO_NFS },
    };
    for (size_t k = 0; k < sizeof swap / sizeof swap[0]; k++)
        if (!strcmp(t, swap[k][0])) return swap[k][1];
    return t;
}

/* One paragraph at y; returns the y below it. */
static int help_para(JkFrame *f, int x, int y, int w, const Para *p, const char *text)
{
    switch (p->kind) {
    case P_HEAD:
        jk_text(f, JK_FONT_HEAD, x, y, text, JK_RGB_DUSK);
        return y + jk_font_line_height(JK_FONT_HEAD) + 12;
    case P_PATH: {
        int h = jk_font_line_height(JK_FONT_BODY) + 10;
        jk_fill(f, x + 30, y - 2, w - 30, h, JK_RGB_ASPHALT);
        jk_text(f, JK_FONT_BODY, x + 50, y + 3, text, JK_RGB_CHALK);
        return y + h + 8;
    }
    case P_ITEM:
        jk_plate(f, x + 6, y + 14, 12, 12, JK_RGB_DUSK, 255);
        return wrap(f, JK_FONT_HINT, x + 36, y, w - 36, text, JK_RGB_CHALK) + 4;
    default:
        return wrap(f, JK_FONT_BODY, x, y, w, text, JK_RGB_CHALK) + 8;
    }
}

static void draw_help(JkFrame *f, const JkModel *m)
{
    chrome(f, m);
    page_title(f, JK_S_HELP);
    int page = m->help_page >= 0 && m->help_page < JK_HELP_COUNT ? m->help_page : 0;
    /* the page tabs, right of the title: the open one on a CHALK plate */
    int tx = 520, ty = TITLE_Y + 14, th = 56;
    for (int i = 0; i < JK_HELP_COUNT; i++) {
        int tw = jk_text_width(JK_FONT_HINT, HELP[i].title);
        if (i == page) {
            jk_plate(f, tx - 18, ty, tw + 40, th, JK_RGB_CHALK, 255);
            jk_siren_bar(f, tx - 20, ty + th + 6, tw + 40, 5, m->siren);
        }
        jk_text(f, JK_FONT_HINT, tx, ty + (th - jk_font_line_height(JK_FONT_HINT)) / 2, HELP[i].title,
                i == page ? JK_RGB_INK : JK_RGB_SMOKE);
        tx += tw + 64;
    }
    panel(f, 150, PANEL_Y, 1660, PANEL_H, JK_RGB_DUSK);
    int x = 194, w = 1570, y = PANEL_Y + 30;
    for (int i = 0; i < HELP[page].n; i++) y = help_para(f, x, y, w, &HELP[page].p[i], jk_help_text(m, page, i));
    Hint h[2] = { { JK_BTN_LEFTRIGHT, JK_S_H_PAGE }, { JK_BTN_CIRCLE, JK_S_H_BACK } };
    hint_bar(f, h, 2, m->build_id);
}

void jk_screen_draw(JkFrame *f, const JkModel *m)
{
    if (!f || !f->pixels || !m) return;
    switch (m->screen) {
    case JK_SCR_PATCH: draw_patch(f, m); break;
    case JK_SCR_SETTINGS: draw_settings(f, m); break;
    case JK_SCR_CONTROLS: draw_controls(f, m); break;
    case JK_SCR_VISUALS: draw_visuals(f, m); break;
    case JK_SCR_SAVES: draw_saves(f, m); break;
    case JK_SCR_LOGS: draw_logs(f, m); break;
    case JK_SCR_HELP: draw_help(f, m); break;
    case JK_SCR_INTERRUPTED: draw_interrupted(f, m); break;
    case JK_SCR_SETUP: draw_setup(f, m); break;
    case JK_SCR_ABOUT: draw_about(f, m); break;
    case JK_SCR_FAILED: draw_failed(f, m); break;
    case JK_SCR_MODS: draw_mods(f, m); break;
    case JK_SCR_CREDITS: draw_credits(f, m); break;
    default: draw_home(f, m); break;
    }
}


void jk_restored_body(const JkModel *m, char *out, size_t cap)
{
    if (!out || !cap) return;
    int n = m ? m->restore_left_n : 0, shown = n < 3 ? n : 3;
    if (!n) { snprintf(out, cap, "%s", JK_S_P_RESTORED_B); return; }
    size_t l = (size_t)snprintf(out, cap, "%s", JK_S_P_RESTORED_LEFT_A);
    for (int i = 0; i < shown && l < cap; i++)
        l += (size_t)snprintf(out + l, cap - l, "%s%s", i ? ", " : "", m->restore_left[i]);
    if (n > shown && l < cap) l += (size_t)snprintf(out + l, cap - l, JK_S_NP_MORE, n - shown);
    if (l < cap) snprintf(out + l, cap - l, "%s", JK_S_P_RESTORED_LEFT_B);
}
