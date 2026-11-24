/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Patcher: readiness, install, backup, restore (spec 8). Runs on temp folders
 * with a fake game and a fake package. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_patcher.h"
#include "jk_sha256.h"
#include "jk_elf.h"
#include "jk_elfdigest.h"

typedef struct Env { char root[256], game[300], data[300], pkg[300], prof[300]; JkPaths p; } Env;

static void sha_of(const char *s, char out[65]) { jk_sha256_hex(s, strlen(s), out); }

/* Package files (contents) used by every test. */
#define D3D9 "our d3d9 v2"
#define DINPUT "our loader"
#define ASI "our asi"
#define CFG "FPSLimit = 60\n"
#define REG "REGEDIT4\n"
#define EXE "speed exe v1.3"
#define INI30 "[jk4speed]\r\nA=1\r\nDXVK_PS4_NFS_PACE=30\r\n"
#define INI60 "[jk4speed]\r\nA=1\r\nDXVK_PS4_NFS_PACE=60\r\n"
#define PROFILE "executable = Z:\\data\\Jk4Speed\\NFSMW-X360\\speed.exe\nprefix = nfsmw\n"
#define PROFILE_NAME "nfsmw-x360.profile"

static void write_manifest(Env *e, const char *d3d9_content)
{
    char h_exe[65], h_d3d9[65], h_din[65], h_asi[65], h_cfg[65], h_reg[65], h_30[65], h_60[65], h_prof[65], m[3072];
    sha_of(EXE, h_exe); sha_of(d3d9_content, h_d3d9); sha_of(DINPUT, h_din); sha_of(ASI, h_asi);
    sha_of(CFG, h_cfg); sha_of(REG, h_reg); sha_of(INI30, h_30); sha_of(INI60, h_60); sha_of(PROFILE, h_prof);
    tmp_put(e->pkg, "d3d9.dll", d3d9_content);
    tmp_put(e->pkg, "jk4speed-dev.ini", INI30);
    tmp_put(e->pkg, PROFILE_NAME, PROFILE);
    snprintf(m, sizeof m,
             "# test package\n"
             "exe speed.exe %s\n"
             "require GLOBAL\n"
             "require CARS/x.bin\n"
             "file d3d9.dll %s %zu\n"
             "file dinput8.dll %s %zu\n"
             "file scripts/Fix.asi %s %zu\n"
             "config scripts/Fix.cfg %s %zu\n"
             "remove scripts/Old.asi\n"
             "remove dsound.dll\n"
             "seed nfsmw.reg %s %zu\n"
             "file jk4speed-dev.ini %s %zu\n"
             "accept jk4speed-dev.ini %s\n"
             "profile " PROFILE_NAME " %s %zu\n",
             h_exe, h_d3d9, strlen(d3d9_content), h_din, strlen(DINPUT), h_asi, strlen(ASI), h_cfg, strlen(CFG),
             h_reg, strlen(REG), h_30, strlen(INI30), h_60, h_prof, strlen(PROFILE));
    tmp_put(e->pkg, "manifest.txt", m);
}

static void env_new(Env *e, int with_game)
{
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->game, sizeof e->game, "%s/game", e->root);
    snprintf(e->data, sizeof e->data, "%s/data", e->root);
    snprintf(e->pkg, sizeof e->pkg, "%s/pkg", e->root);
    snprintf(e->prof, sizeof e->prof, "%s/profiles", e->root);
    mkdir(e->data, 0755);
    mkdir(e->prof, 0755);
    tmp_put(e->pkg, "dinput8.dll", DINPUT);
    tmp_put(e->pkg, "scripts/Fix.asi", ASI);
    tmp_put(e->pkg, "scripts/Fix.cfg", CFG);
    tmp_put(e->pkg, "nfsmw.reg", REG);
    write_manifest(e, D3D9);
    if (with_game) {
        tmp_put(e->game, "speed.exe", EXE);
        tmp_put(e->game, "GLOBAL/a.bin", "data");
        tmp_put(e->game, "CARS/x.bin", "car");
    }
    e->p.game_dir = e->game; e->p.data_root = e->data; e->p.files_dir = e->pkg; e->p.profiles_dir = e->prof;
}

static JkState state_of(Env *e)
{
    JkCheck c;
    jk_patch_check(&e->p, &c);
    return c.state;
}

/* The launcher composed this ini: its hash in state/composed.txt (jk_config.h). */
static void composed_ini(Env *e, const char *text)
{
    char h[65], rec[100];
    tmp_put(e->game, "jk4speed-dev.ini", text);
    sha_of(text, h);
    snprintf(rec, sizeof rec, "ini %s\n", h);
    tmp_put(e->data, "state/composed.txt", rec);
}

static int install(Env *e)
{
    char why[256] = "";
    int r = jk_patch_install(&e->p, NULL, NULL, why, sizeof why);
    if (r) fprintf(stderr, "    install: %s\n", why);
    return r;
}

