/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Support bundle and the launcher's own log (spec 9). */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_support.h"

#include <time.h>
#include <utime.h>

typedef struct Env { char root[256], data[300], game[300], logs[300], pkg[300], runtime[300], prof[300]; JkSupportIn in; } Env;

static void set_mtime(const char *dir, const char *rel, long t)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    struct utimbuf u = { (time_t)t, (time_t)t };
    utime(p, &u);
}

static void env_new(Env *e)
{
    memset(e, 0, sizeof *e);
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->data, sizeof e->data, "%s/Jk4Speed/System", e->root);
    snprintf(e->game, sizeof e->game, "%s/Jk4Speed/NFSMW", e->root);
    snprintf(e->runtime, sizeof e->runtime, "%s/prospero-win", e->root);
    snprintf(e->logs, sizeof e->logs, "%s/Jk4Speed/Logs", e->root);
    snprintf(e->prof, sizeof e->prof, "%s/prospero-win/profiles", e->root);
    snprintf(e->pkg, sizeof e->pkg, "%s/pkg", e->root);
    tmp_put(e->game, "speed.exe", "exe");
    tmp_put(e->game, "speed_d3d9.log", "dxvk log line\n");
    tmp_put(e->game, "speed_frames.log", "frames\n");
    tmp_put(e->game, "dxn_server.txt", "server line\n");
    tmp_put(e->game, "dxn_stage.txt", "stage line\n");
    tmp_put(e->game, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\n");
    tmp_put(e->prof, "nfsmw-x360.profile", "prefix = nfsmw\n");   /* r3's: no longer collected */
    tmp_put(e->prof, "nfsmw.profile", "prefix = default\n");
    tmp_put(e->runtime, "radv.log", "radv line\n");
    tmp_put(e->data, "state/installed.txt", "# record\n");
    tmp_put(e->pkg, "manifest.txt", "# manifest\n");
    for (int i = 1; i <= 5; i++) {
        char rel[64], text[64];
        snprintf(rel, sizeof rel, "session-%d.log", i);
        snprintf(text, sizeof text, "session %d by player alice\n", i);
        tmp_put(e->logs, rel, text);
        set_mtime(e->logs, rel, 1000000 + i * 100);
    }
    e->in.data_root = e->data;
    e->in.game_dir = e->game;
    e->in.files_dir = e->pkg;
    e->in.logs_dir = e->logs;
    e->in.runtime_root = e->runtime;
    e->in.profiles_dir = e->prof;
    e->in.build_id = "JK4SPEED-1.0";
    e->in.stamp = "2026-11-17_2105";
    e->in.state = "ready";
    e->in.pace = "60";
    e->in.release = "unofficial";
}

/* Engine 1.0: the server log, the frame log, the ini, the profile and the
 * runtime's session logs (/data/prospero-win/logs) are all in the bundle. */
static void bundle_has_engine_logs(void)
{
    Env e; env_new(&e);
    char big[600];
    snprintf(big, sizeof big, "%s/dxn_server.txt", e.game);
    FILE *f = fopen(big, "wb");
    for (int i = 0; i < 6 * 1024; i++) {                /* 6 MB: the tail is kept */
        static char kb[1024];
        memset(kb, i < 6 * 1024 - 1 ? 'a' : 'z', sizeof kb);
        fwrite(kb, 1, sizeof kb, f);
    }
    fclose(f);
    char out[512], buf[8192];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    const char *want[] = { "dxn_server.txt", "dxn_stage.txt", "speed_frames.log", "speed_d3d9.log", "jk4speed-dev.ini",
                           "nfsmw.profile", "radv.log", "session-5.log" };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        char rel[256];
        snprintf(rel, sizeof rel, "support/2026-11-17_2105/%s", want[i]);
        CHECK(tmp_exists(e.logs, rel));
        if (!tmp_exists(e.logs, rel)) fprintf(stderr, "    missing %s\n", want[i]);
    }
    char p[600];
    snprintf(p, sizeof p, "%s/support/2026-11-17_2105/dxn_server.txt", e.logs);
    CHECK_EQ(jk_fs_size(p), JK_SUPPORT_LOG_MAX);
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/MANIFEST.txt", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "pace: 60") != NULL);
    CHECK(strstr(buf, "release: unofficial") != NULL);
    tmp_rm_rf(e.root);
}

