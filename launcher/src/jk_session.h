/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The "game didn't start" marker (as in JkFlow's bl_session):
 * <data_root>/state/starting is written when PLAY is chosen and deleted at the
 * game's first frame. A marker found when the launcher opens means the last
 * start died before its first picture: the launcher opens on "The game
 * didn't start" and the marker is removed, so it is reported once.
 */
#ifndef JK_SESSION_H
#define JK_SESSION_H

#include <stddef.h>

/* Writes the marker with a one-line note (e.g. "pace=30"). 0 ok. */
int jk_session_starting(const char *data_root, const char *note);
/* The game showed its first frame: the marker goes. */
void jk_session_first_frame(const char *data_root);
/* 1 when a marker was left (note gets its first line, cut to fit; the
 * marker is deleted), 0 when not. */
int jk_session_take_failure(const char *data_root, char *note, size_t cap);

#endif