/* sha256 over every file name and content under dir (sorted walk). */
static void tree_hash_rec(JkSha256 *s, const char *dir, const char *rel)
{
    char p[1024];
    snprintf(p, sizeof p, "%s%s%s", dir, *rel ? "/" : "", rel);
    struct dirent **list;
    int n = scandir(p, &list, NULL, alphasort);
    if (n < 0) {
        char h[65];
        if (jk_fs_sha256(p, h, NULL) == 0) { jk_sha256_update(s, rel, strlen(rel)); jk_sha256_update(s, h, 64); }
        return;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i]->d_name, ".") && strcmp(list[i]->d_name, "..")) {
            char r[1024];
            snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", list[i]->d_name);
            tree_hash_rec(s, dir, r);
        }
        free(list[i]);
    }
    free(list);
}
static void tree_hash(const char *dir, char out[65])
{
    JkSha256 s;
    jk_sha256_init(&s);
    tree_hash_rec(&s, dir, "");
    jk_sha256_final_hex(&s, out);
}

/* ---- readiness ---- */

static void no_game_folder(void)
{
    Env e; env_new(&e, 0);
    CHECK_EQ(state_of(&e), JK_ST_NO_GAME);
    mkdir(e.game, 0755);                               /* folder without speed.exe */
    CHECK_EQ(state_of(&e), JK_ST_NO_GAME);
    tmp_rm_rf(e.root);
}

static void wrong_exe(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "speed.exe", "speed exe v1.0");
    CHECK_EQ(state_of(&e), JK_ST_WRONG_EXE);
    CHECK(install(&e) < 0);
    CHECK(!tmp_exists(e.game, "d3d9.dll"));
    tmp_rm_rf(e.root);
}

static void missing_reports_first(void)
{
    Env e; env_new(&e, 1);
    tmp_rm_rf(e.game); tmp_put(e.game, "speed.exe", EXE);
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_MISSING_FILES);
    CHECK(!strcmp(c.first_missing, "GLOBAL"));
    CHECK_EQ(c.missing_count, 2);
    CHECK(install(&e) < 0);
    tmp_rm_rf(e.root);
}

static void bad_package_state(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.pkg, "manifest.txt", "file ../../etc/passwd x 1\n");
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_BAD_PACKAGE);
    CHECK(c.reason[0] != 0);
    unlink(strcat(strcpy(c.reason, e.pkg), "/manifest.txt"));
    CHECK_EQ(state_of(&e), JK_ST_BAD_PACKAGE);
    tmp_rm_rf(e.root);
}

static void fresh_game_not_installed(void)
{
    Env e; env_new(&e, 1);
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_NOT_INSTALLED);
    CHECK_EQ(c.has_backup, 0);
    tmp_rm_rf(e.root);
}

/* ---- install ---- */

static void install_then_ready(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_READY);
    CHECK_EQ(c.has_backup, 1);
    char buf[256];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, D3D9));
    CHECK(tmp_get(e.game, "scripts/Fix.asi", buf, sizeof buf) > 0 && !strcmp(buf, ASI));
    CHECK(tmp_get(e.game, "scripts/Fix.cfg", buf, sizeof buf) > 0 && !strcmp(buf, CFG));
    tmp_rm_rf(e.root);
}

static void creates_scripts_dir(void)
{
    Env e; env_new(&e, 1);
    CHECK(!tmp_exists(e.game, "scripts"));
    CHECK_EQ(install(&e), 0);
    CHECK(tmp_exists(e.game, "scripts/Fix.asi"));
    tmp_rm_rf(e.root);
}

static int progress_calls, progress_total;
static void on_progress(void *ctx, int done, int total, const char *file)
{
    (void)ctx;
    CHECK(file != NULL);
    CHECK(done >= 0 && done <= total);
    progress_calls++;
    progress_total = total;
}

static void install_reports_progress(void)
{
    Env e; env_new(&e, 1);
    char why[128];
    progress_calls = 0;
    CHECK_EQ(jk_patch_install(&e.p, on_progress, NULL, why, sizeof why), 0);
    CHECK_EQ(progress_total, 9);                       /* 4 file + 1 config + 2 remove + 1 seed + 1 profile */
    CHECK(progress_calls >= 9);
    tmp_rm_rf(e.root);
}

