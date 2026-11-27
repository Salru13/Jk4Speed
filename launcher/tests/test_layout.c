/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The JkFlow-style folder layout under /data/Jk4Speed (r4). */
#include <sys/stat.h>
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_layout.h"
#include "jk_fs.h"

typedef struct Lines { char text[4096]; } Lines;
static void collect(void *ctx, const char *line)
{
    Lines *l = ctx;
    size_t n = strlen(l->text);
    snprintf(l->text + n, sizeof l->text - n, "%s\n", line);
}

static void layout_default_paths(void)
{
    JkLayout l;
    jk_layout_init(&l, NULL);
    CHECK(!strcmp(l.root, "/data/Jk4Speed"));
    CHECK(!strcmp(l.game, "/data/Jk4Speed/NFSMW"));
    CHECK(!strcmp(l.saves, "/data/Jk4Speed/Saves"));
    CHECK(!strcmp(l.saves_game, "/data/Jk4Speed/Saves/NFS Most Wanted"));
    CHECK(!strcmp(l.mods, "/data/Jk4Speed/Mods"));
    CHECK(!strcmp(l.logs, "/data/Jk4Speed/Logs"));
    CHECK(!strcmp(l.support, "/data/Jk4Speed/Logs/support"));
    CHECK(!strcmp(l.devtools, "/data/Jk4Speed/devtools"));
    CHECK(!strcmp(l.system, "/data/Jk4Speed/System"));
    CHECK(!strcmp(l.prefix, "/data/Jk4Speed/System/prefix"));
    CHECK(!strcmp(l.backup, "/data/Jk4Speed/System/backup"));
    CHECK(!strcmp(l.pristine, "/data/Jk4Speed/System/backup/pristine"));
    CHECK(!strcmp(l.modset, "/data/Jk4Speed/System/modset"));
    CHECK(!strcmp(l.state, "/data/Jk4Speed/System/state"));
    CHECK(!strcmp(l.cache, "/data/Jk4Speed/System/cache"));
    CHECK(!strcmp(l.profiles, "/data/Jk4Speed/System/profiles"));
    CHECK(!strcmp(JK_PRODUCT_ROOT, "/data/Jk4Speed"));
}

/* r5: the folders a player fills over FTP or that the title writes as another
 * user are 0777 (console 2026-11-24: FTP STOR into a 0755 Mods got 550),
 * also when an earlier launcher made them 0755. */
static void layout_dirs_0777(void)
{
    char root[256], p[600];
    tmp_dir(root, sizeof root);
    snprintf(p, sizeof p, "%s/Jk4Speed", root);
    JkLayout l;
    jk_layout_init(&l, p);
    mkdir(p, 0755);
    mkdir(l.mods, 0755);                               /* made by r4 */
    CHECK_EQ(jk_layout_make(&l), 0);
    const char *dirs[] = { l.root, l.saves, l.mods, l.logs, l.system };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        struct stat st;
        CHECK(stat(dirs[i], &st) == 0 && (st.st_mode & 0777) == 0777);
    }
    tmp_rm_rf(root);
}

/* M-1: a Saves-NFS or System/nfs that cannot be made never keeps the 0777 off the 1.01 folders */
static void layout_nfs_failure_keeps_0777(void)
{
    char root[256], p[600];
    tmp_dir(root, sizeof root);
    snprintf(p, sizeof p, "%s/Jk4Speed", root);
    JkLayout l;
    jk_layout_init(&l, p);
    mkdir(p, 0755);
    mkdir(l.saves, 0755);
    mkdir(l.mods, 0755);
    mkdir(l.logs, 0755);
    mkdir(l.system, 0755);
    tmp_put(p, "Saves-NFS", "a file in the way");
    CHECK_EQ(jk_layout_make(&l), -1);                  /* the failure is still reported */
    const char *dirs[] = { l.root, l.saves, l.mods, l.logs, l.system };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        struct stat st;
        CHECK(stat(dirs[i], &st) == 0 && (st.st_mode & 0777) == 0777);
    }
    tmp_rm_rf(root);
    tmp_dir(root, sizeof root);
    snprintf(p, sizeof p, "%s/Jk4Speed", root);
    jk_layout_init(&l, p);
    mkdir(p, 0755);
    mkdir(l.system, 0755);
    mkdir(l.saves, 0755);
    tmp_put(l.system, "nfs", "a file in the way");
    CHECK_EQ(jk_layout_make(&l), -1);
    struct stat st;
    CHECK(stat(l.saves, &st) == 0 && (st.st_mode & 0777) == 0777);
    CHECK(stat(l.system, &st) == 0 && (st.st_mode & 0777) == 0777);
    tmp_rm_rf(root);
}

