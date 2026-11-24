/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The title's host helpers that need no console: free space from a FreeBSD
 * struct statfs, the bundle's wall-clock stamp, the USB root probe. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_host.h"

#include <stdint.h>

/* The FreeBSD 11 struct statfs head (version 0x20030518). */
typedef struct Head { uint32_t version, type; uint64_t flags, bsize, iosize, blocks, bfree; int64_t bavail; } Head;

static void statfs_free_reads_bavail_times_bsize(void)
{
    union { Head h; uint8_t raw[472]; } st;
    memset(&st, 0, sizeof st);
    st.h.version = 0x20030518u;
    st.h.bsize = 32768;
    st.h.bavail = 1000;
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), 32768LL * 1000);
    st.h.version = 0x20030517u;                               /* another layout: unknown */
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), -1);
    st.h.version = 0x20030518u;
    st.h.bsize = 100;                                          /* implausible block size */
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), -1);
    st.h.bsize = 4096;
    st.h.bavail = -5;                                          /* negative: reserved blocks */
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), -1);
    st.h.bavail = INT64_MAX / 2;                               /* would overflow */
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), -1);
    CHECK_EQ(jk_host_statfs_free(&st, 16), -1);               /* too short */
    CHECK_EQ(jk_host_statfs_free(NULL, sizeof st), -1);
}

/* FreeBSD 12's ino64 struct statfs (version 0x20140518, 2344 bytes) keeps
 * the same head, so a kernel that fills that layout is read the same way. */
static void statfs_free_reads_the_ino64_layout_too(void)
{
    union { Head h; uint8_t raw[2344]; } st;
    memset(&st, 0, sizeof st);
    st.h.version = 0x20140518u;
    st.h.bsize = 32768;
    st.h.bavail = 1000;
    CHECK_EQ(jk_host_statfs_free(&st, sizeof st), 32768LL * 1000);
}

/* A fake kernel for jk_host_free_probe: per call kind, the rc, the errno and
 * the head it writes; every call is recorded as "kind:path;". */
typedef struct Fake {
    int rc[2], err[2];
    Head head[2];
    int ok_path_len;            /* >0: only a path of this length gets head[] */
    char calls[512];
    char log[4][400];
    int nlog;
} Fake;

static int fake_call(Fake *f, int kind, const char *path, void *buf, size_t cap, int *err)
{
    size_t n = strlen(f->calls);
    snprintf(f->calls + n, sizeof f->calls - n, "%s:%s;", kind ? "statfs" : "fstatfs", path);
    if (f->ok_path_len && (int)strlen(path) != f->ok_path_len) { *err = 2; return -1; }
    if (cap >= sizeof(Head)) memcpy(buf, &f->head[kind], sizeof(Head));
    *err = f->err[kind];
    return f->rc[kind];
}
static int fake_fstatfs(void *ctx, const char *p, void *b, size_t c, int *e) { return fake_call(ctx, 0, p, b, c, e); }
static int fake_statfs(void *ctx, const char *p, void *b, size_t c, int *e) { return fake_call(ctx, 1, p, b, c, e); }
static void fake_log(void *ctx, const char *line)
{
    Fake *f = ctx;
    if (f->nlog < 4) snprintf(f->log[f->nlog], sizeof f->log[0], "%s", line);
    f->nlog++;
}
static Head good_head(void)
{
    Head h;
    memset(&h, 0, sizeof h);
    h.version = 0x20030518u;
    h.bsize = 32768;
    h.bavail = 1000;
    return h;
}

static void free_probe_falls_back_and_logs_the_first_failure_once(void)
{
    Fake f;
    memset(&f, 0, sizeof f);
    f.rc[0] = -1; f.err[0] = 2;                     /* _fstatfs refused: ENOENT */
    f.head[1] = good_head();                        /* statfs answers */
    JkFreeProbe p = { fake_fstatfs, fake_statfs, fake_log, &f, 0 };
    CHECK_EQ(jk_host_free_probe(&p, "/data/Jk4Speed/System"), 32768LL * 1000);
    CHECK(!strcmp(f.calls, "fstatfs:/data/Jk4Speed/System;statfs:/data/Jk4Speed/System;"));
    CHECK_EQ(f.nlog, 2);
    CHECK(!strncmp(f.log[0], "free space probe: call=fstatfs rc=-1 errno=2 raw=", 49));
    CHECK_EQ(strlen(f.log[0] + 49), 128 + strlen(" path=/data/Jk4Speed/System"));
    CHECK(strstr(f.log[0], " path=/data/Jk4Speed/System") != NULL);
    CHECK(!strcmp(f.log[1], "free space probe: ok call=statfs path=/data/Jk4Speed/System"));
    /* Second PATCH: the same failure is not logged again. */
    f.calls[0] = 0;
    CHECK_EQ(jk_host_free_probe(&p, "/data/Jk4Speed/NFSMW"), 32768LL * 1000);
    CHECK_EQ(f.nlog, 2);
}