static void install_backs_up_differing_file(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "dinput8.dll", "someone else's loader");
    CHECK_EQ(state_of(&e), JK_ST_NOT_INSTALLED);
    CHECK_EQ(install(&e), 0);
    char buf[256];
    CHECK(tmp_get(e.data, "backup/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "someone else's loader"));
    CHECK(tmp_get(e.game, "dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, DINPUT));
    tmp_rm_rf(e.root);
}

static void backup_never_overwritten(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "dinput8.dll", "original");
    CHECK_EQ(install(&e), 0);
    tmp_put(e.game, "dinput8.dll", "changed later");      /* someone replaced ours */
    CHECK_EQ(state_of(&e), JK_ST_CHANGED);
    CHECK_EQ(install(&e), 0);
    char buf[256];
    CHECK(tmp_get(e.data, "backup/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "original"));
    /* the foreign replacement is not lost either: it is kept beside the backup */
    CHECK(tmp_get(e.data, "backup/dinput8.dll.changed", buf, sizeof buf) > 0 && !strcmp(buf, "changed later"));
    tmp_rm_rf(e.root);
}

/* ---- review findings (code review 2026-11-18) ---- */

static void planted_temp_symlink_cannot_touch_exe(void)
{
    Env e; env_new(&e, 1);
    char exe[512], link[512];
    snprintf(exe, sizeof exe, "%s/speed.exe", e.game);
    snprintf(link, sizeof link, "%s/d3d9.dll.jk4new", e.game);
    CHECK_EQ(symlink(exe, link), 0);
    install(&e);
    char got[65], want[65];
    jk_fs_sha256(exe, got, NULL); sha_of(EXE, want);
    CHECK(!strcmp(got, want));
    tmp_rm_rf(e.root);
}

static void scripts_symlink_refused(void)
{
    Env e; env_new(&e, 1);
    char outside[512], link[512];
    snprintf(outside, sizeof outside, "%s/outside", e.root);
    mkdir(outside, 0755);
    snprintf(link, sizeof link, "%s/scripts", e.game);
    CHECK_EQ(symlink(outside, link), 0);
    char why[160] = "";
    CHECK(jk_patch_install(&e.p, NULL, NULL, why, sizeof why) < 0);
    CHECK(strstr(why, "scripts") != NULL);
    CHECK(!tmp_exists(outside, "Fix.asi"));
    tmp_rm_rf(e.root);
}

static void target_symlink_refused(void)
{
    Env e; env_new(&e, 1);
    char exe[512], link[512];
    snprintf(exe, sizeof exe, "%s/speed.exe", e.game);
    snprintf(link, sizeof link, "%s/dinput8.dll", e.game);
    CHECK_EQ(symlink(exe, link), 0);
    CHECK(install(&e) < 0);
    char got[65], want[65];
    jk_fs_sha256(exe, got, NULL); sha_of(EXE, want);
    CHECK(!strcmp(got, want));
    tmp_rm_rf(e.root);
}

static void pending_survives_any_write_failure(void)
{
    for (int k = 0; k < 40; k++) {
        Env e; env_new(&e, 1);
        jk_fs_fail_after = k;
        char why[128];
        jk_patch_install(&e.p, NULL, NULL, why, sizeof why);
        jk_fs_fail_after = -1;
        CHECK_EQ(install(&e), 0);
        CHECK(tmp_exists(e.data, "registry/pending"));
        tmp_rm_rf(e.root);
    }
}

static void damaged_record_restore_returns_originals(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "d3d9.dll", "player's d3d9");
    tmp_put(e.game, "dsound.dll", "classic loader");
    CHECK_EQ(install(&e), 0);
    tmp_put(e.data, "state/installed.txt", "garbage that is not a record\n");
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK(c.has_backup);
    char why[128], buf[256];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "player's d3d9"));
    CHECK(tmp_get(e.game, "dsound.dll", buf, sizeof buf) > 0 && !strcmp(buf, "classic loader"));
    CHECK(!tmp_exists(e.game, "dinput8.dll"));                 /* ours, removed */
    tmp_rm_rf(e.root);
}

/* ---- pad-test findings (2026-11-18) ---- */

static void restore_from_changed_keeps_players_file(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "d3d9.dll", "original");
    CHECK_EQ(install(&e), 0);
    tmp_put(e.game, "d3d9.dll", "player's newer mod");
    CHECK_EQ(state_of(&e), JK_ST_CHANGED);
    char why[128], buf[256];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "original"));
    CHECK(tmp_get(e.data, "backup/d3d9.dll.changed", buf, sizeof buf) > 0 && !strcmp(buf, "player's newer mod"));
    tmp_rm_rf(e.root);
}

/* ---- engine 1.0 (owner decisions 2026-11-23) ---- */

/* r4: jk4speed-dev.ini is composed by the launcher (jk_config.h), so it is ours
 * whatever its bytes, even while the package manifest still lists it: never
 * CHANGED, and a reinstall keeps what is there. */
static void composed_ini_is_never_changed(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    char buf[256];
    tmp_put(e.game, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\nDXVK_PS4_AUTOTEST=0\r\n");
    CHECK_EQ(state_of(&e), JK_ST_READY);
    CHECK_EQ(install(&e), 0);
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "NFS_PACE=60"));
    tmp_put(e.game, "jk4speed-dev.ini", "[jk4speed]\nA=1\nDXVK_PS4_NFS_PACE=60\n");     /* LF: still ours */
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_put(e.game, "d3d9.dll", "someone else's");                      /* any other file is still CHANGED */
    CHECK_EQ(state_of(&e), JK_ST_CHANGED);
    tmp_rm_rf(e.root);
}

/* Restore takes the composed profile out only while it still has the hash the
 * launcher last wrote (state/composed.txt); a changed one stays. */
static void restore_removes_composed_ini_and_profile(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    char hp[65], hi[65], rec[300];
    sha_of("[application]\nid = nfsmw\n", hp);
    sha_of("[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\n", hi);
    snprintf(rec, sizeof rec, "ini %s\nprofile %s\n", hi, hp);
    tmp_put(e.data, "state/composed.txt", rec);
    tmp_put(e.game, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\n");
    tmp_put(e.prof, "nfsmw.profile", "[application]\nid = nfsmw\n");
    tmp_put(e.prof, "other.profile", "someone else's\n");
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(!tmp_exists(e.game, "jk4speed-dev.ini"));
    CHECK(!tmp_exists(e.prof, "nfsmw.profile"));
    CHECK(tmp_exists(e.prof, "other.profile"));
    CHECK(!tmp_exists(e.data, "state/composed.txt"));
    /* changed since it was composed: not ours to delete */
    CHECK_EQ(install(&e), 0);
    tmp_put(e.data, "state/composed.txt", rec);
    tmp_put(e.prof, "nfsmw.profile", "[application]\nid = edited by hand\n");
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_exists(e.prof, "nfsmw.profile"));
    tmp_rm_rf(e.root);
}

