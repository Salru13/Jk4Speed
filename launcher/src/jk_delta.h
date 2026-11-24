/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * JKD1 binary deltas (written by tools/make_delta.py): our derived files are
 * rebuilt on the console from the player's own game and pack files and the
 * mod authors' own releases. Ops: COPY from a source, ADD our bytes, and JDLZ
 * (compress the bytes of nested COPY/ADD parts with the game's crash-safe
 * JDLZ encoder, the same bytes as the tool's). A delta
 * holds only the bytes that are in none of its sources; it cannot be applied
 * without them. Every source is checked by size and SHA-256 before, the
 * result after, and the result is written through <out>.jk4new + rename.
 */
#ifndef JK_DELTA_H
#define JK_DELTA_H

#include <stddef.h>

#define JK_DELTA_MAX_SRC 255

typedef struct JkDeltaInfo {
    int nsrc;
    char target_sha[65];
    long long target_size;
    char src_sha[JK_DELTA_MAX_SRC][65];
    long long src_size[JK_DELTA_MAX_SRC];
} JkDeltaInfo;

/* The header. 0 ok, <0 not a JKD1 delta. */
int jk_delta_info(const char *delta_path, JkDeltaInfo *out);
/* Rebuilds the target from src[0..n) (in the delta's order). 0 ok; <0 with a
 * short reason, and nothing written at out. */
int jk_delta_apply(const char *delta_path, const char *const *src, int n, const char *out,
                   char *why, size_t cap);

#endif
