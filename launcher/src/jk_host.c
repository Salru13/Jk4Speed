/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host helpers. See jk_host.h. */
#include "jk_host.h"
#include "jk_fs.h"
#include "jk_layout.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STATFS_VERSION 0x20030518u         /* FreeBSD 5..11: 472 bytes */
#define STATFS_VERSION_INO64 0x20140518u   /* FreeBSD 12+: 2344 bytes, same head */

long long jk_host_statfs_free(const void *buf, size_t len)
{
    struct { uint32_t version, type; uint64_t flags, bsize, iosize, blocks, bfree; int64_t bavail; } h;
    if (!buf || len < sizeof h) return -1;
    memcpy(&h, buf, sizeof h);
    if ((h.version != STATFS_VERSION && h.version != STATFS_VERSION_INO64) || h.bsize < 512 || h.bsize > (1u << 20) || h.bavail < 0
        || (uint64_t)h.bavail > (uint64_t)INT64_MAX / h.bsize)
        return -1;
    return (long long)h.bavail * (long long)h.bsize;
}

/* One call of the chain: -1 unknown, else the free bytes. Logs the first
 * failure (and the first success after it) once per probe. */
static long long free_try(JkFreeProbe *p, JkStatfsFn fn, const char *kind, const char *path)
{
    /* 4 KiB: room for the 472-byte FreeBSD 9 struct and the 2344-byte ino64 one. */
    static union { uint8_t raw[4096]; uint64_t align; } st;
    int err = 0, rc;
    long long got;
    char line[400];

    if (!fn) return -1;
    memset(&st, 0, sizeof st);
    rc = fn(p->ctx, path, &st, sizeof st, &err);
    got = rc >= 0 ? jk_host_statfs_free(&st, sizeof st) : -1;
    if (got < 0 && p->logged == 0) {
        int n = snprintf(line, sizeof line, "free space probe: call=%s rc=%d errno=%d raw=", kind, rc, err);
        for (int i = 0; i < 64 && n > 0 && n + 3 < (int)sizeof line; i++, n += 2)
            snprintf(line + n, sizeof line - (size_t)n, "%02x", st.raw[i]);
        if (n > 0 && n < (int)sizeof line) snprintf(line + n, sizeof line - (size_t)n, " path=%s", path);
        p->logged = 1;
        if (p->log) p->log(p->ctx, line);
    } else if (got >= 0 && p->logged == 1) {
        snprintf(line, sizeof line, "free space probe: ok call=%s path=%s", kind, path);
        p->logged = 2;
        if (p->log) p->log(p->ctx, line);
    }
    return got;
}

long long jk_host_free_probe(JkFreeProbe *p, const char *path)
{
    char parent[512];
    long long got;
    size_t n;

    if (!p || !path || !*path) return -1;
    if ((got = free_try(p, p->fstatfs_dir, "fstatfs", path)) >= 0) return got;
    if ((got = free_try(p, p->statfs_path, "statfs", path)) >= 0) return got;
    /* The parent folder (the path may not exist yet), never "/". */
    n = strlen(path);
    if (n >= sizeof parent) return -1;
    memcpy(parent, path, n + 1);
    while (n > 1 && parent[n - 1] == '/') parent[--n] = 0;
    while (n > 0 && parent[n - 1] != '/') n--;
    while (n > 1 && parent[n - 1] == '/') n--;
    if (n <= 1) return -1;
    parent[n] = 0;
    if ((got = free_try(p, p->fstatfs_dir, "fstatfs-parent", parent)) >= 0) return got;
    return free_try(p, p->statfs_path, "statfs-parent", parent);
}

int jk_host_stamp(long long t, char *out, size_t cap)
{
    if (t < 0 || !out || cap < 16) return -1;
    long long days = t / 86400, rem = t % 86400;
    /* civil from days (H. Hinnant), days since 1970-01-01 */
    long long z = days + 719468, era = z / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
    long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    snprintf(out, cap, "%04lld-%02lld-%02lld_%02lld%02lld", y, m, d, rem / 3600, rem % 3600 / 60);
    return 0;
}

const char *jk_host_first_dir(const char *const *paths)
{
    for (; paths && *paths; paths++)
        if (jk_fs_is_dir(*paths)) return *paths;
    return NULL;
}