/* I-1: the install record names its game folder. Another folder never sees
 * it as its own: no Restore there, and nothing is put back into it. */
static void restore_ignores_other_game_dir(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "dinput8.dll", "first folder's loader");
    CHECK_EQ(install(&e), 0);
    char other[400], why[256] = "", buf[256];
    snprintf(other, sizeof other, "%s/other", e.root);
    tmp_put(other, "speed.exe", EXE);
    tmp_put(other, "GLOBAL/a.bin", "data");
    tmp_put(other, "CARS/x.bin", "car");
    tmp_put(other, "dinput8.dll", "second folder's loader");
    JkPaths q = e.p;
    q.game_dir = other;
    JkCheck c;
    jk_patch_check(&q, &c);
    CHECK_EQ(c.has_backup, 0);
    CHECK(jk_patch_restore(&q, why, sizeof why) < 0);
    CHECK(tmp_get(other, "dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "second folder's loader"));
    CHECK(tmp_exists(e.data, "state/installed.txt"));
    CHECK(tmp_get(e.data, "backup/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "first folder's loader"));
    /* PATCH there is refused while the first folder exists: its backups are not overwritten */
    CHECK(jk_patch_install(&q, NULL, NULL, why, sizeof why) < 0);
    CHECK(tmp_get(e.data, "backup/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "first folder's loader"));
    /* the first folder is gone: its record and backups are kept aside, never lost */
    tmp_rm_rf(e.game);
    CHECK_EQ(jk_patch_install(&q, NULL, NULL, why, sizeof why), 0);
    CHECK(tmp_get(e.data, "backup/kept/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "first folder's loader"));
    CHECK(tmp_get(e.data, "backup/dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "second folder's loader"));
    jk_patch_check(&q, &c);
    CHECK_EQ(c.has_backup, 1);
    CHECK_EQ(jk_patch_restore(&q, why, sizeof why), 0);
    CHECK(tmp_get(other, "dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "second folder's loader"));
    tmp_rm_rf(e.root);
}

/* The runtime profile lives outside the game folder and is covered too. */
static void profile_installed_and_restored(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.prof, PROFILE_NAME, "old profile\n");
    tmp_put(e.prof, "other.profile", "someone else's\n");
    CHECK_EQ(state_of(&e), JK_ST_NOT_INSTALLED);
    CHECK_EQ(install(&e), 0);
    char buf[256];
    CHECK(tmp_get(e.prof, PROFILE_NAME, buf, sizeof buf) > 0 && !strcmp(buf, PROFILE));
    CHECK(tmp_get(e.data, "backup/profiles/" PROFILE_NAME, buf, sizeof buf) > 0 && !strcmp(buf, "old profile\n"));
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_put(e.prof, PROFILE_NAME, "edited\n");
    CHECK_EQ(state_of(&e), JK_ST_CHANGED);
    CHECK_EQ(install(&e), 0);
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.prof, PROFILE_NAME, buf, sizeof buf) > 0 && !strcmp(buf, "old profile\n"));
    CHECK(tmp_get(e.prof, "other.profile", buf, sizeof buf) > 0 && !strcmp(buf, "someone else's\n"));
    tmp_rm_rf(e.root);
}

static void profile_new_removed_on_restore(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    CHECK(tmp_exists(e.prof, PROFILE_NAME));
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(!tmp_exists(e.prof, PROFILE_NAME));
    tmp_rm_rf(e.root);
}

static void no_profiles_folder_is_bad_package(void)
{
    Env e; env_new(&e, 1);
    e.p.profiles_dir = NULL;
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_BAD_PACKAGE);
    CHECK(strstr(c.reason, "profiles") != NULL);
    CHECK(install(&e) < 0);
    tmp_rm_rf(e.root);
}

/* The native DXVK server: /data/prospero-win/override/dxvknative.prx is
 * shared with every title on the console (Blur keeps its own server there
 * between NFS sessions), so any file found there is backed up first and
 * Restore puts the exact bytes back. */
#define SERVER "our native server enc17"
static void with_server(Env *e, char *ovr, size_t cap)
{
    char m[4096], h[65];
    snprintf(ovr, cap, "%s/override", e->root);
    e->p.override_dir = ovr;
    tmp_get(e->pkg, "manifest.txt", m, sizeof m);
    sha_of(SERVER, h);
    size_t n = strlen(m);
    snprintf(m + n, sizeof m - n, "server dxvknative.prx %s %zu\n", h, strlen(SERVER));
    tmp_put(e->pkg, "manifest.txt", m);
    tmp_put(e->pkg, "dxvknative.prx", SERVER);
}

static void server_unknown_file_backed_up_and_restored(void)
{
    Env e; env_new(&e, 1);
    char ovr[400];
    with_server(&e, ovr, sizeof ovr);
    tmp_put(ovr, "dxvknative.prx", "blur's server eb49cac4");
    tmp_put(ovr, "dxvknative.prx.before-nfs", "bench convention, never touched");
    CHECK_EQ(state_of(&e), JK_ST_NOT_INSTALLED);
    CHECK_EQ(install(&e), 0);
    char buf[128];
    CHECK(tmp_get(ovr, "dxvknative.prx", buf, sizeof buf) > 0 && !strcmp(buf, SERVER));
    CHECK(tmp_get(e.data, "backup/override/dxvknative.prx", buf, sizeof buf) > 0 && !strcmp(buf, "blur's server eb49cac4"));
    CHECK_EQ(state_of(&e), JK_ST_READY);
    char why[160];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(ovr, "dxvknative.prx", buf, sizeof buf) > 0 && !strcmp(buf, "blur's server eb49cac4"));
    CHECK(tmp_get(ovr, "dxvknative.prx.before-nfs", buf, sizeof buf) > 0 && !strcmp(buf, "bench convention, never touched"));
    tmp_rm_rf(e.root);
}

static void server_new_removed_and_ours_kept(void)
{
    Env e; env_new(&e, 1);
    char ovr[400];
    with_server(&e, ovr, sizeof ovr);
    CHECK_EQ(install(&e), 0);                          /* no server there before: added */
    CHECK(tmp_exists(ovr, "dxvknative.prx"));
    char why[160];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(!tmp_exists(ovr, "dxvknative.prx"));
    tmp_put(ovr, "dxvknative.prx", SERVER);            /* already the release server: kept, never removed */
    CHECK_EQ(install(&e), 0);
    CHECK(!tmp_exists(e.data, "backup/override/dxvknative.prx"));
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_exists(ovr, "dxvknative.prx"));
    tmp_rm_rf(e.root);
}

static void server_replaced_later_is_changed_and_kept_aside(void)
{
    Env e; env_new(&e, 1);
    char ovr[400];
    with_server(&e, ovr, sizeof ovr);
    tmp_put(ovr, "dxvknative.prx", "blur's server");
    CHECK_EQ(install(&e), 0);
    tmp_put(ovr, "dxvknative.prx", "someone's experiment");
    CHECK_EQ(state_of(&e), JK_ST_CHANGED);
    char why[160], buf[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(ovr, "dxvknative.prx", buf, sizeof buf) > 0 && !strcmp(buf, "blur's server"));
    CHECK(tmp_get(e.data, "backup/override/dxvknative.prx.changed", buf, sizeof buf) > 0 && !strcmp(buf, "someone's experiment"));
    tmp_rm_rf(e.root);
}

/* Console pre-check 2026-11-23: the installed server may be read raw (SELF)
 * or unwrapped (ELF). Raw at the pinned hash, or either form at the pinned
 * load-segment digest, is ours: kept, no backup. Another module is backed up
 * and replaced. */
static void server_recognised_raw_or_unwrapped(void)
{
    static const char *const ours[ELF_NSEG] = { "enc17 code", "enc17 0x61000010", "dyn", "enc17 data" };
    static const char *const blur[ELF_NSEG] = { "blur code", "blur 0x61000010", "dyn", "blur data" };
    static unsigned char raw[4096], form[4096];
    size_t rn = self_make(raw, ours);
    char h[65], dg[65], m[4096], ovr[400], why[160];
    for (int k = 0; k < 4; k++) {
        Env e; env_new(&e, 1);
        snprintf(ovr, sizeof ovr, "%s/override", e.root);
        e.p.override_dir = ovr;
        mkdir(ovr, 0755);
        char pkgsrv[512], dst[512];
        snprintf(pkgsrv, sizeof pkgsrv, "%s/dxvknative.prx", e.pkg);
        snprintf(dst, sizeof dst, "%s/dxvknative.prx", ovr);
        elf_write(pkgsrv, raw, rn);
        jk_sha256_hex(raw, rn, h);
        CHECK_EQ(jk_load_digest(pkgsrv, dg), 0);
        tmp_get(e.pkg, "manifest.txt", m, sizeof m);
        size_t n = strlen(m);
        snprintf(m + n, sizeof m - n, "server dxvknative.prx %s %zu\naccept dxvknative.prx %s\n", h, rn, dg);
        tmp_put(e.pkg, "manifest.txt", m);
        size_t fn = k == 0 ? self_make(form, ours) : k == 1 ? elf_make(form, ours, 0) : k == 2 ? elf_make(form, blur, 0) : self_make(form, blur);
        elf_write(dst, form, fn);
        int foreign = k >= 2;
        CHECK_EQ(install(&e), 0);
        CHECK_EQ(tmp_exists(e.data, "backup/override/dxvknative.prx"), foreign);
        static unsigned char now[4096];
        FILE *f = fopen(dst, "rb");
        size_t got = f ? fread(now, 1, sizeof now, f) : 0;
        if (f) fclose(f);
        if (foreign) CHECK(got == rn && !memcmp(now, raw, rn));            /* replaced by ours */
        else CHECK(got == fn && !memcmp(now, form, fn));                   /* ours in either form: untouched */
        CHECK_EQ(state_of(&e), JK_ST_READY);
        CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
        f = fopen(dst, "rb");
        got = f ? fread(now, 1, sizeof now, f) : 0;
        if (f) fclose(f);
        CHECK(got == fn && !memcmp(now, form, fn));                        /* what was there before, either way */
        tmp_rm_rf(e.root);
    }
}

/* profiles.lst (console pre-check: it exists and already lists ours). */
static int count_lines(const char *text, const char *name)
{
    int n = 0;
    for (const char *s = text; (s = strstr(s, name)); s += strlen(name)) n++;
    return n;
}

static void profiles_list_never_duplicated(void)
{
    static const char *const CONSOLE = "nfsmw.profile\nnfsmw-grab.profile\nmwprobe.profile\nmwprobe2.profile\n"
                                       "blur-fg.profile\nblur-racelock.profile\nblur-native248.profile\nnfsmw-x360.profile\n";
    char why[160], buf[1024];
    /* already listed (LF, CRLF, no final newline): untouched by install, install again and restore */
    const char *forms[3] = { CONSOLE, "nfsmw.profile\r\nnfsmw-x360.profile\r\n", "nfsmw.profile\nnfsmw-x360.profile  " };
    for (int k = 0; k < 3; k++) {
        Env e; env_new(&e, 1);
        tmp_put(e.prof, "profiles.lst", forms[k]);
        CHECK_EQ(install(&e), 0);
        CHECK_EQ(install(&e), 0);
        CHECK(tmp_get(e.prof, "profiles.lst", buf, sizeof buf) >= 0 && !strcmp(buf, forms[k]));
        CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
        CHECK(tmp_get(e.prof, "profiles.lst", buf, sizeof buf) >= 0 && !strcmp(buf, forms[k]));
        tmp_rm_rf(e.root);
    }
    /* not listed: added once, kept once, taken out by Restore */
    Env e; env_new(&e, 1);
    tmp_put(e.prof, "profiles.lst", "nfsmw.profile\nblur-fg.profile");
    CHECK_EQ(install(&e), 0);
    CHECK_EQ(install(&e), 0);
    CHECK(tmp_get(e.prof, "profiles.lst", buf, sizeof buf) > 0 && count_lines(buf, PROFILE_NAME) == 1);
    CHECK(!strcmp(buf, "nfsmw.profile\nblur-fg.profile\n" PROFILE_NAME "\n"));
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.prof, "profiles.lst", buf, sizeof buf) > 0 && count_lines(buf, PROFILE_NAME) == 0);
    tmp_rm_rf(e.root);
    /* no list: none made */
    env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    CHECK(!tmp_exists(e.prof, "profiles.lst"));
    tmp_rm_rf(e.root);
}

static void server_without_folder_is_bad_package(void)
{
    Env e; env_new(&e, 1);
    char ovr[400];
    with_server(&e, ovr, sizeof ovr);
    e.p.override_dir = NULL;
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_BAD_PACKAGE);
    tmp_rm_rf(e.root);
}

/* Unsupported version: the check says which speed.exe it found (short hash). */
static void wrong_exe_names_found_hash(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "speed.exe", "speed exe v1.0");
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_WRONG_EXE);
    char want[65];
    sha_of("speed exe v1.0", want);
    want[8] = 0;
    CHECK(!strcmp(c.found_exe, want));
    tmp_rm_rf(e.root);
}

