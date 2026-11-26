/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* SETTINGS > CONTROLS remap (jk_controls.h): 10 functions on 10 buttons. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_controls.h"

#include <stdio.h>
#include <string.h>

static void defaults_are_the_owner_layout(void)
{
    JkControls c;
    jk_controls_default(&c);
    CHECK_EQ(c.btn[JK_ACT_GAS], JK_CB_R2);
    CHECK_EQ(c.btn[JK_ACT_BRAKE], JK_CB_L2);
    CHECK_EQ(c.btn[JK_ACT_HANDBRAKE], JK_CB_CROSS);
    CHECK_EQ(c.btn[JK_ACT_BACK], JK_CB_CIRCLE);
    CHECK_EQ(c.btn[JK_ACT_NITRO], JK_CB_SQUARE);
    CHECK_EQ(c.btn[JK_ACT_SPEEDBREAKER], JK_CB_TRIANGLE);
    CHECK_EQ(c.btn[JK_ACT_SHIFT_UP], JK_CB_R1);
    CHECK_EQ(c.btn[JK_ACT_SHIFT_DOWN], JK_CB_L1);
    CHECK_EQ(c.btn[JK_ACT_CAMERA], JK_CB_R3);
    CHECK_EQ(c.btn[JK_ACT_RESET], JK_CB_L3);
    CHECK_EQ(c.btn[JK_ACT_CHALLENGE], JK_CB_TOUCHPAD);         /* keyboard "2": challenge a rival */
    CHECK(jk_controls_is_default(&c));
}

/* Challenge rival = keyboard key 2 (the game has no pad button for it). The
 * touchpad has no Xidi element: it carries only keyboard functions. */
static void challenge_is_key_two_on_touchpad(void)
{
    JkControls c;
    jk_controls_default(&c);
    char x[1024], in[256];
    CHECK(jk_controls_input_lines(&c, in, sizeof in) > 0);
    CHECK(strstr(in, "touchpad = 2\n") != NULL);
    CHECK(jk_controls_xidi_lines(&c, x, sizeof x) > 0);
    CHECK(!strstr(x, "touchpad") && !strstr(x, "(null)"));
    CHECK_EQ(jk_controls_assign(&c, JK_ACT_GAS, JK_CB_TOUCHPAD), -1);   /* a game button: refused */
    CHECK_EQ(c.btn[JK_ACT_GAS], JK_CB_R2);
    CHECK_EQ(c.btn[JK_ACT_CHALLENGE], JK_CB_TOUCHPAD);
    CHECK_EQ(jk_controls_assign(&c, JK_ACT_CAMERA, JK_CB_TOUCHPAD), 0);  /* a key function: taken */
    CHECK_EQ(c.btn[JK_ACT_CHALLENGE], JK_CB_NONE);
    CHECK_EQ(jk_controls_assign(&c, JK_ACT_CHALLENGE, JK_CB_SQUARE), 0); /* key 2 on square: nitro empty */
    CHECK_EQ(c.btn[JK_ACT_NITRO], JK_CB_NONE);
    jk_controls_input_lines(&c, in, sizeof in);
    jk_controls_xidi_lines(&c, x, sizeof x);
    CHECK(strstr(in, "square = 2\n") && strstr(in, "touchpad = c\n"));
    CHECK(strstr(x, "ButtonX = Null\n") != NULL);
    CHECK_EQ(jk_controls_button_from_pad(0x100000u), JK_CB_TOUCHPAD);
    CHECK(!strcmp(jk_controls_button_name(JK_CB_TOUCHPAD), "Touchpad"));
}

/* A 1.00 controls.txt has no "challenge" line: the player's layout is kept
 * and challenge gets its default (the touchpad, which 1.00 could not use). */
static void old_file_without_challenge_keeps_layout(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    tmp_put(d, "controls.txt", "gas cross\nbrake l2\nhandbrake none\nback circle\nnitro square\n"
                               "speedbreaker triangle\nshift_up r1\nshift_down l1\ncamera r3\nreset l3\n");
    JkControls r;
    CHECK_EQ(jk_controls_load(d, &r), 0);
    CHECK_EQ(r.btn[JK_ACT_GAS], JK_CB_CROSS);
    CHECK_EQ(r.btn[JK_ACT_HANDBRAKE], JK_CB_NONE);
    CHECK_EQ(r.btn[JK_ACT_CHALLENGE], JK_CB_TOUCHPAD);
    tmp_rm_rf(d);
}