static void layout_make_creates_all_but_game(void)
{
    char root[256], p[600];
    tmp_dir(root, sizeof root);
    snprintf(p, sizeof p, "%s/Jk4Speed", root);
    JkLayout l;
    jk_layout_init(&l, p);
    CHECK_EQ(jk_layout_make(&l), 0);
    const char *dirs[] = { l.root, l.saves, l.mods, l.logs, l.support, l.devtools, l.system, l.prefix, l.backup,
                           l.pristine, l.modset, l.state, l.cache, l.profiles };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) CHECK(jk_fs_is_dir(dirs[i]));
    CHECK(!jk_fs_exists(l.game));
    CHECK(!jk_fs_exists(l.saves_game));
    CHECK_EQ(jk_layout_make(&l), 0);                   /* again: fine */
    tmp_rm_rf(root);
}

static void game_dir_found_any_case(void)
{
    char root[256], out[400], other[400];
    tmp_dir(root, sizeof root);
    tmp_put(root, "jk4speed/nfsmw/speed.exe", "x");
    char base[300];
    snprintf(base, sizeof base, "%s/jk4speed", root);
    CHECK_EQ(jk_game_dir_find(base, out, sizeof out, other, sizeof other), 1);
    size_t n = strlen(out);
    CHECK(n > 6 && !strcmp(out + n - 6, "/nfsmw"));
    CHECK_EQ(other[0], 0);
    tmp_rm_rf(root);
}

static void game_dir_two_spellings_reported(void)
{
    char root[256], out[400], other[400];
    tmp_dir(root, sizeof root);
    tmp_put(root, "NFSMW/a", "x");
    tmp_put(root, "nfsmw/b", "x");
    CHECK_EQ(jk_game_dir_find(root, out, sizeof out, other, sizeof other), -1);
    CHECK(strstr(out, "/NFSMW") != NULL);
    CHECK(strstr(other, "/nfsmw") != NULL);
    CHECK(strcmp(out, other) != 0);
    tmp_rm_rf(root);
}

static void game_dir_missing(void)
{
    char root[256], out[400], other[400], want[500];
    tmp_dir(root, sizeof root);
    tmp_put(root, "NFSMW-X360/a", "x");                /* another name: not the game */
    CHECK_EQ(jk_game_dir_find(root, out, sizeof out, other, sizeof other), 0);
    snprintf(want, sizeof want, "%s/NFSMW", root);
    CHECK(!strcmp(out, want));
    tmp_rm_rf(root);
}

static void migrate_r3_state_aside_never_overwrites(void)
{
    char root[256], p[600];
    tmp_dir(root, sizeof root);
    snprintf(p, sizeof p, "%s/Jk4Speed", root);
    JkLayout l;
    jk_layout_init(&l, p);
    tmp_put(l.root, "state/installed.txt", "r3 record");
    tmp_put(l.root, "logs/launcher.log", "r3 log");
    tmp_put(l.root, "modset/choice.txt", "c");
    tmp_put(l.root, "backup/speed.exe", "b");
    tmp_put(l.root, "registry/user.reg", "r");
    tmp_put(l.root, "support/b1/MANIFEST.txt", "m");
    tmp_put(l.root, "NFSMW/speed.exe", "game");
    tmp_put(l.root, "NFSMW-X360/speed.exe", "old game");
    tmp_put(l.root, "mystuff/file", "u");
    /* an earlier partial run left a different state there */
    tmp_put(l.system, "legacy-r3/state/installed.txt", "kept");
    Lines lines = { "" };
    CHECK_EQ(jk_layout_migrate_r3(&l, collect, &lines), 6);
    CHECK(!jk_fs_exists("/nonexistent"));
    char b[64];
    CHECK(tmp_get(l.system, "legacy-r3/state/installed.txt", b, sizeof b) > 0 && !strcmp(b, "kept"));
    CHECK(tmp_get(l.system, "legacy-r3/state.old-r3/installed.txt", b, sizeof b) > 0 && !strcmp(b, "r3 record"));
    CHECK(tmp_exists(l.system, "legacy-r3/logs/launcher.log"));
    CHECK(tmp_exists(l.system, "legacy-r3/modset/choice.txt"));
    CHECK(tmp_exists(l.system, "legacy-r3/backup/speed.exe"));
    CHECK(tmp_exists(l.system, "legacy-r3/registry/user.reg"));
    CHECK(tmp_exists(l.system, "legacy-r3/support/b1/MANIFEST.txt"));
    CHECK(!tmp_exists(l.root, "state"));
    CHECK(!tmp_exists(l.root, "logs"));
    CHECK(tmp_exists(l.root, "NFSMW/speed.exe"));
    CHECK(tmp_exists(l.root, "NFSMW-X360/speed.exe"));
    CHECK(tmp_exists(l.root, "mystuff/file"));
    CHECK(strstr(lines.text, "state") != NULL);
    /* second run: nothing left to move */
    CHECK_EQ(jk_layout_migrate_r3(&l, collect, &lines), 0);
    tmp_rm_rf(root);
}