/* Backups of 532 MB need room: the install refuses up front, and nothing changes. */
static long long ten_bytes_free(void *ctx, const char *path) { (void)ctx; (void)path; return 10; }
static long long plenty_free(void *ctx, const char *path) { (void)ctx; (void)path; return 1LL << 40; }
static void install_checks_free_space(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "d3d9.dll", "player's d3d9");
    char before[65], after[65];
    tree_hash(e.game, before);
    jk_fs_free_fn = ten_bytes_free;
    char why[200] = "";
    CHECK(jk_patch_install(&e.p, NULL, NULL, why, sizeof why) < 0);
    CHECK(strstr(why, "free space") != NULL);
    tree_hash(e.game, after);
    CHECK(!strcmp(before, after));                     /* nothing changed */
    CHECK(!tmp_exists(e.data, "state/installed.txt"));
    jk_fs_free_fn = plenty_free;
    CHECK_EQ(install(&e), 0);
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    jk_fs_free_fn = NULL;                              /* unknown: the install goes ahead */
    CHECK_EQ(install(&e), 0);
    tmp_rm_rf(e.root);
}

/* A damaged record: backups of the HD game files under GLOBAL/ and TRACKS/
 * come back too. */
static void damaged_record_restores_hd_backups(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    tmp_put(e.data, "backup/GLOBAL/InGameA.bun", "EA hud");
    tmp_put(e.data, "backup/TRACKS/STREAML2RA.BUN", "EA road");
    tmp_put(e.data, "backup/GLOBAL/GLOBALB.BUN", "not ours to restore");
    tmp_put(e.data, "state/installed.txt", "garbage\n");
    char why[128], buf[64];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "GLOBAL/InGameA.bun", buf, sizeof buf) > 0 && !strcmp(buf, "EA hud"));
    CHECK(tmp_get(e.game, "TRACKS/STREAML2RA.BUN", buf, sizeof buf) > 0 && !strcmp(buf, "EA road"));
    CHECK(!tmp_exists(e.game, "GLOBAL/GLOBALB.BUN"));
    tmp_rm_rf(e.root);
}

