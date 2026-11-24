/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The launcher's own on-screen keyboard (see jk_keyboard.h). Taken from the
 * Blur launcher's bl_keyboard.c (same authors, same licence). */
#include "jk_keyboard.h"

#include "jk_frontend.h"

#include <stdio.h>
#include <string.h>

/* Cells: printable ASCII, or one of the special keys below (wide keys repeat). */
enum { K_SHIFT = 1, K_PAGE, K_SPACE, K_DEL, K_CANCEL, K_OK };

static const char letters[4][JK_KB_COLS + 1] = { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm._-" };
static const char symbols[4][JK_KB_COLS + 1] = { "1234567890", "!#$%&*+=?@", ".-_~^()[]/", ":;,'\"<>{}|" };
static const char bottom[JK_KB_COLS] = { K_SHIFT, K_SHIFT, K_PAGE, K_PAGE, K_SPACE, K_SPACE,
                                         K_DEL, K_DEL, K_CANCEL, K_OK };

/* Panel geometry (pixels, relative to the panel). */
enum {
    PW = 1280, PH = 700,
    KX = 46, KY = 214, KW = 108, KH = 70, KPX = 120, KPY = 82,
    FX = 46, FY = 104, FW = PW - 92, FH = 72
};

static int cell(const JkKeyboard *kb, int r, int c)
{
    if (r == JK_KB_ROWS - 1) return bottom[c];
    char ch = (kb->symbols ? symbols : letters)[r][c];
    if (kb->shift && !kb->symbols && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return (unsigned char)ch;
}

static void span(int r, int c, int *first, int *last)
{
    *first = *last = c;
    if (r != JK_KB_ROWS - 1) return;
    while (*first > 0 && bottom[*first - 1] == bottom[c]) (*first)--;
    while (*last < JK_KB_COLS - 1 && bottom[*last + 1] == bottom[c]) (*last)++;
}

static const char *label(const JkKeyboard *kb, int key)
{
    static char one[8][2];
    static int next;
    switch (key) {
    case K_SHIFT: return "Shift";
    case K_PAGE: return kb->symbols ? "abc" : "#+=";
    case K_SPACE: return "Space";
    case K_DEL: return "Del";
    case K_CANCEL: return "Cancel";
    case K_OK: return "OK";
    default:
        next = (next + 1) & 7;
        one[next][0] = (char)key; one[next][1] = 0;
        return one[next];
    }
}

static int printable(int ch) { return ch >= 0x20 && ch < 0x7F; }
static int ok_char(const JkKeyboard *kb, int ch) { return printable(ch) && (!kb->allowed || kb->allowed(ch)); }

void jk_keyboard_init(JkKeyboard *kb, const char *title, const char *initial, int max_len, int (*allowed)(int ch))
{
    memset(kb, 0, sizeof *kb);
    snprintf(kb->title, sizeof kb->title, "%s", title ? title : "");
    kb->allowed = allowed;
    kb->max_len = max_len < 0 ? 0 : max_len > JK_KB_MAX_LEN ? JK_KB_MAX_LEN : max_len;
    for (const char *p = initial ? initial : ""; *p && kb->len < kb->max_len; p++)
        if (ok_char(kb, (unsigned char)*p)) kb->text[kb->len++] = *p;
    memcpy(kb->initial, kb->text, sizeof kb->initial);
    kb->cursor = kb->len;
    kb->result = JK_KB_EDITING;
}

static void insert(JkKeyboard *kb, char ch)
{
    if (kb->len >= kb->max_len || !ok_char(kb, (unsigned char)ch)) return;
    memmove(kb->text + kb->cursor + 1, kb->text + kb->cursor, (size_t)(kb->len - kb->cursor + 1));
    kb->text[kb->cursor++] = ch;
    kb->len++;
}

static void backspace(JkKeyboard *kb)
{
    if (kb->cursor == 0) return;
    memmove(kb->text + kb->cursor - 1, kb->text + kb->cursor, (size_t)(kb->len - kb->cursor + 1));
    kb->cursor--;
    kb->len--;
}

static int finish(JkKeyboard *kb, int result)
{
    if (result == JK_KB_CANCEL) {
        memcpy(kb->text, kb->initial, sizeof kb->text);
        kb->len = kb->cursor = (int)strlen(kb->text);
    }
    return kb->result = result;
}

static int press_key(JkKeyboard *kb)
{
    int key = cell(kb, kb->row, kb->col);
    switch (key) {
    case K_SHIFT: kb->shift = !kb->shift; kb->symbols = 0; break;
    case K_PAGE: kb->symbols = !kb->symbols; break;
    case K_SPACE: insert(kb, ' '); break;
    case K_DEL: backspace(kb); break;
    case K_CANCEL: return finish(kb, JK_KB_CANCEL);
    case K_OK: return finish(kb, JK_KB_DONE);
    default: insert(kb, (char)key); break;
    }
    return JK_KB_EDITING;
}

int jk_keyboard_input(JkKeyboard *kb, uint32_t pressed)
{
    if (kb->result != JK_KB_EDITING) return kb->result;
    if (pressed & JK_PAD_CIRCLE) return finish(kb, JK_KB_CANCEL);
    if (pressed & JK_PAD_OPTIONS) return finish(kb, JK_KB_DONE);
    int first, last;
    if (pressed & JK_PAD_UP) kb->row = (kb->row + JK_KB_ROWS - 1) % JK_KB_ROWS;
    if (pressed & JK_PAD_DOWN) kb->row = (kb->row + 1) % JK_KB_ROWS;
    if (pressed & JK_PAD_LEFT) {
        span(kb->row, kb->col, &first, &last);
        kb->col = (first + JK_KB_COLS - 1) % JK_KB_COLS;
        span(kb->row, kb->col, &first, &last);
        kb->col = first;
    }
    if (pressed & JK_PAD_RIGHT) {
        span(kb->row, kb->col, &first, &last);
        kb->col = (last + 1) % JK_KB_COLS;
    }
    if (pressed & JK_PAD_L1 && kb->cursor > 0) kb->cursor--;
    if (pressed & JK_PAD_R1 && kb->cursor < kb->len) kb->cursor++;
    if (pressed & JK_PAD_TRIANGLE) { kb->shift = !kb->shift; kb->symbols = 0; }
    if (pressed & JK_PAD_SQUARE) backspace(kb);
    if (pressed & JK_PAD_CROSS) return press_key(kb);
    return JK_KB_EDITING;
}

const char *jk_keyboard_text(const JkKeyboard *kb) { return kb->text; }
const char *jk_keyboard_focus_label(const JkKeyboard *kb) { return label(kb, cell(kb, kb->row, kb->col)); }
const char *jk_keyboard_hints(void)
{
    return "Cross type    Square delete    Triangle shift    L1/R1 cursor    Circle cancel    Options OK";
}

/* ---------------- drawing ---------------- */

static void draw_key(JkFrame *f, const JkKeyboard *kb, int ox, int oy, int r, int c, int focused)
{
    int first, last, key = cell(kb, r, c);
    span(r, c, &first, &last);
    int x = ox + KX + first * KPX, y = oy + KY + r * KPY, w = (last - first) * KPX + KW;
    int usable = key < 0x20 || ok_char(kb, key);
    jk_fill(f, x, y, w, KH, focused ? JK_RGB_DUSK : JK_RGB_ASPHALT);
    if (!focused) jk_fill(f, x, y + KH - 4, w, 4, 0x3A3F4A);
    JkFont font = key < 0x20 ? JK_FONT_HINT : JK_FONT_BODY;
    const char *s = label(kb, key);
    uint32_t rgb = focused ? JK_RGB_INK : !usable ? 0x5A606C : key == K_SHIFT && kb->shift ? JK_RGB_DUSK
                 : key == K_OK ? JK_RGB_OK : JK_RGB_CHALK;
    jk_text(f, font, x + (w - jk_text_width(font, s)) / 2, y + (KH - jk_font_line_height(font)) / 2, s, rgb);
}

void jk_keyboard_draw(JkFrame *f, const JkKeyboard *kb)
{
    const int ox = ((int)f->width - PW) / 2, oy = ((int)f->height - PH) / 2;
    jk_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x000000, 170);
    jk_fill(f, ox, oy, PW, PH, JK_RGB_INK);
    jk_fill(f, ox, oy, 6, PH, JK_RGB_DUSK);
    jk_text(f, JK_FONT_HEAD, ox + FX, oy + 30, kb->title, JK_RGB_DUSK);
    jk_fill(f, ox + FX, oy + FY, FW, FH, JK_RGB_ASPHALT);
    for (int r = 0; r < JK_KB_ROWS; r++)
        for (int c = 0; c < JK_KB_COLS; c++) {
            int first, last;
            span(r, c, &first, &last);
            if (first == c) draw_key(f, kb, ox, oy, r, c, r == kb->row && c == (kb->col >= first && kb->col <= last ? c : -1));
        }
    const char *hints = jk_keyboard_hints();
    jk_text(f, JK_FONT_HINT, ox + (PW - jk_text_width(JK_FONT_HINT, hints)) / 2, oy + PH - 62, hints, JK_RGB_SMOKE);
    char before[JK_KB_MAX_LEN + 1];
    memcpy(before, kb->text, (size_t)kb->cursor);
    before[kb->cursor] = 0;
    const int tx = ox + FX + 20, ty = oy + FY + (FH - jk_font_line_height(JK_FONT_BODY)) / 2;
    jk_text(f, JK_FONT_BODY, tx, ty, kb->text, JK_RGB_CHALK);
    jk_fill(f, tx + jk_text_width(JK_FONT_BODY, before) + 1, oy + FY + 14, 3, FH - 28, JK_RGB_DUSK);
    char count[16];
    snprintf(count, sizeof count, "%d/%d", kb->len, kb->max_len);
    jk_text(f, JK_FONT_HINT, ox + FX + FW - 20 - jk_text_width(JK_FONT_HINT, count),
            oy + FY + (FH - jk_font_line_height(JK_FONT_HINT)) / 2, count, JK_RGB_SMOKE);
}
