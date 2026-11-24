/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Support bundle and launcher log. See jk_support.h. */
#include "jk_support.h"
#include "jk_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Launcher log                                                       */
/* ------------------------------------------------------------------ */

void jk_log_event(const char *data_root, const char *line)
{
    char logs[1024], path[1024], old[1100];
    if (!data_root || !line || jk_fs_join(logs, sizeof logs, data_root, "state") < 0
        || jk_fs_join(path, sizeof path, logs, "launcher.log") < 0 || jk_fs_mkdir_p(logs) < 0) return;
    if (jk_fs_size(path) + (long long)strlen(line) + 1 > JK_LAUNCHER_LOG_MAX) {
        snprintf(old, sizeof old, "%s.1", path);
        jk_fs_remove(old);
        jk_fs_rename(path, old);
    }
    FILE *f = fopen(path, "ab");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Bundle                                                             */
/* ------------------------------------------------------------------ */

typedef struct Man { char *p; size_t n, cap; } Man;
static void man_add(Man *m, const char *fmt, const char *a, const char *b, const char *c)
{
    char line[1400];
    int k = snprintf(line, sizeof line, fmt, a ? a : "", b ? b : "", c ? c : "");
    if (k < 0) return;
    if ((size_t)k >= sizeof line) k = (int)sizeof line - 1;
    if (m->n + (size_t)k + 1 > m->cap) {
        size_t cap = (m->n + (size_t)k + 1) * 2;
        char *q = realloc(m->p, cap);
        if (!q) return;
        m->p = q; m->cap = cap;
    }
    memcpy(m->p + m->n, line, (size_t)k);
    m->n += (size_t)k;
    m->p[m->n] = 0;
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);
    return n >= k && !strcmp(s + n - k, suffix);
}

/* A registry file in any letter case, also a copy such as "user.reg.bak". */
static int is_registry(const char *name)
{
    for (const char *s = name; *s; s++)
        if (s[0] == '.' && (s[1] | 32) == 'r' && (s[2] | 32) == 'e' && (s[3] | 32) == 'g' && (s[4] == 0 || s[4] == '.'))
            return 1;
    return 0;
}

/* Copies the last JK_SUPPORT_LOG_MAX bytes of src to dst, replacing user with
 * <ps4-user>. 0 ok. */
static int copy_masked(const char *src, const char *dst, const char *user)
{
    long long size = jk_fs_size(src);
    if (size < 0) return -1;
    long long take = size > JK_SUPPORT_LOG_MAX ? JK_SUPPORT_LOG_MAX : size;
    FILE *f = fopen(src, "rb");
    if (!f) return -1;
    if (size > take && fseeko(f, (off_t)(size - take), SEEK_SET) != 0) { fclose(f); return -1; }
    char *buf = malloc((size_t)take + 1);
    size_t n = buf ? fread(buf, 1, (size_t)take, f) : 0;
    fclose(f);
    if (!buf) return -1;
    buf[n] = 0;
    int r;
    size_t ul = user ? strlen(user) : 0;
    if (ul >= 2) {
        static const char MASK[] = "<ps4-user>";
        size_t cap = n + 1, count = 0;
        for (size_t i = 0; i + ul <= n;) {          /* no memmem: the console libc may lack it */
            if (!memcmp(buf + i, user, ul)) { count++; i += ul; } else i++;
        }
        cap += count * (sizeof MASK - 1);
        char *out = malloc(cap), *o = out;
        if (!out) { free(buf); return -1; }
        for (size_t i = 0; i < n;) {
            if (i + ul <= n && !memcmp(buf + i, user, ul)) { memcpy(o, MASK, sizeof MASK - 1); o += sizeof MASK - 1; i += ul; }
            else *o++ = buf[i++];
        }
        r = jk_fs_write_atomic(dst, out, (size_t)(o - out));
        free(out);
    } else {
        r = jk_fs_write_atomic(dst, buf, n);
    }
    free(buf);
    return r;
}