static void restore_reports_missing_backup(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "d3d9.dll", "original");
    CHECK_EQ(install(&e), 0);
    char p[512];
    snprintf(p, sizeof p, "%s/backup/d3d9.dll", e.data);
    unlink(p);
    char why[160] = "";
    CHECK(jk_patch_restore(&e.p, why, sizeof why) < 0);
    CHECK(strstr(why, "d3d9.dll") != NULL);
    CHECK(!tmp_exists(e.game, "dinput8.dll"));      /* the rest was still put back */
    tmp_rm_rf(e.root);
}

static void stale_backup_replaced(void)
{
    /* a backup left without a record entry (crash) must not beat the current file */
    Env e; env_new(&e, 1);
    tmp_put(e.data, "backup/dinput8.dll", "stale X");
    tmp_put(e.game, "dinput8.dll", "current Y");
    CHECK_EQ(install(&e), 0);
    char why[128], buf[256];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "current Y"));
    tmp_rm_rf(e.root);
}

static void reinstall_idempotent(void)
{
    Env e; env_new(&e, 1);
    char a[65], b[65], ra[65], rb[65];
    CHECK_EQ(install(&e), 0);
    tree_hash(e.game, a); tree_hash(e.data, ra);
    CHECK_EQ(install(&e), 0);
    tree_hash(e.game, b); tree_hash(e.data, rb);
    CHECK(!strcmp(a, b));
    CHECK(!strcmp(ra, rb));
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_rm_rf(e.root);
}