static void nfs_leaf_and_paths(void)
{
    char root[256], out[300], other[300];
    tmp_dir(root, sizeof root);
    char d[400];
    snprintf(d, sizeof d, "%s/nfs", root); mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/NFSMW", root); mkdir(d, 0755);
    CHECK_EQ(jk_game_dir_find_leaf(root, "NFS", out, sizeof out, other, sizeof other), 1);
    CHECK(strstr(out, "/nfs") && !strstr(out, "NFSMW"));
    CHECK_EQ(jk_game_dir_find_leaf(root, "NFSMW", out, sizeof out, other, sizeof other), 1);
    CHECK(strstr(out, "/NFSMW"));
    JkLayout l;
    jk_layout_init(&l, "/data/Jk4Speed");
    CHECK(!strcmp(l.game_nfs, "/data/Jk4Speed/NFS"));
    CHECK(!strcmp(l.saves_nfs, "/data/Jk4Speed/Saves-NFS"));
    CHECK(!strcmp(l.saves_nfs_game, "/data/Jk4Speed/Saves-NFS/NFS Most Wanted"));
    CHECK(!strcmp(l.system_nfs, "/data/Jk4Speed/System/nfs"));
    tmp_rm_rf(root);
    /* make creates Saves-NFS and System/nfs */
    tmp_dir(root, sizeof root);
    snprintf(d, sizeof d, "%s/Jk4Speed", root);
    jk_layout_init(&l, d);
    CHECK_EQ(jk_layout_make(&l), 0);
    CHECK(jk_fs_is_dir(l.saves_nfs) && jk_fs_is_dir(l.system_nfs));
    CHECK(!jk_fs_exists(l.game_nfs));
    tmp_rm_rf(root);
}

static void nfs_two_spellings(void)
{
    char root[256], out[300], other[300], d[400];
    tmp_dir(root, sizeof root);
    snprintf(d, sizeof d, "%s/nfs", root); mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/NFS", root); mkdir(d, 0755);
    CHECK_EQ(jk_game_dir_find_leaf(root, "NFS", out, sizeof out, other, sizeof other), -1);
    CHECK(out[0] && other[0] && strcmp(out, other) != 0);
    CHECK_EQ(jk_game_dir_find_leaf(root, "nfs", out, sizeof out, other, sizeof other), -1);
    tmp_rm_rf(root);
    tmp_dir(root, sizeof root);
    CHECK_EQ(jk_game_dir_find_leaf(root, "NFS", out, sizeof out, other, sizeof other), 0);
    CHECK(strstr(out, "/NFS"));
    tmp_rm_rf(root);
}

int main(void)
{
    RUN(layout_default_paths);
    RUN(layout_make_creates_all_but_game);
    RUN(layout_dirs_0777);
    RUN(layout_nfs_failure_keeps_0777);
    RUN(game_dir_found_any_case);
    RUN(game_dir_two_spellings_reported);
    RUN(game_dir_missing);
    RUN(nfs_leaf_and_paths);
    RUN(nfs_two_spellings);
    RUN(migrate_r3_state_aside_never_overwrites);
    return jk_test_report("test_layout");
}
