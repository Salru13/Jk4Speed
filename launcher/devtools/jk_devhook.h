/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Console test hook for the launcher phase (title only, not the launcher
 * core). Off unless <product_root>/devtools/enable (/data/Jk4Speed/devtools;
 * <data_root> when product_root is NULL) exists when the launcher
 * opens; players never have that file, so for them nothing is attached and
 * the services are left exactly as the title filled them.
 *
 * When on, it wraps the title's pad, present, screen and log callbacks and
 * speaks the bench remote-control protocol v1 (the bench title's remote-control runtime; the PC
 * client ps4_control.py), with phase "launcher", in that devtools/ folder:
 *
 *   status.json   {"version":1,"session":..,"phase":"launcher","last_seq":..,"active":true,..}, every second
 *   command.txt   one line from the PC: PWCTRL1 <session> <seq> <VERB> [args]
 *   ack.json      {"version":1,"session":..,"seq":..,"result":..[,"file":..]}
 *
 *   PAD <xinput-mask> <lt> <rt> <lx> <ly> <rx> <ry> <ms>   same payload as the game phase;
 *        the buttons are mapped to the launcher's pad bits and held for <ms>,
 *        merged into what the real pad reports (a press = a new edge)
 *   RELEASE                                                drop a held injection
 *   SNAP                                                   the last presented frame -> snap-<seq>.png
 *   STATE                                                  screen, focus, last messages -> state.json
 *   LAUNCH, CLOSE                                          "wrong_phase" (game phase only)
 */
#ifndef JK_DEVHOOK_H
#define JK_DEVHOOK_H

#include <stdint.h>

#include "jk_frontend.h"

#define JK_DEVHOOK_LINES 16

typedef struct JkDevHook {
    JkServices inner;              /* the title's own callbacks */
    char dir[512];                 /* <product_root>/devtools */
    char session[64];
    const char *result;
    uint32_t seq;
    int enabled;
    uint64_t poll_at, status_at, expires;
    uint32_t inj_held, inj_prev;   /* injected launcher pad bits; previous poll's */
    const JkFrame *frame;          /* the last presented frame (valid during the run) */
    uint32_t frames;
    char screen[16];
    char lines[JK_DEVHOOK_LINES][160];
    unsigned line_n;               /* lines seen (ring index = line_n % JK_DEVHOOK_LINES) */
    char ack_file[64];
} JkDevHook;

/* If <s->product_root>/devtools/enable exists (data_root when product_root is NULL): keep s's callbacks in h->inner,
 * point s at the hook (s->ctx = h) and return 1. Otherwise leave s untouched
 * and return 0. -1: bad arguments. */
int jk_devhook_attach(JkDevHook *h, JkServices *s);

/* XInput button mask (ps4_control.py BUTTONS) -> JK_PAD_* bits. Back (Share)
 * has no launcher meaning and maps to nothing. */
uint32_t jk_devhook_buttons(unsigned xinput);
/* Left stick past half travel -> D-pad bits (ly > 0 is up, as XInput). */
uint32_t jk_devhook_stick(int lx, int ly);

/* The frame as an RGB PNG (stored deflate). 0 ok. */
int jk_devhook_png(const char *path, const JkFrame *f);

#endif