static void free_probe_logs_the_raw_head_of_an_unknown_layout(void)
{
    Fake f;
    memset(&f, 0, sizeof f);
    f.head[0] = good_head();
    f.head[0].version = 0x12345678u;                /* rc 0, another layout */
    f.rc[1] = -1; f.err[1] = 78;
    JkFreeProbe p = { fake_fstatfs, fake_statfs, fake_log, &f, 0 };
    CHECK_EQ(jk_host_free_probe(&p, "/data/x"), -1);
    CHECK_EQ(f.nlog, 1);
    CHECK(!strncmp(f.log[0], "free space probe: call=fstatfs rc=0 errno=0 raw=78563412", 56));
}

static void free_probe_tries_the_parent_but_never_the_root(void)
{
    Fake f;
    memset(&f, 0, sizeof f);
    f.head[0] = good_head();
    f.ok_path_len = (int)strlen("/data/Jk4Speed");  /* System/ does not exist yet */
    JkFreeProbe p = { fake_fstatfs, fake_statfs, fake_log, &f, 0 };
    CHECK_EQ(jk_host_free_probe(&p, "/data/Jk4Speed/System/"), 32768LL * 1000);
    CHECK(!strcmp(f.calls, "fstatfs:/data/Jk4Speed/System/;statfs:/data/Jk4Speed/System/;"
                           "fstatfs:/data/Jk4Speed;"));
    CHECK(!strcmp(f.log[1], "free space probe: ok call=fstatfs-parent path=/data/Jk4Speed"));
    /* /data's parent is the sandbox root, another volume: not asked. */
    f.calls[0] = 0;
    f.ok_path_len = 1000;
    CHECK_EQ(jk_host_free_probe(&p, "/data"), -1);
    CHECK(!strcmp(f.calls, "fstatfs:/data;statfs:/data;"));
    /* No statfs on this console: skipped. */
    f.calls[0] = 0;
    p.statfs_path = NULL;
    CHECK_EQ(jk_host_free_probe(&p, "/data/a/b"), -1);
    CHECK(!strcmp(f.calls, "fstatfs:/data/a/b;fstatfs:/data/a;"));
    CHECK_EQ(jk_host_free_probe(&p, NULL), -1);
    CHECK_EQ(jk_host_free_probe(NULL, "/data"), -1);
}

static void stamp_is_utc_minutes(void)
{
    char s[32];
    CHECK_EQ(jk_host_stamp(0, s, sizeof s), 0);
    CHECK(!strcmp(s, "1970-01-01_0000"));
    CHECK_EQ(jk_host_stamp(1795452180LL, s, sizeof s), 0);      /* 2026-11-23 16:43 UTC */
    CHECK(!strcmp(s, "2026-11-23_1643"));
    CHECK_EQ(jk_host_stamp(951782400LL, s, sizeof s), 0);       /* leap day 2000-02-29 */
    CHECK(!strcmp(s, "2000-02-29_0000"));
    CHECK(jk_host_stamp(-1, s, sizeof s) < 0);
    CHECK(jk_host_stamp(0, s, 8) < 0);
}

static void usb_probe_takes_first_folder(void)
{
    char d[256], a[300], b[300];
    tmp_dir(d, sizeof d);
    snprintf(a, sizeof a, "%s/usb0", d);
    snprintf(b, sizeof b, "%s/usb1", d);
    const char *list[] = { a, b, NULL };
    CHECK(jk_host_first_dir(list) == NULL);
    tmp_put(d, "usb0", "a file, not a folder");
    CHECK(jk_host_first_dir(list) == NULL);
    CHECK(tmp_get(d, "usb0", a, 2) > 0 && tmp_exists(d, "usb0"));
    snprintf(a, sizeof a, "%s/usb0", d);
    unlink(a);
    mkdir(b, 0755);
    CHECK(jk_host_first_dir(list) == b);
    mkdir(a, 0755);
    CHECK(jk_host_first_dir(list) == a);
    tmp_rm_rf(d);
}

