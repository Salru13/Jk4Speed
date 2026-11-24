/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Loader shown from START until the game's first frame (spec 7.4): the dusk
 * city, the wordmark, "Starting Most Wanted…" and traffic streaks sliding
 * along the highway. No ring, no percentage. Deterministic for t_ms; loops
 * every 3 s. Meant to be drawn at most every 33 ms.
 */
#ifndef JK_LOADER_H
#define JK_LOADER_H

#include "jk_ui.h"

#include <stdint.h>

void jk_loader_draw(JkFrame *f, uint64_t t_ms);
/* Frees the background cache (about 8 MB); call at the game's first frame. */
void jk_loader_release(void);

#endif