static void config_installed_only_when_missing(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "scripts/Fix.cfg", "player's own settings\n");
    CHECK_EQ(install(&e), 0);
    char buf[256];
    CHECK(tmp_get(e.game, "scripts/Fix.cfg", buf, sizeof buf) > 0 && !strcmp(buf, "player's own settings\n"));
    tmp_rm_rf(e.root);
}

static void config_change_is_not_changed(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    tmp_put(e.game, "scripts/Fix.cfg", "rewritten by the plugin at first start\n");
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_rm_rf(e.root);
}

static void remove_moves_to_backup_and_restore_returns(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "scripts/Old.asi", "classic fix");
    tmp_put(e.game, "dsound.dll", "classic loader");
    CHECK_EQ(state_of(&e), JK_ST_NOT_INSTALLED);
    CHECK_EQ(install(&e), 0);
    CHECK(!tmp_exists(e.game, "scripts/Old.asi"));
    CHECK(!tmp_exists(e.game, "dsound.dll"));
    char buf[256];
    CHECK(tmp_get(e.data, "backup/scripts/Old.asi", buf, sizeof buf) > 0 && !strcmp(buf, "classic fix"));
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_put(e.game, "dsound.dll", "came back");          /* a conflicting file reappears */
    CHECK(state_of(&e) != JK_ST_READY);
    tmp_rm_rf(e.root);
}

static void seed_written_with_pending(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    char buf[256];
    CHECK(tmp_get(e.data, "registry/nfsmw.reg", buf, sizeof buf) > 0 && !strcmp(buf, REG));
    CHECK(tmp_exists(e.data, "registry/pending"));
    /* the host imports it and deletes pending: still READY */
    char p[512]; snprintf(p, sizeof p, "%s/registry/pending", e.data); unlink(p);
    CHECK_EQ(state_of(&e), JK_ST_READY);
    tmp_rm_rf(e.root);
}

/* ---- restore ---- */

static void restore_returns_folder_byte_identical(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "dinput8.dll", "xidi v5");
    tmp_put(e.game, "scripts/Old.asi", "classic fix");
    tmp_put(e.game, "Xidi.ini", "player mapping");       /* not in our package: untouched */
    char before[65], after[65], exe_before[65], exe_after[65];
    tree_hash(e.game, before);
    jk_fs_sha256(strcat(strcpy((char[512]){0}, e.game), "/speed.exe"), exe_before, NULL);
    CHECK_EQ(install(&e), 0);
    composed_ini(&e, "[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\n");                       /* the composed ini goes too */
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    tree_hash(e.game, after);
    CHECK(!strcmp(before, after));
    jk_fs_sha256(strcat(strcpy((char[512]){0}, e.game), "/speed.exe"), exe_after, NULL);
    CHECK(!strcmp(exe_before, exe_after));
    CHECK(!tmp_exists(e.data, "registry/nfsmw.reg"));
    CHECK(!tmp_exists(e.data, "registry/pending"));
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_NOT_INSTALLED);
    CHECK_EQ(c.has_backup, 0);
    tmp_rm_rf(e.root);
}

static void restore_without_record_fails(void)
{
    Env e; env_new(&e, 1);
    char why[128] = "";
    CHECK(jk_patch_restore(&e.p, why, sizeof why) < 0);
    CHECK(why[0] != 0);
    tmp_rm_rf(e.root);
}

static void restore_keeps_foreign_replacement(void)
{
    /* someone replaced our d3d9.dll after install: restore must not delete theirs */
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    tmp_put(e.game, "d3d9.dll", "another mod");
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    char buf[256];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "another mod"));
    tmp_rm_rf(e.root);
}

static void never_touches_exe(void)
{
    Env e; env_new(&e, 1);
    struct stat a, b;
    char p[512]; snprintf(p, sizeof p, "%s/speed.exe", e.game);
    stat(p, &a);
    CHECK_EQ(install(&e), 0);
    char why[128];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    stat(p, &b);
    CHECK_EQ(a.st_mtime, b.st_mtime);
    CHECK_EQ(a.st_ino, b.st_ino);
    char got[65], want[65];
    jk_fs_sha256(p, got, NULL); sha_of(EXE, want);
    CHECK(!strcmp(got, want));
    tmp_rm_rf(e.root);
}

