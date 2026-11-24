/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The Wine prefix seed (jk_seed.h): a pinned index, objects in the package
 * and twins of the package's win/ runtime, built in <prefix>.staging and
 * renamed into place only when every file is checked. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_seed.h"
#include "jk_sha256.h"

#include <sys/wait.h>

/* rm -rf that never follows a link (the seed has links such as z: -> /). */
static void rm_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                char p[1024];
                snprintf(p, sizeof p, "%s/%s", path, e->d_name);
                rm_tree(p);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

typedef struct Fx {
    char root[256], seed[300], app[300], prefix[300], staging[320];
    char index_sha[65];
} Fx;

static const char REG[] = "WINE REGISTRY Version 2\n[Software\\\\Wine] 1\n";
static const char NTDLL[] = "MZ ntdll twin of the runtime\n";
static const char DOC[] = "a file under a folder with a space\n";

/* objects/<sha> for text; the sha goes to out. */
static void put_object(Fx *fx, const char *text, char out[65])
{
    char rel[128];
    jk_sha256_hex(text, strlen(text), out);
    snprintf(rel, sizeof rel, "objects/%s", out);
    tmp_put(fx->seed, rel, text);
}

/* A seed with a folder, two links, two objects and one runtime twin. */
static void fx_make(Fx *fx)
{
    char reg[65], doc[65], ntdll[65], idx[2048];
    memset(fx, 0, sizeof *fx);
    tmp_dir(fx->root, sizeof fx->root);
    snprintf(fx->seed, sizeof fx->seed, "%s/app/jk4speed-files/seed", fx->root);
    snprintf(fx->app, sizeof fx->app, "%s/app", fx->root);
    snprintf(fx->prefix, sizeof fx->prefix, "%s/System/prefix", fx->root);
    snprintf(fx->staging, sizeof fx->staging, "%s/System/prefix.staging", fx->root);
    put_object(fx, REG, reg);
    put_object(fx, DOC, doc);
    jk_sha256_hex(NTDLL, strlen(NTDLL), ntdll);
    tmp_put(fx->app, "win/wine/lib/wine/i386-windows/ntdll.dll", NTDLL);
    snprintf(idx, sizeof idx,
             "JKSEED 1\n"
             "d dosdevices\n"
             "d drive_c\n"
             "d drive_c/windows/syswow64\n"
             "d drive_c/Program Files/Common Files\n"
             "l ../drive_c dosdevices/c:\n"
             "l /nonexistent-jkseed-root dosdevices/z:\n"
             "f %s %zu system.reg obj\n"
             "f %s %zu drive_c/Program Files/Common Files/read me.txt obj\n"
             "f %s %zu drive_c/windows/syswow64/ntdll.dll app:win/wine/lib/wine/i386-windows/ntdll.dll\n",
             reg, strlen(REG), doc, strlen(DOC), ntdll, strlen(NTDLL));
    tmp_put(fx->seed, "index.txt", idx);
    jk_sha256_hex(idx, strlen(idx), fx->index_sha);
}

static void fx_free(Fx *fx) { rm_tree(fx->root); }

static int import(Fx *fx, char *why, size_t cap)
{
    const char *roots[] = { "/nonexistent-app0", fx->app };
    return jk_seed_import(fx->seed, roots, 2, fx->prefix, NULL, NULL, why, cap);
}

static int prefix_has(const Fx *fx, const char *rel, const char *want)
{
    char buf[256];
    return tmp_get(fx->prefix, rel, buf, sizeof buf) >= 0 && !strcmp(buf, want);
}

static int progress_calls;
static void count_progress(void *ctx, int done, int total, const char *file)
{
    (void)ctx; (void)file;
    if (done >= 0 && done <= total) progress_calls++;
}

