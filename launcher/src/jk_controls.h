/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * SETTINGS > CONTROLS remap. Ten driving functions, each on one DS4 button
 * or on none. Assigning a function a button another function holds leaves
 * that other function empty (the owner's rule: no swap, the player sees it
 * and gives it a new button). Fixed, never remapped: Options = pause, right
 * stick back = look behind, left stick = steer, D-pad = menus.
 *
 * Saved as <state_dir>/controls.txt ("<function> <button>" lines). The game
 * gets it through Xidi.ini (the CustomMapper lines: a button -> the
 * DirectInput Button(n) the game reads) and the runtime profile's [input]
 * keyboard binds (Shift up = shift, Shift down = ctrl, Camera = c).
 */
#ifndef JK_CONTROLS_H
#define JK_CONTROLS_H

#include <stddef.h>
#include <stdint.h>

typedef enum JkCtlAction {
    JK_ACT_GAS = 0, JK_ACT_BRAKE, JK_ACT_HANDBRAKE, JK_ACT_BACK, JK_ACT_NITRO, JK_ACT_SPEEDBREAKER,
    JK_ACT_SHIFT_UP, JK_ACT_SHIFT_DOWN, JK_ACT_CAMERA, JK_ACT_RESET, JK_ACT_N
} JkCtlAction;

typedef enum JkCtlButton {
    JK_CB_R2 = 0, JK_CB_L2, JK_CB_CROSS, JK_CB_CIRCLE, JK_CB_SQUARE, JK_CB_TRIANGLE,
    JK_CB_R1, JK_CB_L1, JK_CB_R3, JK_CB_L3, JK_CB_N,
    JK_CB_NONE = 0xFF                 /* a function with no button */
} JkCtlButton;

typedef struct JkControls { uint8_t btn[JK_ACT_N]; } JkControls;   /* function -> button */

void jk_controls_default(JkControls *c);
int jk_controls_is_default(const JkControls *c);
/* Function a gets button b; a function that held b is left with JK_CB_NONE. */
void jk_controls_assign(JkControls *c, JkCtlAction a, JkCtlButton b);

/* <state_dir>/controls.txt. A missing or damaged file (not one button per
 * function, "none" allowed) gives the defaults. 0 ok (defaults included), -1 bad args. */
int jk_controls_load(const char *state_dir, JkControls *out);
int jk_controls_save(const char *state_dir, const JkControls *c);

/* The 10 Xidi CustomMapper lines ("TriggerRT = Button(1)\n" ...). Returns
 * the length, <0 when cap is too small. */
int jk_controls_xidi_lines(const JkControls *c, char *out, size_t cap);
/* The profile [input] key lines in default order of the keys
 * ("l1 = ctrl\nr1 = shift\nr3 = c\n" for the defaults). Returns the length. */
int jk_controls_input_lines(const JkControls *c, char *out, size_t cap);

/* A launcher pad bit (jk_frontend.h JK_PAD_*, L2 0x100, R2 0x200) -> button,
 * or -1 for a button that cannot be remapped (Options, D-pad, sticks). */
int jk_controls_button_from_pad(uint32_t pad_bit);
const char *jk_controls_button_name(JkCtlButton b);     /* "R2", "Cross" ... */
const char *jk_controls_action_name(JkCtlAction a);        /* "Gas", "Shift up" ... */

#endif
