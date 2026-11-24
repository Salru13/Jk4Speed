/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* File helpers. See jk_fs.h. */
#include "jk_fs.h"
#include "jk_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int jk_fs_fail_after = -1;
int jk_fs_cut_after = -1;
long long (*jk_fs_free_fn)(void *ctx, const char *path) = NULL;
void *jk_fs_free_ctx = NULL;

long long jk_fs_free_bytes(const char *path)
{
    return jk_fs_free_fn && path ? jk_fs_free_fn(jk_fs_free_ctx, path) : -1;
}

/* One write counted for fault injection. 1: let it fail. */
static int injected_failure(void)
{
    if (jk_fs_fail_after < 0) return 0;
    if (jk_fs_fail_after == 0) { jk_fs_fail_after = -1; return 1; }
    jk_fs_fail_after--;
    return 0;
}

int jk_fs_join(char *out, size_t cap, const char *a, const char *b)
{
    if (!out || !cap || !a || !b) return -1;
    int n = snprintf(out, cap, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    return 0;
}

int jk_fs_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

int jk_fs_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

long long jk_fs_size(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (long long)st.st_size;
}

int jk_fs_mkdir_p(const char *path)
{
    if (!path || !*path) return -1;
    char p[1024];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
    return jk_fs_is_dir(p) ? 0 : -1;
}

int jk_fs_mkdir_parent(const char *path)
{
    if (!path) return -1;
    char p[1024];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    char *slash = strrchr(p, '/');
    if (!slash || slash == p) return 0;
    *slash = 0;
    return jk_fs_mkdir_p(p);
}

char *jk_fs_read(const char *path, size_t max, size_t *len)
{
    if (len) *len = 0;
    long long size = jk_fs_size(path);
    if (size < 0 || (unsigned long long)size > max) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc((size_t)size + 1);
    size_t n = buf ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!buf || n != (size_t)size) { free(buf); return NULL; }
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

static int temp_name(char *out, size_t cap, const char *path)
{
    int n = snprintf(out, cap, "%s.jk4new", path);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

/* A fresh temp file: a leftover (or planted) one is removed first, and the
 * open never follows a link or reuses an existing file. */
static int open_temp(const char *tmp)
{
    if (unlink(tmp) != 0 && errno != ENOENT) return -1;
    return open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
}

/* Best effort: make a rename in dir durable (power loss). */
static void sync_parent(const char *path)
{
    char d[1024];
    if (snprintf(d, sizeof d, "%s", path) >= (int)sizeof d) return;
    char *slash = strrchr(d, '/');
    if (!slash) return;
    if (slash == d) slash[1] = 0; else *slash = 0;
    int fd = open(d, O_RDONLY);
    if (fd >= 0) { fsync(fd); close(fd); }
}

int jk_fs_safe_path(const char *base, const char *rel)
{
    if (!base || !rel || !*rel || *rel == '/') return -1;
    char p[1024];
    size_t bl = strlen(base);
    if (snprintf(p, sizeof p, "%s/%s", base, rel) >= (int)sizeof p) return -1;
    for (char *s = p + bl + 1;; s++) {
        int last = !strchr(s, '/');
        char *end = last ? s + strlen(s) : strchr(s, '/');
        char keep = *end;
        *end = 0;
        struct stat st;
        int r = lstat(p, &st);
        *end = keep;
        if (r != 0) return errno == ENOENT ? 0 : -1;       /* the rest does not exist yet */
        if (S_ISLNK(st.st_mode)) return -1;
        if (last) return S_ISREG(st.st_mode) ? 0 : -1;
        if (!S_ISDIR(st.st_mode)) return -1;
        s = end;
    }
}

/* Write all of data to fd; honours fault injection. */
static int write_all(int fd, const void *data, size_t n)
{
    if (injected_failure()) { errno = ENOSPC; return -1; }
    const char *p = data;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; n -= (size_t)w;
    }
    return 0;
}

int jk_fs_write_atomic(const char *path, const void *data, size_t n)
{
    char tmp[1100];
    if (!path || temp_name(tmp, sizeof tmp, path) < 0) return -1;
    int fd = open_temp(tmp);
    if (fd < 0) return -1;
    int ok = write_all(fd, data, n) == 0 && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    sync_parent(path);
    return 0;
}

/* r5: a "still working" tick for long reads (see jk_fs.h) */
void (*jk_fs_tick_fn)(void *ctx, long long done);
void *jk_fs_tick_ctx;
static void fs_tick(long long done)
{
    if (jk_fs_tick_fn && (done & ((8LL << 20) - 1)) < (64 * 1024)) jk_fs_tick_fn(jk_fs_tick_ctx, done);
}

int jk_fs_sha256(const char *path, char out[65], long long *size)
{
    if (size) *size = -1;
    if (!path || !out || jk_fs_is_dir(path)) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    JkSha256 s;
    jk_sha256_init(&s);
    static unsigned char buf[64 * 1024];
    long long total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) { jk_sha256_update(&s, buf, n); total += (long long)n; fs_tick(total); }
    int err = ferror(f);
    fclose(f);
    if (err) return -1;
    jk_sha256_final_hex(&s, out);
    if (size) *size = total;
    return 0;
}

