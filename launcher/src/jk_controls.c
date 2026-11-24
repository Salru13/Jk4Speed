/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "jk_controls.h"
#include "jk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* per button: the Xidi element, the runtime [input] name, the shown name,
 * the pad bit, and what the button carries while it holds a keyboard
 * function (L1/R1 kept on unused game buttons as in the release Xidi.ini) */
static const struct {
    const char *xidi, *input, *name, *key;
    uint32_t pad;
    const char *idle;
} B[JK_CB_N] = {
    [JK_CB_R2] = { "TriggerRT", "r2", "R2", "r2", 0x200u, "Null" },
    [JK_CB_L2] = { "TriggerLT", "l2", "L2", "l2", 0x100u, "Null" },
    [JK_CB_CROSS] = { "ButtonA", "cross", "Cross", "cross", 0x4000u, "Null" },
    [JK_CB_CIRCLE] = { "ButtonB", "circle", "Circle", "circle", 0x2000u, "Null" },
    [JK_CB_SQUARE] = { "ButtonX", "square", "Square", "square", 0x8000u, "Null" },
    [JK_CB_TRIANGLE] = { "ButtonY", "triangle", "Triangle", "triangle", 0x1000u, "Null" },
    [JK_CB_R1] = { "ButtonRB", "r1", "R1", "r1", 0x800u, "Button(12)" },
    [JK_CB_L1] = { "ButtonLB", "l1", "L1", "l1", 0x400u, "Button(11)" },
    [JK_CB_R3] = { "ButtonRS", "r3", "R3", "r3", 0x4u, "Null" },
    [JK_CB_L3] = { "ButtonLS", "l3", "L3", "l3", 0x2u, "Null" },
};

/* per function: the game's DirectInput button, or a keyboard key */
static const struct { const char *name, *id; int game; const char *key; JkCtlButton def; } A[JK_ACT_N] = {
    [JK_ACT_GAS] = { "Gas", "gas", 1, NULL, JK_CB_R2 },
    [JK_ACT_BRAKE] = { "Brake / reverse", "brake", 3, NULL, JK_CB_L2 },
    [JK_ACT_HANDBRAKE] = { "Handbrake, accept", "handbrake", 2, NULL, JK_CB_CROSS },
    [JK_ACT_BACK] = { "Brake, back", "back", 3, NULL, JK_CB_CIRCLE },
    [JK_ACT_NITRO] = { "Nitrous (N2O)", "nitro", 7, NULL, JK_CB_SQUARE },
    [JK_ACT_SPEEDBREAKER] = { "Speedbreaker", "speedbreaker", 4, NULL, JK_CB_TRIANGLE },
    [JK_ACT_SHIFT_UP] = { "Shift up", "shift_up", 0, "shift", JK_CB_R1 },
    [JK_ACT_SHIFT_DOWN] = { "Shift down", "shift_down", 0, "ctrl", JK_CB_L1 },
    [JK_ACT_CAMERA] = { "Change camera", "camera", 0, "c", JK_CB_R3 },
    [JK_ACT_RESET] = { "Reset car", "reset", 6, NULL, JK_CB_L3 },
};

void jk_controls_default(JkControls *c)
{
    for (int a = 0; a < JK_ACT_N; a++) c->btn[a] = (uint8_t)A[a].def;
}

int jk_controls_is_default(const JkControls *c)
{
    for (int a = 0; a < JK_ACT_N; a++) if (c->btn[a] != A[a].def) return 0;
    return 1;
}

void jk_controls_assign(JkControls *c, JkCtlAction a, JkCtlButton b)
{
    if (!c || a < 0 || a >= JK_ACT_N || b < 0 || b >= JK_CB_N) return;
    for (int o = 0; o < JK_ACT_N; o++)
        if (o != (int)a && c->btn[o] == b) c->btn[o] = JK_CB_NONE;   /* the old holder is left empty */
    c->btn[a] = (uint8_t)b;
}

