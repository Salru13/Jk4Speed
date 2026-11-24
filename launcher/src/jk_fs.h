/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * File helpers for the patcher, the look setting and the support bundle.
 * POSIX only (open/rename/mkdir/stat), so host tests run on temp folders.
 * Every write goes to "<path>.jk4new" first and is renamed over the target
 * only when complete (and, where a hash is given, verified).
 */
#ifndef JK_FS_H
#define JK_FS_H

#include <stddef.h>

/* out = a + "/" + b. 0 ok, -1 when it does not fit. */
int jk_fs_join(char *out, size_t cap, const char *a, const char *b);
/* 1 when path exists (file or folder). */
int jk_fs_exists(const char *path);
int jk_fs_is_dir(const char *path);
/* Size in bytes, -1 when missing. */
long long jk_fs_size(const char *path);
/* mkdir -p of path itself / of its parent folder. 0 ok. */
int jk_fs_mkdir_p(const char *path);
int jk_fs_mkdir_parent(const char *path);
/* Whole file into a malloc'd NUL-terminated buffer, at most max bytes
 * (larger files fail). NULL on error. *len may be NULL. */
char *jk_fs_read(const char *path, size_t max, size_t *len);
/* Write via "<path>.jk4new" + rename. Creates no folders. The temp file is
 * created fresh (O_EXCL | O_NOFOLLOW); the folder is synced after the rename.
 * 0 ok. */
int jk_fs_write_atomic(const char *path, const void *data, size_t n);
/* sha256 of a file as 64 lower-case hex digits. 0 ok, <0 missing/error. */
int jk_fs_sha256(const char *path, char out[65], long long *size);
/* r5: called about every 8 MB while jk_fs_sha256 / jk_fs_copy_checked read a
 * file (done = bytes so far), so a long PATCH can show it is still working.
 * NULL: off. */
extern void (*jk_fs_tick_fn)(void *ctx, long long done);
extern void *jk_fs_tick_ctx;
/* Copy src to dst (parent folders created) through "<dst>.jk4new"; the copy
 * is hashed and must equal want_sha (64 hex; NULL: the source's own hash)
 * before it is renamed over dst. On any failure dst is unchanged and no temp
 * file is left. 0 ok. */
int jk_fs_copy_checked(const char *src, const char *dst, const char *want_sha);
/* Copy the last max_bytes of src to dst (plain, for logs). 0 ok. */
int jk_fs_copy_tail(const char *src, const char *dst, long long max_bytes);
/* 0 when base/rel can be written without following a link: no existing
 * component after base is a symlink, every existing middle component is a
 * folder and the target, if it exists, is a regular file. -1 otherwise. */
int jk_fs_safe_path(const char *base, const char *rel);
/* Delete a file. 0 ok or already gone. */
int jk_fs_remove(const char *path);
/* Rename (same volume). 0 ok. */
int jk_fs_rename(const char *from, const char *to);

/* Fault injection for the install tests: when >= 0, the write that would be
 * number jk_fs_fail_after + 1 fails as if the disk were full, and the counter
 * stops. -1 (default): off. Only the host tests set it; the title never does. */
extern int jk_fs_fail_after;
/* Power-cut injection for the mod-set tests: when >= 0, rename number
 * jk_fs_cut_after + 1 ends the process (_exit 77) before it happens, as a
 * power loss would. -1 (default): off. Set by host tests only. */
extern int jk_fs_cut_after;

/* Free bytes on the volume holding path, -1 when unknown. Asks the host
 * (JkServices.free_bytes, set by the frontend; the same pattern as JkFlow's
 * bl_free_bytes): the console's libc is not assumed to have statvfs. */
long long jk_fs_free_bytes(const char *path);
extern long long (*jk_fs_free_fn)(void *ctx, const char *path);
extern void *jk_fs_free_ctx;

#endif
