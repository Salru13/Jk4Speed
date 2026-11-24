/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Mods from a USB drop folder (owner request 2026-11-23): the Xbox 360 Stuff
 * Pack files and the HD road/HUD files the engine-1.0 release uses. The
 * package never carries them (they are game data or third-party mods without
 * a licence to redistribute); the player puts them on a USB drive:
 *
 *   <usb>/Jk4Speed/mods/<game-relative path>     e.g. mods/scripts/X360Stuff.asi
 *
 * Each file is accepted only at its pinned release hash (engine-1.0 manifest
 * 2.1/2.2); anything else on the drive is ignored. Installing goes through the
 * patcher: the game's own file is backed up first, every step is in the
 * install record, and PATCH > Restore puts the originals back.
 */
#ifndef JK_MODS_H
#define JK_MODS_H

#include "jk_patcher.h"

#include <stddef.h>

#define JK_MODS_DIR "Jk4Speed/mods"

/* PACK: the file as it comes in the X360 Stuff Pack v4.2 download.
 * BUILD: a Jk4Speed build over the player's game data (HD road/HUD, the sky
 * merge, the rebuilt attributes, the tuned X360Stuff.ini): not on any mod
 * page as-is. */
typedef enum JkModSource { JK_MOD_PACK = 0, JK_MOD_BUILD } JkModSource;

/* delta: for a BUILD file, the JKD1 delta in <files_dir>/deltas/ that makes it
 * from the player's own files (jk_delta.h). Its sources are looked for, by
 * hash, at <game>/<path>, <mods>/<path>, <mods>/original/<path> and
 * <data_root>/backup/<path>. */
typedef struct JkModFile { const char *path, *sha; long long size; JkModSource src; const char *delta; } JkModFile;

/* The release set (29 files). */
const JkModFile *jk_mods_table(int *count);
/* The pinned entry for a game-relative path, or NULL. */
const JkModFile *jk_mods_find(const char *path);

typedef struct JkModsScan {
    int total;        /* entries in the table */
    int found;        /* present in the drop folder */
    int ok;           /* present and at the pinned hash */
    int bad;          /* present at another hash */
    int installed;    /* the game folder already has it at the pinned hash */
    int buildable;    /* not installed, not on the drive, but its delta and all its sources are here */
    char first_bad[160];
} JkModsScan;

/* Hashes the drop folder (mods_dir may be NULL: no USB) and the game folder. */
int jk_mods_scan(const char *mods_dir, const char *game_dir, JkModsScan *out);
/* The same, and also counts the BUILD files that can be made (buildable). */
int jk_mods_scan_paths(const JkPaths *p, const char *mods_dir, JkModsScan *out);
/* Installs every verified file of the drop folder that the game folder does
 * not have yet, then makes each missing BUILD file from its delta and the
 * player's sources (into <data_root>/staging, checked, then installed the
 * same way). *installed (may be NULL) gets the count put in place. 0 ok;
 * <0 with a short reason (no game, wrong version, nothing verified or
 * buildable, a source missing, a write failed: the record then lets Restore
 * undo the rest). */
int jk_mods_install(const JkPaths *p, const char *mods_dir, JkProgress cb, void *ctx, int *installed,
                    char *why, size_t cap);

/* The same over a given table (host tests use their own contents). */
int jk_mods_scan_table(const JkModFile *t, int n, const char *mods_dir, const char *game_dir, JkModsScan *out);
int jk_mods_scan_paths_table(const JkModFile *t, int n, const JkPaths *p, const char *mods_dir, JkModsScan *out);
int jk_mods_install_table(const JkModFile *t, int n, const JkPaths *p, const char *mods_dir, JkProgress cb, void *ctx,
                          int *installed, char *why, size_t cap);

#endif