/* the default must write exactly today's mapping (Xidi.ini 3c03a044, profile [input]) */
static void default_output_equals_release_files(void)
{
    JkControls c;
    jk_controls_default(&c);
    char x[1024], in[256];
    CHECK(jk_controls_xidi_lines(&c, x, sizeof x) > 0);
    CHECK(strstr(x, "TriggerRT = Button(1)\n") && strstr(x, "TriggerLT = Button(3)\n"));
    CHECK(strstr(x, "ButtonA = Button(2)\n") && strstr(x, "ButtonB = Button(3)\n"));
    CHECK(strstr(x, "ButtonX = Button(7)\n") && strstr(x, "ButtonY = Button(4)\n"));
    CHECK(strstr(x, "ButtonRS = Null\n") && strstr(x, "ButtonLS = Button(6)\n"));
    CHECK(strstr(x, "ButtonLB = Button(11)\n") && strstr(x, "ButtonRB = Button(12)\n"));
    CHECK(jk_controls_input_lines(&c, in, sizeof in) > 0);
    CHECK(!strcmp(in, "l1 = ctrl\nr1 = shift\nr3 = c\ntouchpad = 2\n"));
}

static void assign_leaves_the_old_owner_empty(void)
{
    JkControls c;
    jk_controls_default(&c);
    jk_controls_assign(&c, JK_ACT_GAS, JK_CB_CROSS);          /* gas on cross: handbrake left empty */
    CHECK_EQ(c.btn[JK_ACT_GAS], JK_CB_CROSS);
    CHECK_EQ(c.btn[JK_ACT_HANDBRAKE], JK_CB_NONE);
    CHECK(!jk_controls_is_default(&c));
    char x[1024], in[256];
    jk_controls_xidi_lines(&c, x, sizeof x);
    CHECK(strstr(x, "ButtonA = Button(1)\n") && strstr(x, "TriggerRT = Null\n"));    /* R2 now free */
    jk_controls_assign(&c, JK_ACT_SHIFT_UP, JK_CB_SQUARE);    /* a key function on square: nitro empty */
    jk_controls_xidi_lines(&c, x, sizeof x);
    jk_controls_input_lines(&c, in, sizeof in);
    CHECK_EQ(c.btn[JK_ACT_NITRO], JK_CB_NONE);
    CHECK(strstr(x, "ButtonX = Null\n") != NULL);             /* square: only the key now */
    CHECK(strstr(x, "ButtonRB = Button(12)\n") != NULL);       /* R1 free: its idle value */
    CHECK(strstr(in, "square = shift\n") && !strstr(in, "r1 = "));
    jk_controls_assign(&c, JK_ACT_GAS, JK_CB_CROSS);          /* already there: nothing changes */
    CHECK_EQ(c.btn[JK_ACT_GAS], JK_CB_CROSS);
}

static void save_load_roundtrip_and_bad_file(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    JkControls c, r;
    jk_controls_default(&c);
    jk_controls_assign(&c, JK_ACT_NITRO, JK_CB_L1);           /* shift down left empty: saved as none */
    CHECK_EQ(jk_controls_save(d, &c), 0);
    CHECK_EQ(jk_controls_load(d, &r), 0);
    CHECK(!memcmp(&c, &r, sizeof c));
    tmp_put(d, "controls.txt", "gas r2\nbrake r2\n");        /* not a bijection: defaults */
    jk_controls_load(d, &r);
    CHECK(jk_controls_is_default(&r));
    tmp_rm_rf(d);
    jk_controls_load("/nonexistent-dir", &r);                 /* missing: defaults */
    CHECK(jk_controls_is_default(&r));
}

static void button_from_pad_and_names(void)
{
    CHECK_EQ(jk_controls_button_from_pad(0x200u), JK_CB_R2);
    CHECK_EQ(jk_controls_button_from_pad(0x100u), JK_CB_L2);
    CHECK_EQ(jk_controls_button_from_pad(0x4000u), JK_CB_CROSS);
    CHECK_EQ(jk_controls_button_from_pad(0x8u), -1);           /* Options: fixed (pause) */
    CHECK_EQ(jk_controls_button_from_pad(0x10u), -1);          /* D-pad: menus */
    CHECK(!strcmp(jk_controls_button_name(JK_CB_L3), "L3"));
    CHECK(!strcmp(jk_controls_action_name(JK_ACT_SHIFT_UP), "Shift up"));
}

int main(void)
{
    RUN(defaults_are_the_owner_layout);
    RUN(default_output_equals_release_files);
    RUN(assign_leaves_the_old_owner_empty);
    RUN(save_load_roundtrip_and_bad_file);
    RUN(button_from_pad_and_names);
    RUN(challenge_is_key_two_on_touchpad);
    RUN(old_file_without_challenge_keeps_layout);
    return jk_test_report("test_controls");
}