/* The title's game folder (jk_host_game_dir): the devtools override is taken
 * only with <root>/devtools/enable and only for a folder under
 * <root>/clean-test/; anything else falls back to jk_game_dir_find. */
static void devtools_game_dir_only_under_clean_test(void)
{
    char root[256], out[512], other[512], note[600], want[512];
    tmp_dir(root, sizeof root);
    tmp_put(root, "NFSMW/speed.exe", "exe");
    tmp_put(root, "clean-test/run1/speed.exe", "exe");
    tmp_put(root, "elsewhere/speed.exe", "exe");
    snprintf(want, sizeof want, "%s/NFSMW", root);
    /* no enable file: the override file is not even read */
    char line[600];
    snprintf(line, sizeof line, "%s/clean-test/run1\n", root);
    tmp_put(root, "devtools/game_dir", line);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    CHECK(!strcmp(out, want));
    CHECK(note[0] == 0);
    /* enable: taken (the newline is not part of the path) and logged */
    tmp_put(root, "devtools/enable", "");
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 2);
    snprintf(want, sizeof want, "%s/clean-test/run1", root);
    CHECK(!strcmp(out, want));
    CHECK(strstr(note, "devtools game_dir") != NULL && strstr(note, "clean-test/run1") != NULL);
    /* anything outside clean-test/ is refused, logged, and the real folder is used */
    static const char *const bad[] = { "%s/elsewhere", "%s/clean-test", "%s/clean-test/", "%s/clean-testx/run1",
                                       "%s/clean-test/../elsewhere", "%s/clean-test/missing", "/data/prospero-win/x",
                                       "relative/clean-test/run1" };
    snprintf(want, sizeof want, "%s/NFSMW", root);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        snprintf(line, sizeof line, bad[i], root);
        tmp_put(root, "devtools/game_dir", line);
        CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
        CHECK(!strcmp(out, want));
        CHECK(strstr(note, "refused") != NULL);
    }
    /* enable without a game_dir file: the real folder, nothing to say */
    snprintf(line, sizeof line, "%s/devtools/game_dir", root);
    unlink(line);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    CHECK(note[0] == 0);
    tmp_rm_rf(root);
}

/* I-1 / T8 M4: the override never follows a link out of <root>/clean-test/
 * (the folder, a folder on the way, clean-test itself, or its System/). */
static void clean_test_override_refuses_link(void)
{
    char root[256], out[512], other[512], note[600], line[600], p[600], q[600];
    tmp_dir(root, sizeof root);
    tmp_put(root, "NFSMW/speed.exe", "exe");
    tmp_put(root, "elsewhere/speed.exe", "exe");
    tmp_put(root, "clean-test/run1/speed.exe", "exe");
    tmp_put(root, "devtools/enable", "");
    snprintf(p, sizeof p, "%s/elsewhere", root);
    snprintf(q, sizeof q, "%s/clean-test/link", root);
    CHECK_EQ(symlink(p, q), 0);                                       /* clean-test/link -> elsewhere */
    snprintf(line, sizeof line, "%s/clean-test/link\n", root);
    tmp_put(root, "devtools/game_dir", line);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    CHECK(strstr(out, "/NFSMW") != NULL);
    CHECK(strstr(note, "refused") != NULL);
    /* a link on the way */
    snprintf(q, sizeof q, "%s/clean-test/via", root);
    CHECK_EQ(symlink(root, q), 0);                                    /* clean-test/via -> root */
    snprintf(line, sizeof line, "%s/clean-test/via/elsewhere\n", root);
    tmp_put(root, "devtools/game_dir", line);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    /* a real folder is taken ... */
    snprintf(line, sizeof line, "%s/clean-test/run1\n", root);
    tmp_put(root, "devtools/game_dir", line);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 2);
    /* ... but not when clean-test/System (its own state) is a link */
    snprintf(q, sizeof q, "%s/clean-test/System", root);
    CHECK_EQ(symlink(p, q), 0);
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    CHECK(strstr(note, "refused") != NULL);
    unlink(q);
    /* clean-test itself a link */
    char root2[256];
    tmp_dir(root2, sizeof root2);
    tmp_put(root2, "NFSMW/speed.exe", "exe");
    tmp_put(root2, "devtools/enable", "");
    snprintf(q, sizeof q, "%s/clean-test", root2);
    snprintf(p, sizeof p, "%s/clean-test", root);
    CHECK_EQ(symlink(p, q), 0);
    snprintf(line, sizeof line, "%s/clean-test/run1\n", root2);
    tmp_put(root2, "devtools/game_dir", line);
    CHECK_EQ(jk_host_game_dir(root2, out, sizeof out, other, sizeof other, note, sizeof note), 1);
    unlink(q);                                                        /* the links go first: never followed */
    snprintf(q, sizeof q, "%s/clean-test/link", root);
    unlink(q);
    snprintf(q, sizeof q, "%s/clean-test/via", root);
    unlink(q);
    tmp_rm_rf(root2);
    tmp_rm_rf(root);
}