/* Adds one source file to the bundle (or notes why not). */
static void add_file(Man *man, const char *bundle, const char *src, const char *user, const char *label)
{
    if (!src) return;
    if (is_registry(base_name(src))) { man_add(man, "  skipped  %s  (registry files are never copied)\n", base_name(src), NULL, NULL); return; }
    if (jk_fs_size(src) < 0) { man_add(man, "  missing  %s  (%s)\n", base_name(src), label, NULL); return; }
    char dst[1024], name[300];
    snprintf(name, sizeof name, "%s", base_name(src));
    if (jk_fs_join(dst, sizeof dst, bundle, name) < 0) return;
    for (int k = 2; jk_fs_exists(dst) && k < 10; k++) {
        snprintf(name, sizeof name, "%d-%s", k, base_name(src));
        if (jk_fs_join(dst, sizeof dst, bundle, name) < 0) return;
    }
    if (copy_masked(src, dst, user) < 0) { man_add(man, "  failed   %s  (%s)\n", name, label, NULL); return; }
    char sha[65], sz[32];
    long long size = 0;
    if (jk_fs_sha256(dst, sha, &size) < 0) snprintf(sha, sizeof sha, "?");
    snprintf(sz, sizeof sz, "%lld", size);
    man_add(man, "  %s  %s bytes  sha256 %s\n", name, sz, sha);
}

typedef struct Sess { char name[256]; long long mtime; } Sess;

static int sess_cmp(const void *a, const void *b)
{
    const Sess *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;     /* newest first */
    return strcmp(y->name, x->name);
}

static void add_sessions(Man *man, const char *bundle, const char *logs_dir, const char *user)
{
    if (!logs_dir) return;
    DIR *d = opendir(logs_dir);
    if (!d) { man_add(man, "  missing  session logs  (%s)\n", logs_dir, NULL, NULL); return; }
    /* keep only the newest few while reading: small stack, any folder size */
    Sess list[JK_SUPPORT_SESSIONS + 1];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "session-", 8) || !ends_with(e->d_name, ".log")) continue;
        char p[1024];
        struct stat st;
        if (jk_fs_join(p, sizeof p, logs_dir, e->d_name) < 0 || stat(p, &st) != 0) continue;
        snprintf(list[n].name, sizeof list[n].name, "%s", e->d_name);
        list[n].mtime = (long long)st.st_mtime;
        n++;
        qsort(list, (size_t)n, sizeof list[0], sess_cmp);
        if (n > JK_SUPPORT_SESSIONS) n = JK_SUPPORT_SESSIONS;      /* drop the oldest */
    }
    closedir(d);
    for (int i = 0; i < n && i < JK_SUPPORT_SESSIONS; i++) {
        char p[1024];
        if (jk_fs_join(p, sizeof p, logs_dir, list[i].name) == 0) add_file(man, bundle, p, user, "session log");
    }
}

/* A folder name made only of [A-Za-z0-9_.-], never "." or "..". */
static void clean_stamp(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    for (; s && *s && n + 1 < cap; s++) {
        char c = *s;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[n++] = ok ? c : '_';
    }
    out[n] = 0;
}

/* Copies every file of src into dst (flat). 0 ok. */
static int copy_flat(const char *src, const char *dst)
{
    if (jk_fs_mkdir_p(dst) < 0) return -1;
    DIR *d = opendir(src);
    if (!d) return -1;
    int ok = 1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char a[1024], b[1024];
        if (jk_fs_join(a, sizeof a, src, e->d_name) < 0 || jk_fs_join(b, sizeof b, dst, e->d_name) < 0) { ok = 0; continue; }
        if (jk_fs_copy_checked(a, b, NULL) < 0) ok = 0;
    }
    closedir(d);
    return ok ? 0 : -1;
}

