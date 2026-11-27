/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * 1.1: the two games share one Wine prefix. Each keeps its own saves because
 * PLAY points the prefix's "Personal" (My Documents) shell folder at the
 * game's saves root before Wine starts. The rules mirror
 * launcher/tools/make_seed.py redirect_personal().
 */
#ifndef JK_PREFIX_H
#define JK_PREFIX_H

#include <stddef.h>

/* Sets "Personal"= in both Explorer\Shell Folders and ...\User Shell Folders
 * of <prefix_dir>/user.reg to win_path (a Windows path with single
 * backslashes, e.g. Z:\data\Jk4Speed\Saves-NFS; it is escaped here). A str(2):
 * prefix stays. Written atomically; nothing else changes.
 * Returns 0 written, 1 already that value (no write), -1 refused: why holds
 * the reason and user.reg is untouched. */
int jk_prefix_set_personal(const char *prefix_dir, const char *win_path, char *why, size_t cap);

/* The original game (1.1): ThirteenAG's Widescreen Fix moves the game's settings key from HKLM
 * to HKCU (jk_prefix.c GAME_KEY). Unless <prefix_dir>/user.reg already holds our key (it
 * carries a "JK4SPEED" marker value), any copy of that key is dropped (the package's prefix
 * seed has a stale one) and ours is appended: the Black Edition's values (bench/nfsmw.reg),
 * 1920x1080 (the packed value the fix saves, 0x87800438) and
 * Install Dir = win_game_dir, so the first start is 1080p. Our key is never changed again (the
 * player's own settings). Other keys stay byte for byte. Written atomically.
 * Returns 0 written, 1 our key is already there (no write), -1 refused (why). */
int jk_prefix_seed_game_key(const char *prefix_dir, const char *win_game_dir, char *why, size_t cap);

#endif
