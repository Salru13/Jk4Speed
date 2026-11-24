/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The package manifest, jk4speed-files/manifest.txt (spec 8.1):
 *
 *   exe <relpath> <sha256>             the game exe and its only accepted hash
 *   require <relpath>                  a game file or folder that must exist
 *   file <relpath> <sha256> <size>     our file for the game folder
 *   config <relpath> <sha256> <size>   ours, installed only when missing
 *   remove <relpath>                   a conflicting third-party file (backed up)
 *   seed <name.reg> <sha256> <size>    registry pre-seed for <data_root>/registry
 *   profile <name.profile> <sha256> <size>  runtime profile for the profiles folder
 *                                      (/data/prospero-win/profiles)
 *   server dxvknative.prx <sha256> <size>   the native DXVK server for the runtime's
 *                                      override folder (/data/prospero-win/override)
 *   accept <relpath> <sha256>          a second accepted hash for an earlier
 *                                      file line of the same path (the ini's
 *                                      PACE=60 twin); never installed itself
 *
 * '#' starts a comment line. file/config/remove paths must be on the allow-list
 * of non-game names (spec 8.2), so even a wrong manifest cannot touch the game.
 * The pinned mod files (jk_mods.h: HD road/HUD, X360 Stuff Pack) are on the
 * allow-list so their backups can be restored, but a package line naming one
 * is refused: they come only from the player's USB drop folder.
 */
#ifndef JK_MANIFEST_H
#define JK_MANIFEST_H

typedef enum JkEntryKind { JK_M_EXE = 0, JK_M_REQUIRE, JK_M_FILE, JK_M_CONFIG, JK_M_REMOVE, JK_M_SEED,
                           JK_M_PROFILE, JK_M_ACCEPT, JK_M_SERVER } JkEntryKind;

#define JK_MANIFEST_MAX 64
#define JK_PATH_MAX 160

typedef struct JkEntry {
    JkEntryKind kind;
    char path[JK_PATH_MAX];
    char sha[65];            /* "" for require/remove */
    long long size;          /* -1 for exe/require/remove */
} JkEntry;

typedef struct JkManifest {
    JkEntry e[JK_MANIFEST_MAX];
    int n;
    char err[160];           /* "line N: reason" when parsing failed */
} JkManifest;

/* 0 ok; -1 with m->err set. */
int jk_manifest_parse(const char *text, JkManifest *m);
/* 1 when path may be used with kind (relative, no "..", allow-listed). */
int jk_manifest_path_allowed(JkEntryKind kind, const char *path);
/* The exe entry (parse guarantees exactly one). */
const JkEntry *jk_manifest_exe(const JkManifest *m);
/* 1 when sha is the hash of a file/profile/seed line for path or of an accept
 * line for it. */
int jk_manifest_accepts(const JkManifest *m, const char *path, const char *sha);
/* The only hash a pinned mod file (jk_mods.h) may have, or NULL when path is not one. */
const char *jk_manifest_hd_sha(const char *path);

#endif