int jk_support_bundle(const JkSupportIn *in, char *out_dir, size_t cap, int *usb_ok)
{
    if (usb_ok) *usb_ok = 0;
    if (out_dir && cap) out_dir[0] = 0;
    if (!in || !in->data_root || !in->logs_dir || !out_dir || !cap || !jk_fs_is_dir(in->data_root)) return -1;

    char support[1024], name[128], bundle[1024];
    if (jk_fs_join(support, sizeof support, in->logs_dir, "support") < 0 || jk_fs_mkdir_p(support) < 0) return -1;
    char stamp[96];
    clean_stamp(stamp, sizeof stamp, in->stamp);
    for (int k = 1; k < 1000; k++) {
        if (stamp[0]) {
            if (k == 1) snprintf(name, sizeof name, "%s", stamp);
            else snprintf(name, sizeof name, "%s-%d", stamp, k);
        } else {
            snprintf(name, sizeof name, "bundle-%d", k);
        }
        if (jk_fs_join(bundle, sizeof bundle, support, name) < 0) return -1;
        if (!jk_fs_exists(bundle)) break;
    }
    if (jk_fs_exists(bundle) || jk_fs_mkdir_p(bundle) < 0) return -1;

    Man man = { 0 };
    man_add(&man, "Jk4Speed support bundle\nbuild: %s\ntime: %s\n", in->build_id ? in->build_id : "unknown", name, NULL);
    man_add(&man, "state: %s\npace: %s\n", in->state ? in->state : "unknown", in->pace ? in->pace : "unknown", NULL);
    man_add(&man, "release: %s\n", in->release ? in->release : "unknown", NULL, NULL);
    if (in->game_dir) {
        char exe[1024], sha[65];
        if (jk_fs_join(exe, sizeof exe, in->game_dir, "speed.exe") == 0 && jk_fs_sha256(exe, sha, NULL) == 0)
            man_add(&man, "game exe sha256: %s\n", sha, NULL, NULL);
        else
            man_add(&man, "game exe: not found\n", NULL, NULL, NULL);
    }
    man_add(&man, "This folder holds logs only: no saves, no registry files, no game data.\n\nfiles:\n", NULL, NULL, NULL);

    char p[1024];
    if (jk_fs_join(p, sizeof p, in->data_root, "state/launcher.log") == 0) add_file(&man, bundle, p, in->local_user, "launcher");
    add_sessions(&man, bundle, in->logs_dir, in->local_user);
    if (in->log_files) {
        for (const char *const *f = in->log_files; *f; f++) add_file(&man, bundle, *f, in->local_user, "log");
    } else {
        static const struct { const char *name, *label; int optional; } GAME_LOGS[] = {
            { "speed_d3d9.log", "client", 0 }, { "speed_frames.log", "frame times", 0 },
            { "dxn_server.txt", "server", 0 }, { "dxn_stage.txt", "server stages", 1 },
            { "d3d9.log", "DXVK", 1 }, { "jk4speed-dev.ini", "client settings", 0 },
        };
        for (size_t i = 0; in->game_dir && i < sizeof GAME_LOGS / sizeof GAME_LOGS[0]; i++)
            if (jk_fs_join(p, sizeof p, in->game_dir, GAME_LOGS[i].name) == 0 && (!GAME_LOGS[i].optional || jk_fs_exists(p)))
                add_file(&man, bundle, p, in->local_user, GAME_LOGS[i].label);
        if (in->profiles_dir && jk_fs_join(p, sizeof p, in->profiles_dir, "nfsmw.profile") == 0)   /* r4: the composed profile */
            add_file(&man, bundle, p, in->local_user, "runtime profile");
        if (jk_fs_join(p, sizeof p, in->runtime_root ? in->runtime_root : in->logs_dir, "radv.log") == 0)
            add_file(&man, bundle, p, in->local_user, "radv");
    }
    if (jk_fs_join(p, sizeof p, in->data_root, "state/installed.txt") == 0) add_file(&man, bundle, p, in->local_user, "install record");
    if (in->files_dir && jk_fs_join(p, sizeof p, in->files_dir, "manifest.txt") == 0) add_file(&man, bundle, p, NULL, "package manifest");

    char mf[1024];
    int r = jk_fs_join(mf, sizeof mf, bundle, "MANIFEST.txt") == 0 && man.p ? jk_fs_write_atomic(mf, man.p, man.n) : -1;
    free(man.p);
    if (r < 0) return -1;
    snprintf(out_dir, cap, "%s", bundle);

    if (in->usb_dir && jk_fs_is_dir(in->usb_dir)) {
        char usb[1024], dst[1024];
        if (jk_fs_join(usb, sizeof usb, in->usb_dir, "Jk4Speed-support") == 0 && jk_fs_join(dst, sizeof dst, usb, name) == 0
            && copy_flat(bundle, dst) == 0 && usb_ok)
            *usb_ok = 1;
    }
    return 0;
}