/* ---- update vs changed ---- */

static void update_vs_changed(void)
{
    Env e; env_new(&e, 1);
    CHECK_EQ(install(&e), 0);
    write_manifest(&e, "our d3d9 v3");                   /* a newer package */
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_UPDATE);
    tmp_put(e.game, "d3d9.dll", "foreign");             /* not ours, not the old ours */
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_CHANGED);
    CHECK_EQ(install(&e), 0);
    CHECK_EQ(state_of(&e), JK_ST_READY);
    char buf[256];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "our d3d9 v3"));
    tmp_rm_rf(e.root);
}

static void changed_file_backed_up_and_restored(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.game, "d3d9.dll", "player's mod");
    CHECK_EQ(install(&e), 0);
    char why[128], buf[256];
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "player's mod"));
    tmp_rm_rf(e.root);
}

/* ---- interruption ---- */

static void install_interrupted_leaves_no_partial(void)
{
    for (int k = 0; k < 40; k++) {
        Env e; env_new(&e, 1);
        tmp_put(e.game, "dinput8.dll", "original loader");
        jk_fs_fail_after = k;                            /* the (k+1)-th write fails */
        char why[128] = "";
        int r = jk_patch_install(&e.p, NULL, NULL, why, sizeof why);
        jk_fs_fail_after = -1;
        if (r != 0) CHECK(why[0] != 0);
        /* no temp file anywhere, and every target is whole: old or new */
        CHECK(!tmp_exists(e.game, "d3d9.dll.jk4new"));
        CHECK(!tmp_exists(e.game, "dinput8.dll.jk4new"));
        CHECK(!tmp_exists(e.game, "scripts/Fix.asi.jk4new"));
        char buf[256];
        int n = tmp_get(e.game, "dinput8.dll", buf, sizeof buf);
        CHECK(n > 0 && (!strcmp(buf, "original loader") || !strcmp(buf, DINPUT)));
        /* a second run finishes, and restore then gives the original back */
        CHECK_EQ(install(&e), 0);
        CHECK_EQ(state_of(&e), JK_ST_READY);
        CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
        CHECK(tmp_get(e.game, "dinput8.dll", buf, sizeof buf) > 0 && !strcmp(buf, "original loader"));
        tmp_rm_rf(e.root);
    }
}

static void package_file_corrupt_fails_cleanly(void)
{
    Env e; env_new(&e, 1);
    tmp_put(e.pkg, "dinput8.dll", "corrupted in transit");
    char why[128] = "";
    CHECK(jk_patch_install(&e.p, NULL, NULL, why, sizeof why) < 0);
    CHECK(strstr(why, "dinput8.dll") != NULL);
    CHECK(!tmp_exists(e.game, "dinput8.dll"));
    CHECK(!tmp_exists(e.game, "dinput8.dll.jk4new"));
    tmp_rm_rf(e.root);
}

int main(void)
{
    RUN(no_game_folder);
    RUN(wrong_exe);
    RUN(missing_reports_first);
    RUN(bad_package_state);
    RUN(fresh_game_not_installed);
    RUN(install_then_ready);
    RUN(creates_scripts_dir);
    RUN(install_reports_progress);
    RUN(install_backs_up_differing_file);
    RUN(backup_never_overwritten);
    RUN(reinstall_idempotent);
    RUN(config_installed_only_when_missing);
    RUN(config_change_is_not_changed);
    RUN(remove_moves_to_backup_and_restore_returns);
    RUN(seed_written_with_pending);
    RUN(restore_returns_folder_byte_identical);
    RUN(restore_without_record_fails);
    RUN(restore_keeps_foreign_replacement);
    RUN(never_touches_exe);
    RUN(update_vs_changed);
    RUN(changed_file_backed_up_and_restored);
    RUN(install_interrupted_leaves_no_partial);
    RUN(package_file_corrupt_fails_cleanly);
    RUN(planted_temp_symlink_cannot_touch_exe);
    RUN(scripts_symlink_refused);
    RUN(target_symlink_refused);
    RUN(pending_survives_any_write_failure);
    RUN(damaged_record_restore_returns_originals);
    RUN(stale_backup_replaced);
    RUN(restore_from_changed_keeps_players_file);
    RUN(composed_ini_is_never_changed);
    RUN(restore_removes_composed_ini_and_profile);
    RUN(restore_ignores_other_game_dir);
    RUN(profile_installed_and_restored);
    RUN(profile_new_removed_on_restore);
    RUN(no_profiles_folder_is_bad_package);
    RUN(wrong_exe_names_found_hash);
    RUN(server_unknown_file_backed_up_and_restored);
    RUN(server_new_removed_and_ours_kept);
    RUN(server_replaced_later_is_changed_and_kept_aside);
    RUN(server_recognised_raw_or_unwrapped);
    RUN(profiles_list_never_duplicated);
    RUN(server_without_folder_is_bad_package);
    RUN(install_checks_free_space);
    RUN(damaged_record_restores_hd_backups);
    RUN(restore_reports_missing_backup);
    return jk_test_report("test_patcher");
}
