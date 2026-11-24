/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Helpers for the title's JkServices (launcher/title/jk_title_host.inc) that
 * need no console API, so the host tests cover them.
 */
#ifndef JK_HOST_H
#define JK_HOST_H

#include <stddef.h>

/* Free bytes from the kernel's struct statfs (as filled by libkernel's
 * _fstatfs): read only when its version is FreeBSD's 0x20030518 (472 bytes)
 * or 0x20140518 (ino64, 2344 bytes) layout, whose heads are the same;
 * f_bavail * f_bsize. -1 for any other layout, an implausible block size, a
 * negative count, an overflow or a short buffer. (The SDK libc's statvfs does
 * not fill its result; JkFlow measured the same.) */
long long jk_host_statfs_free(const void *buf, size_t len);

/* One way of asking the kernel about the volume holding path. buf (cap
 * bytes) is zeroed by the caller; returns the call's rc and sets *err to the
 * errno right after it. */
typedef int (*JkStatfsFn)(void *ctx, const char *path, void *buf, size_t cap, int *err);

/* How the title asks for free space (jk_title_host.inc). */
typedef struct JkFreeProbe {
    JkStatfsFn fstatfs_dir;   /* open(path, O_RDONLY|O_DIRECTORY) + libkernel _fstatfs */
    JkStatfsFn statfs_path;   /* statfs(path); NULL when the console has none */
    void (*log)(void *ctx, const char *line);
    void *ctx;
    int logged;               /* 1 once the failure line went out, 2 once the ok line did */
} JkFreeProbe;

/* Free bytes of the volume holding path, or -1 (unknown). Tries, in order:
 * fstatfs_dir(path), statfs_path(path), then the same two on the parent
 * folder (never "/": the sandbox root is another volume). A call counts when
 * its rc is >= 0 and jk_host_statfs_free reads its buffer. The first failure
 * in the process logs once
 *   free space probe: call=<kind> rc=<rc> errno=<e> raw=<first 64 bytes hex> path=<p>
 * and the first success after it logs once
 *   free space probe: ok call=<kind> path=<p>
 * (kinds: fstatfs, statfs, fstatfs-parent, statfs-parent). */
long long jk_host_free_probe(JkFreeProbe *p, const char *path);

/* "YYYY-MM-DD_HHMM" (UTC) for a Unix time. 0 ok, <0 for a negative time or a
 * buffer under 16 bytes. No libc time zone code is used. */
int jk_host_stamp(long long unix_seconds, char *out, size_t cap);

/* The first path of the NULL-terminated list that is a folder, or NULL. */
const char *jk_host_first_dir(const char *const *paths);

/* The title's game folder under product_root (/data/Jk4Speed).
 *   2  the devtools override: <root>/devtools/enable exists and the first line
 *      of <root>/devtools/game_dir names an existing folder below
 *      <root>/clean-test/ (no "." or ".." segment, and no part of it, nor
 *      <root>/clean-test/System, a link: lstat); out = that folder.
 *   1  jk_game_dir_find found the folder (out).
 *   0  none (out = <root>/NFSMW).
 *  -1  two spellings (out = the first, other = the second).
 * note gets one line for the session log when there is something to say (the
 * override taken or refused, two spellings), else "". */
int jk_host_game_dir(const char *product_root, char *out, size_t cap, char *other, size_t ocap, char *note,
                     size_t ncap);

/* "Z:" + game_dir with backslashes + "\speed.exe", the path PLAY hands to
 * Wine. 0 ok; <0 for a relative path or a buffer too small. */
int jk_host_game_exe(const char *game_dir, char *out, size_t cap);
/* The launcher's data_root for a jk_host_game_dir result: the devtools
 * clean-test run (found 2) keeps its own state in <root>/clean-test/System
 * (backup, modset, state, cache, the launcher log), every other run uses
 * system (/data/Jk4Speed/System). 0 ok, <0 too long. */
int jk_host_data_root(const char *product_root, int found, const char *system, char *out, size_t cap);

#endif