static void bundle_has_manifest_and_logs(void)
{
    Env e; env_new(&e);
    char out[512];
    int usb = -1;
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, &usb), 0);
    CHECK(strstr(out, "/support/2026-11-17_2105") != NULL);
    CHECK_EQ(usb, 0);
    char rel[256], buf[8192];
    snprintf(rel, sizeof rel, "support/2026-11-17_2105/MANIFEST.txt");
    CHECK(tmp_get(e.logs, rel, buf, sizeof buf) > 0);
    CHECK(strstr(buf, "JK4SPEED-1.0") != NULL);
    CHECK(strstr(buf, "state: ready") != NULL);
    CHECK(strstr(buf, "pace: 60") != NULL);
    CHECK(strstr(buf, "speed_d3d9.log") != NULL);
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/speed_d3d9.log"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/radv.log"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/installed.txt"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/manifest.txt"));
    tmp_rm_rf(e.root);
}

static void newest_three_sessions(void)
{
    Env e; env_new(&e);
    char out[512];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/session-5.log"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/session-4.log"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/session-3.log"));
    CHECK(!tmp_exists(e.logs, "support/2026-11-17_2105/session-2.log"));
    tmp_rm_rf(e.root);
}

static void caps_large_log(void)
{
    Env e; env_new(&e);
    char p[1024];
    snprintf(p, sizeof p, "%s/speed_d3d9.log", e.game);
    FILE *f = fopen(p, "wb");
    for (int i = 0; i < 5 * 1024; i++) {           /* 5 MiB of 1 KiB lines, the last one marked */
        char line[1024];
        memset(line, i == 5 * 1024 - 1 ? 'Z' : 'a', sizeof line - 1);
        line[sizeof line - 1] = '\n';
        fwrite(line, 1, sizeof line, f);
    }
    fclose(f);
    char out[512];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    char q[1024];
    snprintf(q, sizeof q, "%s/speed_d3d9.log", out);
    CHECK_EQ(jk_fs_size(q), JK_SUPPORT_LOG_MAX);
    FILE *g = fopen(q, "rb");
    fseek(g, -2, SEEK_END);
    CHECK_EQ(fgetc(g), 'Z');                       /* the tail is kept, not the head */
    fclose(g);
    tmp_rm_rf(e.root);
}

static void masks_local_user(void)
{
    Env e; env_new(&e);
    e.in.local_user = "alice";
    char out[512], buf[256];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/session-5.log", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "alice") == NULL);
    CHECK(strstr(buf, "<ps4-user>") != NULL);
    tmp_rm_rf(e.root);
}

static void never_copies_reg(void)
{
    Env e; env_new(&e);
    char reg[512], save[512];
    snprintf(reg, sizeof reg, "%s/user.reg", e.data);
    snprintf(save, sizeof save, "%s/save.bin", e.data);
    tmp_put(e.data, "user.reg", "secret");
    char up[512];
    snprintf(up, sizeof up, "%s/SYSTEM.REG.bak", e.data);
    tmp_put(e.data, "SYSTEM.REG.bak", "secret");
    const char *files[] = { reg, up, NULL };
    e.in.log_files = files;
    char out[512], buf[4096];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(!tmp_exists(e.logs, "support/2026-11-17_2105/user.reg"));
    CHECK(!tmp_exists(e.logs, "support/2026-11-17_2105/SYSTEM.REG.bak"));
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/MANIFEST.txt", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "user.reg") != NULL && strstr(buf, "skipped") != NULL);
    tmp_rm_rf(e.root);
    (void)save;
}

static void copies_to_usb_when_given(void)
{
    Env e; env_new(&e);
    char usb[512];
    snprintf(usb, sizeof usb, "%s/usb0", e.root);
    mkdir(usb, 0755);
    e.in.usb_dir = usb;
    char out[512];
    int ok = 0;
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, &ok), 0);
    CHECK_EQ(ok, 1);
    CHECK(tmp_exists(usb, "Jk4Speed-support/2026-11-17_2105/MANIFEST.txt"));
    CHECK(tmp_exists(usb, "Jk4Speed-support/2026-11-17_2105/session-5.log"));
    /* a USB folder that is not there: the bundle is still saved, usb 0 */
    snprintf(usb, sizeof usb, "%s/no-usb", e.root);
    e.in.stamp = "second";
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, &ok), 0);
    CHECK_EQ(ok, 0);
    tmp_rm_rf(e.root);
}

