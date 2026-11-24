/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The launcher's own on-screen keyboard, driven by the pad. Taken from the
 * Blur launcher (Ps4 BLUR - PC project, launcher/src/bl_keyboard.[ch], same
 * authors, same licence), prefix renamed, drawn with jk_ui. The PS4 system
 * keyboard does not work inside our Wine titles (Blur status, item 12: it
 * opens in a corner and nothing can be typed or confirmed), so this one is
 * used instead; the title never calls sceImeDialog.
 *
 * A 10-column grid: digits, three letter rows (or a symbols page), then
 * Shift, #+= / abc, Space, Del, Cancel and OK. D-pad moves (wraps), Cross
 * types the focused key, Square deletes, Triangle toggles Shift, L1/R1 move
 * the text cursor, Circle cancels, Options confirms.
 *
 * JK4SPEED addition: an optional allowed() filter (the alias rules of the
 * save format): keys it refuses are drawn dim and type nothing.
 */
#ifndef JK_KEYBOARD_H
#define JK_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "jk_ui.h"

#define JK_KB_MAX_LEN 31

enum { JK_KB_EDITING = 0, JK_KB_DONE, JK_KB_CANCEL };
enum { JK_KB_ROWS = 5, JK_KB_COLS = 10 };

typedef struct JkKeyboard {
    char title[96];
    char text[JK_KB_MAX_LEN + 1];
    char initial[JK_KB_MAX_LEN + 1];
    int len, cursor, max_len;
    int shift, symbols;
    int row, col;                     /* focused cell; wide keys span cells */
    int result;
    int (*allowed)(int ch);           /* NULL: every printable ASCII character */
} JkKeyboard;

void jk_keyboard_init(JkKeyboard *kb, const char *title, const char *initial, int max_len, int (*allowed)(int ch));
/* pressed: newly pressed JK_PAD_* bits. Returns JK_KB_EDITING, JK_KB_DONE or JK_KB_CANCEL. */
int jk_keyboard_input(JkKeyboard *kb, uint32_t pressed);
/* The entered text; after JK_KB_CANCEL the initial text. */
const char *jk_keyboard_text(const JkKeyboard *kb);
/* Label of the focused key ("a", "Shift", "OK" ...). */
const char *jk_keyboard_focus_label(const JkKeyboard *kb);
const char *jk_keyboard_hints(void);
void jk_keyboard_draw(JkFrame *f, const JkKeyboard *kb);

#endif
