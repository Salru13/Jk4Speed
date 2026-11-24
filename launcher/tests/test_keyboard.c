/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The pad-driven keyboard (from the Blur launcher): every key reachable with
 * the D-pad, typing, the alias filter and length, cancel and confirm. */
#include "jk_test.h"
#include "jk_frontend.h"
#include "jk_keyboard.h"
#include "jk_saves.h"

static void every_key_reachable(void)
{
    JkKeyboard kb;
    jk_keyboard_init(&kb, "PLAYER NAME", "", 15, NULL);
    /* BFS over the focus with the four directions */
    static int seen[JK_KB_ROWS][JK_KB_COLS];
    int qr[64], qc[64], head = 0, tail = 0;
    seen[0][0] = 1; qr[tail] = 0; qc[tail++] = 0;
    const uint32_t dirs[4] = { JK_PAD_UP, JK_PAD_DOWN, JK_PAD_LEFT, JK_PAD_RIGHT };
    while (head < tail) {
        int r = qr[head], c = qc[head++];
        for (int d = 0; d < 4; d++) {
            kb.row = r; kb.col = c;
            jk_keyboard_input(&kb, dirs[d]);
            if (!seen[kb.row][kb.col]) { seen[kb.row][kb.col] = 1; qr[tail] = kb.row; qc[tail++] = kb.col; }
        }
    }
    /* every letter row cell, and every bottom key (by its first cell) */
    int cells = 0;
    for (int r = 0; r < JK_KB_ROWS - 1; r++) for (int c = 0; c < JK_KB_COLS; c++) { CHECK(seen[r][c]); cells += seen[r][c]; }
    CHECK_EQ(cells, 40);
    const char *bottom[6] = { "Shift", "#+=", "Space", "Del", "Cancel", "OK" };
    for (int k = 0; k < 6; k++) {
        int found = 0;
        for (int c = 0; c < JK_KB_COLS; c++) {
            if (!seen[JK_KB_ROWS - 1][c]) continue;
            kb.row = JK_KB_ROWS - 1; kb.col = c;
            if (!strcmp(jk_keyboard_focus_label(&kb), bottom[k])) found = 1;
        }
        CHECK(found);
    }
}

/* Moves the focus to the key labelled lab with the D-pad only. */
static int go_to(JkKeyboard *kb, const char *lab)
{
    for (int r = 0; r < JK_KB_ROWS; r++) {
        for (int c = 0; c < JK_KB_COLS * 2; c++) {
            if (!strcmp(jk_keyboard_focus_label(kb), lab)) return 1;
            jk_keyboard_input(kb, JK_PAD_RIGHT);
        }
        jk_keyboard_input(kb, JK_PAD_DOWN);
    }
    return 0;
}

static void type_a_name(void)
{
    JkKeyboard kb;
    jk_keyboard_init(&kb, "PLAYER NAME", "NAME", 15, jk_alias_char);
    CHECK(!strcmp(jk_keyboard_text(&kb), "NAME"));
    for (int i = 0; i < 4; i++) jk_keyboard_input(&kb, JK_PAD_SQUARE);      /* delete */
    CHECK_EQ(kb.len, 0);
    jk_keyboard_input(&kb, JK_PAD_TRIANGLE);                                  /* shift */
    CHECK(go_to(&kb, "J")); jk_keyboard_input(&kb, JK_PAD_CROSS);
    jk_keyboard_input(&kb, JK_PAD_TRIANGLE);
    const char *rest = "oker7";
    for (const char *p = rest; *p; p++) { char s[2] = { *p, 0 }; CHECK(go_to(&kb, s)); jk_keyboard_input(&kb, JK_PAD_CROSS); }
    CHECK(!strcmp(jk_keyboard_text(&kb), "Joker7"));
    /* the alias filter: '@', '.', '_', '-' and Space type nothing */
    const char *refused[5] = { "@", ".", "_", "-", "Space" };
    for (int i = 0; i < 5; i++) { CHECK(go_to(&kb, refused[i])); jk_keyboard_input(&kb, JK_PAD_CROSS); }
    CHECK(!strcmp(jk_keyboard_text(&kb), "Joker7"));
    CHECK(go_to(&kb, "OK"));
    CHECK_EQ(jk_keyboard_input(&kb, JK_PAD_CROSS), JK_KB_DONE);
    CHECK(jk_alias_valid(jk_keyboard_text(&kb)));
}

static void length_cancel_confirm(void)
{
    JkKeyboard kb;
    jk_keyboard_init(&kb, "PLAYER NAME", "NAME", JK_ALIAS_MAX, jk_alias_char);
    CHECK(go_to(&kb, "1"));
    for (int i = 0; i < 40; i++) jk_keyboard_input(&kb, JK_PAD_CROSS);
    CHECK_EQ(kb.len, JK_ALIAS_MAX);                                           /* never longer */
    CHECK_EQ(jk_keyboard_input(&kb, JK_PAD_CIRCLE), JK_KB_CANCEL);
    CHECK(!strcmp(jk_keyboard_text(&kb), "NAME"));                           /* cancel: the old name */
    jk_keyboard_init(&kb, "PLAYER NAME", "Bad Name!", JK_ALIAS_MAX, jk_alias_char);
    CHECK(!strcmp(jk_keyboard_text(&kb), "BadName"));                        /* refused characters dropped */
    CHECK_EQ(jk_keyboard_input(&kb, JK_PAD_OPTIONS), JK_KB_DONE);
}

int main(void)
{
    RUN(every_key_reachable);
    RUN(type_a_name);
    RUN(length_cancel_confirm);
    return jk_test_report("test_keyboard");
}