static void missing_expected_listed(void)
{
    Env e; env_new(&e);
    char p[512];
    snprintf(p, sizeof p, "%s/radv.log", e.data); /* wrong place on purpose: radv.log lives in the runtime root */
    char rm[512];
    snprintf(rm, sizeof rm, "%s/radv.log", e.runtime);
    unlink(rm);
    char out[512], buf[4096];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/MANIFEST.txt", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "missing") != NULL && strstr(buf, "radv.log") != NULL);
    tmp_rm_rf(e.root);
    (void)p;
}

static void no_stamp_numbered(void)
{
    Env e; env_new(&e);
    e.in.stamp = NULL;
    char out[512];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_exists(e.logs, "support/bundle-1/MANIFEST.txt"));
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_exists(e.logs, "support/bundle-2/MANIFEST.txt"));
    /* the same stamp twice: the second gets a suffix, nothing is overwritten */
    e.in.stamp = "x";
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_exists(e.logs, "support/x-2/MANIFEST.txt"));
    tmp_rm_rf(e.root);
}

static void without_data_root_fails(void)
{
    JkSupportIn in = { 0 };
    char out[64];
    CHECK(jk_support_bundle(&in, out, sizeof out, NULL) < 0);
}

static void stamp_cannot_escape(void)
{
    Env e; env_new(&e);
    e.in.stamp = "../../evil";
    char out[512];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(strstr(out, "..") == NULL);
    CHECK(!tmp_exists(e.root, "evil"));
    tmp_rm_rf(e.root);
}

static void launcher_log_appends_and_rotates(void)
{
    Env e; env_new(&e);
    jk_log_event(e.data, "open state=ready");
    jk_log_event(e.data, "start");
    char buf[256];
    CHECK(tmp_get(e.data, "state/launcher.log", buf, sizeof buf) > 0);
    CHECK(!strcmp(buf, "open state=ready\nstart\n"));
    char line[1001];
    memset(line, 'x', 1000); line[1000] = 0;
    for (int i = 0; i < 300; i++) jk_log_event(e.data, line);   /* > 256 KiB */
    CHECK(tmp_exists(e.data, "state/launcher.log.1"));
    char p[512];
    snprintf(p, sizeof p, "%s/state/launcher.log", e.data);
    CHECK(jk_fs_size(p) <= JK_LAUNCHER_LOG_MAX);
    /* the bundle includes it */
    char out[512];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/launcher.log"));
    tmp_rm_rf(e.root);
}

/* Minor 1: the bundle carries the profile r4 composes (System/profiles/nfsmw.profile). */
static void bundle_has_r4_profile(void)
{
    Env e; env_new(&e);
    tmp_put(e.prof, "nfsmw.profile", "[application]\nprefix = default\n");
    char out[512], buf[4096];
    CHECK_EQ(jk_support_bundle(&e.in, out, sizeof out, NULL), 0);
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/nfsmw.profile", buf, sizeof buf) > 0 && strstr(buf, "prefix = default"));
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/MANIFEST.txt", buf, sizeof buf) > 0 && strstr(buf, "nfsmw.profile"));
    CHECK(!tmp_exists(e.logs, "support/2026-11-17_2105/nfsmw-x360.profile"));
    tmp_rm_rf(e.root);
}

int main(void)
{
    RUN(bundle_has_r4_profile);
    RUN(bundle_has_manifest_and_logs);
    RUN(bundle_has_engine_logs);
    RUN(newest_three_sessions);
    RUN(caps_large_log);
    RUN(masks_local_user);
    RUN(never_copies_reg);
    RUN(copies_to_usb_when_given);
    RUN(missing_expected_listed);
    RUN(no_stamp_numbered);
    RUN(without_data_root_fails);
    RUN(stamp_cannot_escape);
    RUN(launcher_log_appends_and_rotates);
    return jk_test_report("test_support");
}
