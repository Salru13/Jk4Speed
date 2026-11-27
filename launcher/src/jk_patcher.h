/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The patcher (spec 8): checks the player's game folder against the
 * package manifest and installs, verifies and restores our files.
 *
 * Folders:
 *   game_dir     /data/Jk4Speed/NFSMW-X360     the player's copy of the game
 *   data_root    /data/Jk4Speed                backup/, state/, registry/
 *   files_dir    the package's jk4speed-files/ manifest.txt and our files
 *   profiles_dir /data/prospero-win/profiles   the runtime profile ("profile" lines)
 *
 * Writes only what the manifest allow-list permits (jk_manifest.h): our own
 * file names, the runtime profile and, by owner decision D3, the four HD
 * road/HUD files at their release hashes. speed.exe and every other game file
 * are never written. Anything replaced or removed is first copied to
 * <data_root>/backup/<path> (profiles: backup/profiles/<name>; a backup that
 * exists is kept), and each step is recorded in <data_root>/state/installed.txt
 * before the target changes, so an interrupted install can be finished or
 * restored. A file is "ours" at its manifest hash or at an "accept" hash (the
 * ini's PACE=60 twin).
 */
#ifndef JK_PATCHER_H
#define JK_PATCHER_H

#include <stddef.h>

typedef enum JkState {
    JK_ST_NO_GAME = 0,      /* no game folder, or no exe in it */
    JK_ST_WRONG_EXE,        /* the exe is not the supported build */
    JK_ST_MISSING_FILES,    /* a "require" path is missing */
    JK_ST_NOT_INSTALLED,    /* our files are not (all) there, no install record */
    JK_ST_UPDATE,           /* installed by an older package */
    JK_ST_CHANGED,          /* one of our files was replaced by something else */
    JK_ST_READY,
    JK_ST_BAD_PACKAGE       /* the package manifest is missing or invalid */
} JkState;

/* override_dir: /data/prospero-win/override ("server" lines; shared with every
 * title on the console, so a file found there is always backed up first). */
typedef struct JkPaths {
    const char *game_dir, *data_root, *files_dir, *profiles_dir, *override_dir;
    /* 1.1 (JkGameSlot): exe_any 1 accepts any speed.exe (no hash lock; a
     * missing one is still NO_GAME). profile_key names the composed profile
     * (<key>.profile); NULL: "nfsmw". Zero keeps the 1.0 behaviour. */
    int exe_any;
    const char *profile_key;
} JkPaths;

typedef struct JkCheck {
    JkState state;
    char found_exe[17];        /* WRONG_EXE: the first 8 hex of the exe's sha256 */
    char first_missing[160];   /* MISSING_FILES: the first missing path */
    int missing_count;
    int exe_known;             /* speed.exe is the known v1.3 (PE timestamp 0x438e4c8c, image base 0x400000) */
    int has_backup;            /* an install record exists: Restore is possible */
    char reason[160];          /* BAD_PACKAGE: what is wrong (never a full path) */
} JkCheck;

/* 1 when path is a PE file of the known v1.3 build: TimeDateStamp
 * 0x438e4c8c and ImageBase 0x400000. Anything else, unreadable or not a PE
 * file: 0. Never crashes on a garbage file. */
int jk_exe_is_known_v13(const char *path);

/* Hashes the exe and our files (well under a second for this game). 0 ok. */
int jk_patch_check(const JkPaths *p, JkCheck *out);

/* Progress: done of total entries; file is the entry's path. */
typedef void (*JkProgress)(void *ctx, int done, int total, const char *file);

/* Installs and verifies. Refused for NO_GAME, WRONG_EXE, MISSING_FILES,
 * BAD_PACKAGE, and before any write when the volume lacks room for the new
 * files plus the backups (when the host reports free space). 0 ok; <0 with a
 * short reason (no full paths). */
int jk_patch_install(const JkPaths *p, JkProgress cb, void *ctx, char *reason, size_t cap);

/* Installs the given "file" entries (allow-listed paths, e.g. the pinned mod
 * files of jk_mods.h) from src_dir, with the same checks, free-space test,
 * backups and install record as jk_patch_install, so Restore undoes them too.
 * The package's exe line still decides the supported game. 0 ok. */
struct JkEntry;
int jk_patch_install_entries(const JkPaths *p, const struct JkEntry *files, int n, const char *src_dir,
                             JkProgress cb, void *ctx, char *reason, size_t cap);

/* Puts the folder back as it was before the first install: backed-up files
 * return, files we added are removed when they are still ours, the registry
 * seed is removed, a foreign
 * replacement of our file is kept as backup/<path>.changed, and the record is
 * deleted. A missing backup is reported (<0) after everything else is back.
 * The composed ini and profile are deleted only while they have the hash the
 * launcher last wrote (state/composed.txt), and the player's own ini that the
 * first compose backed up comes back. A record written for another game
 * folder is never acted on (<0, nothing changed); an install over it is
 * refused while that folder exists, else its record and backups are kept
 * aside in backup/kept first. */
int jk_patch_restore(const JkPaths *p, char *reason, size_t cap);

/* An install record for p->game_dir exists (the record names the game folder
 * it was written for; one without that line counts as this folder's). The
 * launcher composes the ini and the profile only then. */
int jk_patch_has_record(const JkPaths *p);

/* Lists name in <profiles_dir>/profiles.lst when that file exists and does
 * not list it yet (never a second line). 0 ok (or no list). Install calls it
 * for the package's profile; Restore takes out only a line it added. */
int jk_profiles_list_ensure(const JkPaths *p, const char *name);

#endif