static void seed_imports_and_marks(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "", buf[128], link[64];
    const char *roots[] = { fx.app };
    progress_calls = 0;
    CHECK_EQ(jk_seed_import(fx.seed, roots, 1, fx.prefix, count_progress, NULL, why, sizeof why), 0);
    CHECK(progress_calls >= 3);
    CHECK(prefix_has(&fx, "system.reg", REG));
    CHECK(prefix_has(&fx, "drive_c/Program Files/Common Files/read me.txt", DOC));
    CHECK(prefix_has(&fx, "drive_c/windows/syswow64/ntdll.dll", NTDLL));
    CHECK(tmp_exists(fx.prefix, "drive_c/windows/syswow64"));
    char p[400];
    snprintf(p, sizeof p, "%s/dosdevices/c:", fx.prefix);
    ssize_t n = readlink(p, link, sizeof link - 1);
    CHECK(n > 0);
    if (n > 0) { link[n] = 0; CHECK(!strcmp(link, "../drive_c")); }
    snprintf(p, sizeof p, "%s/dosdevices/z:", fx.prefix);
    n = readlink(p, link, sizeof link - 1);
    CHECK(n > 0);
    if (n > 0) { link[n] = 0; CHECK(!strcmp(link, "/nonexistent-jkseed-root")); }
    CHECK(tmp_get(fx.prefix, JK_SEED_MARKER, buf, sizeof buf) > 0);
    CHECK(!strncmp(buf, fx.index_sha, 64));
    CHECK(!jk_fs_exists(fx.staging));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 1);
    fx_free(&fx);
}

static void seed_present_second_time(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    CHECK_EQ(import(&fx, why, sizeof why), 0);
    /* the game changes its registry: a second import reads nothing and keeps it */
    tmp_put(fx.prefix, "system.reg", "the game wrote this\n");
    CHECK_EQ(import(&fx, why, sizeof why), 1);
    CHECK(prefix_has(&fx, "system.reg", "the game wrote this\n"));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 1);
    fx_free(&fx);
}

static void seed_refuses_foreign_prefix(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    tmp_put(fx.prefix, "user.reg", "someone else's prefix\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(why[0] != 0);
    CHECK(strstr(why, fx.root) == NULL);                   /* no full paths in a reason */
    CHECK(prefix_has(&fx, "user.reg", "someone else's prefix\n"));
    CHECK(!tmp_exists(fx.prefix, "system.reg"));
    CHECK(!jk_fs_exists(fx.staging));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    /* (a marker of another, older seed of ours is moved aside instead:
     * seed_replaces_own_older_seed_keeps_old_aside) */
    fx_free(&fx);
}

/* A prefix from an older seed of ours (valid marker, another index): moved
 * aside to <prefix>.old-<8 hex of its index> (then .2, .3 ...), never
 * deleted, and the new seed goes in. */
static void seed_replaces_own_older_seed_keeps_old_aside(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "", buf[256], aside[400], aside2[400];
    static const char OLD[] = "1111111111111111111111111111111111111111111111111111111111111111\n";
    tmp_put(fx.prefix, JK_SEED_MARKER, OLD);
    tmp_put(fx.prefix, "system.reg", "older seed, played on\n");
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    CHECK_EQ(import(&fx, why, sizeof why), 0);
    CHECK(prefix_has(&fx, "system.reg", REG));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 1);
    snprintf(aside, sizeof aside, "%s.old-11111111", fx.prefix);
    CHECK(tmp_get(aside, "system.reg", buf, sizeof buf) > 0 && !strcmp(buf, "older seed, played on\n"));
    CHECK(tmp_get(aside, JK_SEED_MARKER, buf, sizeof buf) > 0 && !strcmp(buf, OLD));
    /* the same older seed again: the first aside is kept, the next one is .2 */
    tmp_rm_rf(fx.prefix);
    tmp_put(fx.prefix, JK_SEED_MARKER, OLD);
    tmp_put(fx.prefix, "system.reg", "second\n");
    CHECK_EQ(import(&fx, why, sizeof why), 0);
    snprintf(aside2, sizeof aside2, "%s.old-11111111.2", fx.prefix);
    CHECK(tmp_get(aside, "system.reg", buf, sizeof buf) > 0 && !strcmp(buf, "older seed, played on\n"));
    CHECK(tmp_get(aside2, "system.reg", buf, sizeof buf) > 0 && !strcmp(buf, "second\n"));
    CHECK(prefix_has(&fx, "system.reg", REG));
    /* a damaged marker is not ours: refused, untouched */
    tmp_rm_rf(fx.prefix);
    tmp_put(fx.prefix, JK_SEED_MARKER, "not a sha\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(tmp_get(fx.prefix, JK_SEED_MARKER, buf, sizeof buf) > 0 && !strcmp(buf, "not a sha\n"));
    fx_free(&fx);
}

/* jk_layout_make makes System/prefix as an empty folder: that is not a
 * foreign prefix, the seed goes in. */
static void seed_takes_an_empty_prefix_folder(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    CHECK_EQ(jk_fs_mkdir_p(fx.prefix), 0);
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    CHECK_EQ(import(&fx, why, sizeof why), 0);
    CHECK(prefix_has(&fx, "system.reg", REG));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 1);
    fx_free(&fx);
}