/* 1 when p has no empty, "." or ".." segment after the first character. */
static int plain_segments(const char *p)
{
    for (const char *seg = p + 1;;) {
        const char *end = strchr(seg, '/');
        size_t n = end ? (size_t)(end - seg) : strlen(seg);
        if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) return 0;
        if (!end) return 1;
        seg = end + 1;
    }
}

/* 1 when every part of root/rel that exists is a real folder, never a link
 * (lstat): the clean-test override cannot lead out of clean-test/. A part
 * that does not exist ends the walk (ok when missing_ok). */
static int real_dirs(const char *root, const char *rel, int missing_ok)
{
    char p[700];
    size_t rl = strlen(root);
    if (snprintf(p, sizeof p, "%s/%s", root, rel) >= (int)sizeof p) return 0;
    for (char *s = p + rl + 1;; s++) {
        char *end = strchr(s, '/');
        if (end) *end = 0;
        struct stat st;
        int r = lstat(p, &st);
        if (end) *end = '/';
        if (r != 0) return missing_ok && errno == ENOENT;
        if (!S_ISDIR(st.st_mode)) return 0;            /* a link (to anything) or a file */
        if (!end) return 1;
        s = end;
    }
}

int jk_host_game_dir(const char *product_root, char *out, size_t cap, char *other, size_t ocap, char *note,
                     size_t ncap)
{
    char flag[600], file[600], base[600];
    if (note && ncap) note[0] = 0;
    if (!product_root || !out || !cap) return 0;
    if (snprintf(flag, sizeof flag, "%s/devtools/enable", product_root) < (int)sizeof flag && jk_fs_exists(flag) &&
        snprintf(file, sizeof file, "%s/devtools/game_dir", product_root) < (int)sizeof file && jk_fs_exists(file)) {
        char *text = jk_fs_read(file, 1024, NULL);
        if (text) {
            text[strcspn(text, "\r\n")] = 0;
            size_t bl = (size_t)snprintf(base, sizeof base, "%s/clean-test/", product_root);
            int ok = bl < sizeof base && !strncmp(text, base, bl) && text[bl] && text[0] == '/' && plain_segments(text) &&
                     jk_fs_is_dir(text) && strlen(text) < cap &&
                     real_dirs(product_root, text + strlen(product_root) + 1, 0) &&     /* no link on the way */
                     real_dirs(product_root, "clean-test/System", 1);                 /* its own state: no link either */
            if (ok) {
                snprintf(out, cap, "%s", text);
                if (note && ncap) snprintf(note, ncap, "devtools game_dir taken: %s", text);
                free(text);
                return 2;
            }
            if (note && ncap) snprintf(note, ncap, "devtools game_dir refused (not a real folder under %s, or a link on the way): %.300s", base, text);
            free(text);
        }
    }
    char keep[600] = "";
    if (note && ncap && note[0]) snprintf(keep, sizeof keep, "%s", note);
    int r = jk_game_dir_find(product_root, out, cap, other, ocap);
    if (r == -1 && note && ncap)
        snprintf(note, ncap, "%s%sgame folder: two spellings, %.250s and %.250s; using the first", keep, keep[0] ? "; " : "",
                 out, other ? other : "?");
    return r;
}

int jk_host_game_exe(const char *game_dir, char *out, size_t cap)
{
    static const char tail[] = "/speed.exe";
    if (!game_dir || game_dir[0] != '/' || !out) return -1;
    size_t n = strlen(game_dir);
    if (2 + n + sizeof tail > cap) return -1;
    out[0] = 'Z';
    out[1] = ':';
    memcpy(out + 2, game_dir, n);
    memcpy(out + 2 + n, tail, sizeof tail);
    for (char *s = out + 2; *s; s++)
        if (*s == '/') *s = '\\';
    return 0;
}

int jk_host_data_root(const char *product_root, int found, const char *system, char *out, size_t cap)
{
    if (!out || !cap) return -1;
    int n = found == 2 ? snprintf(out, cap, "%s/clean-test/System", product_root ? product_root : "")
                       : snprintf(out, cap, "%s", system ? system : "");
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    return 0;
}