/* I-1 (lead ruling): the clean-test run keeps its own state root
 * <root>/clean-test/System; every other run uses System/. */
static void clean_test_has_own_state_root(void)
{
    char out[512];
    CHECK_EQ(jk_host_data_root("/data/Jk4Speed", 2, "/data/Jk4Speed/System", out, sizeof out), 0);
    CHECK(!strcmp(out, "/data/Jk4Speed/clean-test/System"));
    CHECK_EQ(jk_host_data_root("/data/Jk4Speed", 1, "/data/Jk4Speed/System", out, sizeof out), 0);
    CHECK(!strcmp(out, "/data/Jk4Speed/System"));
    CHECK_EQ(jk_host_data_root("/data/Jk4Speed", -1, "/data/Jk4Speed/System", out, sizeof out), 0);
    CHECK(!strcmp(out, "/data/Jk4Speed/System"));
    CHECK(jk_host_data_root("/data/Jk4Speed", 2, "/data/Jk4Speed/System", out, 10) < 0);
}

/* Two spellings of the game folder: both are named, the first is used. */
static void game_dir_two_spellings_noted(void)
{
    char root[256], out[512], other[512], note[600];
    tmp_dir(root, sizeof root);
    tmp_put(root, "NFSMW/speed.exe", "exe");
    tmp_put(root, "nfsmw/speed.exe", "exe");
    CHECK_EQ(jk_host_game_dir(root, out, sizeof out, other, sizeof other, note, sizeof note), -1);
    CHECK(strstr(note, "NFSMW") != NULL && strstr(note, "nfsmw") != NULL);
    CHECK(strstr(out, "/NFSMW") != NULL && strstr(other, "/nfsmw") != NULL);
    tmp_rm_rf(root);
}

/* The Windows path PLAY hands to Wine: Z: + the folder with backslashes. */
static void game_exe_is_a_z_path(void)
{
    char out[300];
    CHECK_EQ(jk_host_game_exe("/data/Jk4Speed/NFSMW", out, sizeof out), 0);
    CHECK(!strcmp(out, "Z:\\data\\Jk4Speed\\NFSMW\\speed.exe"));
    CHECK_EQ(jk_host_game_exe("/data/Jk4Speed/clean-test/run 1", out, sizeof out), 0);
    CHECK(!strcmp(out, "Z:\\data\\Jk4Speed\\clean-test\\run 1\\speed.exe"));
    CHECK(jk_host_game_exe("relative", out, sizeof out) < 0);
    CHECK(jk_host_game_exe("/data/Jk4Speed/NFSMW", out, 20) < 0);
}

int main(void)
{
    RUN(statfs_free_reads_bavail_times_bsize);
    RUN(statfs_free_reads_the_ino64_layout_too);
    RUN(free_probe_falls_back_and_logs_the_first_failure_once);
    RUN(free_probe_logs_the_raw_head_of_an_unknown_layout);
    RUN(free_probe_tries_the_parent_but_never_the_root);
    RUN(stamp_is_utc_minutes);
    RUN(usb_probe_takes_first_folder);
    RUN(devtools_game_dir_only_under_clean_test);
    RUN(clean_test_override_refuses_link);
    RUN(clean_test_has_own_state_root);
    RUN(game_dir_two_spellings_noted);
    RUN(game_exe_is_a_z_path);
    return jk_test_report("test_host");
}