static int valid(const JkControls *c)
{
    int seen[JK_CB_N] = { 0 };
    for (int a = 0; a < JK_ACT_N; a++) {
        if (c->btn[a] == JK_CB_NONE) continue;
        if (c->btn[a] >= JK_CB_N || seen[c->btn[a]]++) return 0;
    }
    return 1;
}

int jk_controls_load(const char *state_dir, JkControls *out)
{
    if (!out) return -1;
    jk_controls_default(out);
    char path[1024];
    if (!state_dir || jk_fs_join(path, sizeof path, state_dir, "controls.txt") < 0) return 0;
    char *t = jk_fs_read(path, 4096, NULL);
    if (!t) return 0;
    JkControls c;
    int got = 0;
    jk_controls_default(&c);
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
        char fn[32], bn[32];
        if (sscanf(line, "%31s %31s", fn, bn) != 2) continue;
        int a = -1, b = -1;
        for (int i = 0; i < JK_ACT_N; i++) if (!strcmp(fn, A[i].id)) a = i;
        for (int i = 0; i < JK_CB_N; i++) if (!strcmp(bn, B[i].input)) b = i;
        if (!strcmp(bn, "none")) b = JK_CB_NONE;
        if (a < 0 || b < 0) continue;
        c.btn[a] = (uint8_t)b;
        got |= 1 << a;
    }
    free(t);
    if (got == (1 << JK_ACT_N) - 1 && valid(&c)) *out = c;
    return 0;
}

int jk_controls_save(const char *state_dir, const JkControls *c)
{
    char path[1024], buf[512];
    if (!state_dir || !c || !valid(c) || jk_fs_join(path, sizeof path, state_dir, "controls.txt") < 0) return -1;
    size_t n = 0;
    for (int a = 0; a < JK_ACT_N; a++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s\n", A[a].id,
                              c->btn[a] == JK_CB_NONE ? "none" : B[c->btn[a]].input);
    if (jk_fs_mkdir_p(state_dir) < 0) return -1;
    return jk_fs_write_atomic(path, buf, n);
}

/* the function on button b, or -1 */
static int holder(const JkControls *c, int b)
{
    for (int a = 0; a < JK_ACT_N; a++) if (c->btn[a] == b) return a;
    return -1;
}

int jk_controls_xidi_lines(const JkControls *c, char *out, size_t cap)
{
    size_t n = 0;
    for (int b = 0; b < JK_CB_N; b++) {
        int a = holder(c, b);
        char v[16];
        if (a >= 0 && A[a].game) snprintf(v, sizeof v, "Button(%d)", A[a].game);
        else snprintf(v, sizeof v, "%s", B[b].idle);
        int k = snprintf(out + n, cap > n ? cap - n : 0, "%s = %s\n", B[b].xidi, v);
        if (k < 0 || n + (size_t)k >= cap) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

int jk_controls_input_lines(const JkControls *c, char *out, size_t cap)
{
    static const JkCtlButton order[JK_CB_N] = { JK_CB_L1, JK_CB_R1, JK_CB_R3, JK_CB_L3, JK_CB_R2, JK_CB_L2,
                                                JK_CB_CROSS, JK_CB_CIRCLE, JK_CB_SQUARE, JK_CB_TRIANGLE };
    size_t n = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < JK_CB_N; i++) {
        int b = order[i], a = holder(c, b);
        if (a < 0 || !A[a].key) continue;
        int k = snprintf(out + n, cap > n ? cap - n : 0, "%s = %s\n", B[b].input, A[a].key);
        if (k < 0 || n + (size_t)k >= cap) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

int jk_controls_button_from_pad(uint32_t pad_bit)
{
    for (int b = 0; b < JK_CB_N; b++) if (B[b].pad == pad_bit) return b;
    return -1;
}

const char *jk_controls_button_name(JkCtlButton b) { return b >= 0 && b < JK_CB_N ? B[b].name : "â"; }
const char *jk_controls_action_name(JkCtlAction a) { return a >= 0 && a < JK_ACT_N ? A[a].name : "?"; }