int jk_fs_copy_checked(const char *src, const char *dst, const char *want_sha)
{
    char tmp[1100], got[65], src_sha[65];
    if (!src || !dst || temp_name(tmp, sizeof tmp, dst) < 0) return -1;
    if (!want_sha) {
        if (jk_fs_sha256(src, src_sha, NULL) < 0) return -1;
        want_sha = src_sha;
    }
    if (jk_fs_mkdir_parent(dst) < 0) return -1;
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    int fd = open_temp(tmp);
    if (fd < 0) { fclose(in); return -1; }
    static unsigned char buf[64 * 1024];
    int ok = 1;
    size_t n;
    long long copied = 0;
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) { ok = write_all(fd, buf, n) == 0; copied += (long long)n; fs_tick(copied); }
    ok = ok && !ferror(in) && fsync(fd) == 0;
    fclose(in);
    ok = close(fd) == 0 && ok;
    ok = ok && jk_fs_sha256(tmp, got, NULL) == 0 && !strcmp(got, want_sha);
    if (!ok || rename(tmp, dst) != 0) { unlink(tmp); return -1; }
    sync_parent(dst);
    return 0;
}

int jk_fs_copy_tail(const char *src, const char *dst, long long max_bytes)
{
    long long size = jk_fs_size(src);
    if (size < 0 || max_bytes < 0) return -1;
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    if (size > max_bytes && fseeko(in, (off_t)(size - max_bytes), SEEK_SET) != 0) { fclose(in); return -1; }
    char tmp[1100];
    if (temp_name(tmp, sizeof tmp, dst) < 0 || jk_fs_mkdir_parent(dst) < 0) { fclose(in); return -1; }
    int fd = open_temp(tmp);
    if (fd < 0) { fclose(in); return -1; }
    static unsigned char buf[64 * 1024];
    int ok = 1;
    size_t n;
    long long left = size > max_bytes ? max_bytes : size;
    while (ok && left > 0 && (n = fread(buf, 1, left < (long long)sizeof buf ? (size_t)left : sizeof buf, in)) > 0) {
        ok = write_all(fd, buf, n) == 0;
        left -= (long long)n;
    }
    fclose(in);
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, dst) != 0) { unlink(tmp); return -1; }
    return 0;
}

int jk_fs_remove(const char *path)
{
    if (!path) return -1;
    if (unlink(path) == 0 || errno == ENOENT) return 0;
    return -1;
}

int jk_fs_rename(const char *from, const char *to)
{
    if (!from || !to) return -1;
    if (jk_fs_cut_after == 0) _exit(77);                 /* power cut (host tests only) */
    if (jk_fs_cut_after > 0) jk_fs_cut_after--;
    if (jk_fs_mkdir_parent(to) < 0 || rename(from, to) != 0) return -1;
    sync_parent(to);
    return 0;
}