static void seed_resumes_after_cut(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    /* a run cut by a power loss: the child dies at its second rename */
    pid_t pid = fork();
    if (pid == 0) {
        jk_fs_cut_after = 1;
        import(&fx, why, sizeof why);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(jk_fs_exists(fx.staging));
    CHECK(!jk_fs_exists(fx.prefix));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    /* and some damage in what it left */
    tmp_put(fx.staging, "drive_c/windows/syswow64/ntdll.dll", "half a file");
    tmp_put(fx.staging, "system.reg", "garbage");
    CHECK_EQ(import(&fx, why, sizeof why), 0);
    CHECK(prefix_has(&fx, "system.reg", REG));
    CHECK(prefix_has(&fx, "drive_c/windows/syswow64/ntdll.dll", NTDLL));
    CHECK(prefix_has(&fx, "drive_c/Program Files/Common Files/read me.txt", DOC));
    CHECK(!jk_fs_exists(fx.staging));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 1);
    fx_free(&fx);
}

static void seed_bad_object_refused(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "", sha[65];
    jk_sha256_hex(DOC, strlen(DOC), sha);
    char rel[100];
    snprintf(rel, sizeof rel, "objects/%s", sha);
    tmp_put(fx.seed, rel, "not the pinned bytes\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(why[0] != 0);
    CHECK(!jk_fs_exists(fx.prefix));
    CHECK_EQ(jk_seed_state(fx.seed, fx.prefix), 0);
    /* a runtime twin missing from every app root is refused the same way */
    tmp_put(fx.seed, rel, DOC);
    char twin[400];
    snprintf(twin, sizeof twin, "%s/win/wine/lib/wine/i386-windows/ntdll.dll", fx.app);
    unlink(twin);
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(!jk_fs_exists(fx.prefix));
    fx_free(&fx);
}

static void seed_index_checked(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    tmp_put(fx.seed, "index.txt", "JKSEED 2\nd drive_c\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    tmp_put(fx.seed, "index.txt", "JKSEED 1\nd ../outside\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    tmp_put(fx.seed, "index.txt", "JKSEED 1\nf 00 1 /etc/passwd obj\n");
    CHECK(import(&fx, why, sizeof why) < 0);
    /* a file below a link of the index is never written through it */
    char sha[65], line[512];
    jk_sha256_hex(DOC, strlen(DOC), sha);
    snprintf(line, sizeof line, "JKSEED 1\nl /tmp dosdevices/z:\nf %s %zu dosdevices/z:/jkseed-escape.txt obj\n", sha,
             strlen(DOC));
    tmp_put(fx.seed, "index.txt", line);
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(!jk_fs_exists("/tmp/jkseed-escape.txt"));
    CHECK(!jk_fs_exists(fx.prefix));
    /* no seed in the package at all */
    char none[400];
    snprintf(none, sizeof none, "%s/no-seed-here", fx.root);
    CHECK_EQ(jk_seed_state(none, fx.prefix), -1);
    fx_free(&fx);
}

static long long tiny_free(void *ctx, const char *path) { (void)ctx; (void)path; return 10; }

static void seed_refused_without_space(void)
{
    Fx fx;
    fx_make(&fx);
    char why[200] = "";
    jk_fs_free_fn = tiny_free;
    CHECK(import(&fx, why, sizeof why) < 0);
    CHECK(strstr(why, "space") != NULL);
    CHECK(!jk_fs_exists(fx.prefix));
    jk_fs_free_fn = NULL;
    fx_free(&fx);
}

int main(void)
{
    RUN(seed_imports_and_marks);
    RUN(seed_present_second_time);
    RUN(seed_refuses_foreign_prefix);
    RUN(seed_replaces_own_older_seed_keeps_old_aside);
    RUN(seed_takes_an_empty_prefix_folder);
    RUN(seed_resumes_after_cut);
    RUN(seed_bad_object_refused);
    RUN(seed_index_checked);
    RUN(seed_refused_without_space);
    return jk_test_report("test_seed");
}
