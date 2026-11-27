/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Mods from a USB drop folder (<usb>/Jk4Speed/mods/): the X360 Stuff Pack
 * files and the HD road/HUD files, each verified against its pinned hash,
 * backed up first and restorable. The package never carries them. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_manifest.h"
#include "jk_mods.h"
#include "jk_patcher.h"
#include "jk_sha256.h"

#define EXE "speed exe v1.3"

typedef struct Env { char root[256], game[300], data[300], pkg[300], prof[300], mods[300]; JkPaths p; } Env;

static void sha_of(const char *s, char out[65]) { jk_sha256_hex(s, strlen(s), out); }

/* A test table over real mod paths with our own contents. */
static char t_sha[4][65];
static JkModFile table[4];
static const char *const T_PATH[4] = { "scripts/X360Stuff.asi", "FX/COMPILED/car.fx.o", "GLOBAL/InGameA.bun",
                                       "TRACKS/STREAML2RA.BUN" };
static const char *const T_TEXT[4] = { "x360 asi", "car shader", "hd hud", "hd road" };

static void table_init(void)
{
    for (int i = 0; i < 4; i++) {
        sha_of(T_TEXT[i], t_sha[i]);
        table[i] = (JkModFile){ T_PATH[i], t_sha[i], (long long)strlen(T_TEXT[i]), i < 2 ? JK_MOD_PACK : JK_MOD_BUILD, NULL };
    }
}

static void env_new(Env *e)
{
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->game, sizeof e->game, "%s/game", e->root);
    snprintf(e->data, sizeof e->data, "%s/data", e->root);
    snprintf(e->pkg, sizeof e->pkg, "%s/pkg", e->root);
    snprintf(e->prof, sizeof e->prof, "%s/profiles", e->root);
    snprintf(e->mods, sizeof e->mods, "%s/usb0/Jk4Speed/mods", e->root);
    mkdir(e->data, 0755);
    mkdir(e->prof, 0755);
    char he[65], m[512];
    sha_of(EXE, he);
    snprintf(m, sizeof m, "exe speed.exe %s\nrequire GLOBAL\n", he);
    tmp_put(e->pkg, "manifest.txt", m);
    tmp_put(e->game, "speed.exe", EXE);
    tmp_put(e->game, "GLOBAL/InGameA.bun", "EA hud");
    tmp_put(e->game, "TRACKS/STREAML2RA.BUN", "EA road");
    tmp_put(e->game, "FX/COMPILED/car.fx.o", "EA car shader");
    for (int i = 0; i < 4; i++) tmp_put(e->mods, T_PATH[i], T_TEXT[i]);
    memset(&e->p, 0, sizeof e->p);                      /* JkPaths grew in 1.1: exe_any / profile_key start off */
    e->p.game_dir = e->game; e->p.data_root = e->data; e->p.files_dir = e->pkg; e->p.profiles_dir = e->prof;
}

/* The built-in table is the engine-1.0 release set (engine-1.0-manifest 2.1/2.2). */
static void builtin_table_is_the_release_set(void)
{
    int n = 0;
    const JkModFile *t = jk_mods_table(&n);
    CHECK_EQ(n, 29);                                  /* 4 HD/our builds of HUD/road, 7 scripts/GLOBAL, 18 shaders */
    const JkModFile *road = jk_mods_find("TRACKS/STREAML2RA.BUN");
    CHECK(road && !strcmp(road->sha, "e2a7a504a2563b72ef0674108cbc14dfb24b1896c2e880ca937d4e95e161c226"));
    CHECK(road && road->size == 532663556LL && road->src == JK_MOD_BUILD);
    const JkModFile *asi = jk_mods_find("scripts/X360Stuff.asi");
    CHECK(asi && !strcmp(asi->sha, "70171ad75d5ccdf6032bed161fbcdad6a0799b2f7c717af6d977e3571dd185bf") && asi->src == JK_MOD_PACK);
    CHECK(jk_mods_find("speed.exe") == NULL);
    CHECK(jk_mods_find("scripts/X360Stuff.pdb") == NULL);     /* debug symbols: not needed */
    const JkModFile *huds = jk_mods_find("GLOBAL/HUDS_Custom_00.bin");
    CHECK(huds && !strcmp(huds->sha, "a67a45eb357f0a1479b84ad568891f6368aeb69e5259861c1472eb1d2c2b2ffd"));  /* JDLZ fix */
    for (int i = 0; i < n; i++) {
        CHECK((t[i].src == JK_MOD_BUILD) == (t[i].delta != NULL));     /* every build has its delta */
        CHECK(strlen(t[i].sha) == 64);
        CHECK(t[i].size > 0);
        CHECK(jk_manifest_path_allowed(JK_M_FILE, t[i].path));  /* backups of them can be restored */
    }
}

/* The package may not carry a mod file: it comes from the player's USB drive. */
static void package_cannot_carry_mods(void)
{
    JkManifest m;
    char t[512];
    snprintf(t, sizeof t, "exe speed.exe %s\nfile GLOBAL/InGameA.bun "
             "9825274a478bafa22c0b61f2059552ca45c6c14b8052a1884e5bc26d20069b59 11755264\n",
             "80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c");
    CHECK(jk_manifest_parse(t, &m) < 0);
    CHECK(strstr(m.err, "mods") != NULL);
    snprintf(t, sizeof t, "exe speed.exe %s\nfile FX/COMPILED/car.fx.o "
             "46d45c0bdbc921ff1ba92d7b0362fe243495e3e00fbff5f9c2a8be2aa4e50d83 36116\n",
             "80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c");
    CHECK(jk_manifest_parse(t, &m) < 0);
}

static void scan_counts_found_ok_bad_installed(void)
{
    table_init();
    Env e; env_new(&e);
    tmp_put(e.mods, "GLOBAL/InGameA.bun", "a different hud");       /* wrong build */
    char gone[512];
    snprintf(gone, sizeof gone, "%s/FX/COMPILED/car.fx.o", e.mods);
    unlink(gone);                                                    /* not on the drive */
    tmp_put(e.game, "scripts/X360Stuff.asi", "x360 asi");            /* already in the game */
    JkModsScan s;
    CHECK_EQ(jk_mods_scan_table(table, 4, e.mods, e.game, &s), 0);
    CHECK_EQ(s.total, 4);
    CHECK_EQ(s.found, 3);
    CHECK_EQ(s.ok, 2);
    CHECK_EQ(s.bad, 1);
    CHECK(!strcmp(s.first_bad, "GLOBAL/InGameA.bun"));
    CHECK_EQ(s.installed, 1);
    CHECK_EQ(jk_mods_scan_table(table, 4, NULL, e.game, &s), 0);    /* no USB: nothing found */
    CHECK_EQ(s.found, 0);
    CHECK_EQ(s.installed, 1);
    tmp_rm_rf(e.root);
}

static void install_backs_up_and_restore_returns_originals(void)
{
    table_init();
    Env e; env_new(&e);
    char why[200] = "";
    int done = -1;
    CHECK_EQ(jk_mods_install_table(table, 4, &e.p, e.mods, NULL, NULL, &done, why, sizeof why), 0);
    if (why[0]) fprintf(stderr, "    why: %s\n", why);
    CHECK_EQ(done, 4);
    char buf[64];
    CHECK(tmp_get(e.game, "GLOBAL/InGameA.bun", buf, sizeof buf) > 0 && !strcmp(buf, "hd hud"));
    CHECK(tmp_get(e.game, "TRACKS/STREAML2RA.BUN", buf, sizeof buf) > 0 && !strcmp(buf, "hd road"));
    CHECK(tmp_get(e.game, "FX/COMPILED/car.fx.o", buf, sizeof buf) > 0 && !strcmp(buf, "car shader"));
    CHECK(tmp_get(e.game, "scripts/X360Stuff.asi", buf, sizeof buf) > 0 && !strcmp(buf, "x360 asi"));
    CHECK(tmp_get(e.data, "backup/GLOBAL/InGameA.bun", buf, sizeof buf) > 0 && !strcmp(buf, "EA hud"));
    CHECK(tmp_get(e.data, "backup/FX/COMPILED/car.fx.o", buf, sizeof buf) > 0 && !strcmp(buf, "EA car shader"));
    JkModsScan s;
    jk_mods_scan_table(table, 4, e.mods, e.game, &s);
    CHECK_EQ(s.installed, 4);
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    CHECK(tmp_get(e.game, "GLOBAL/InGameA.bun", buf, sizeof buf) > 0 && !strcmp(buf, "EA hud"));
    CHECK(tmp_get(e.game, "TRACKS/STREAML2RA.BUN", buf, sizeof buf) > 0 && !strcmp(buf, "EA road"));
    CHECK(tmp_get(e.game, "FX/COMPILED/car.fx.o", buf, sizeof buf) > 0 && !strcmp(buf, "EA car shader"));
    CHECK(!tmp_exists(e.game, "scripts/X360Stuff.asi"));             /* added by us: removed */
    tmp_rm_rf(e.root);
}

/* A wrong file on the drive is never installed; the rest is. */
static void wrong_hash_skipped_and_reported(void)
{
    table_init();
    Env e; env_new(&e);
    tmp_put(e.mods, "TRACKS/STREAML2RA.BUN", "someone else's road");
    char why[200] = "";
    int done = -1;
    CHECK_EQ(jk_mods_install_table(table, 4, &e.p, e.mods, NULL, NULL, &done, why, sizeof why), 0);
    CHECK_EQ(done, 3);
    char buf[64];
    CHECK(tmp_get(e.game, "TRACKS/STREAML2RA.BUN", buf, sizeof buf) > 0 && !strcmp(buf, "EA road"));
    CHECK(!tmp_exists(e.data, "backup/TRACKS/STREAML2RA.BUN"));
    tmp_rm_rf(e.root);
}

static void refused_without_game_or_mods(void)
{
    table_init();
    Env e; env_new(&e);
    char why[200] = "";
    int done = -1;
    tmp_put(e.game, "speed.exe", "speed exe v1.0");
    CHECK(jk_mods_install_table(table, 4, &e.p, e.mods, NULL, NULL, &done, why, sizeof why) < 0);
    CHECK(strstr(why, "version") != NULL);
    tmp_put(e.game, "speed.exe", EXE);
    char none[400];
    snprintf(none, sizeof none, "%s/none", e.root);
    CHECK(jk_mods_install_table(table, 4, &e.p, none, NULL, NULL, &done, why, sizeof why) < 0);
    CHECK(strstr(why, "Jk4Speed/mods") != NULL);
    tmp_rm_rf(e.root);
}

/* Installing mods keeps the package's own record: Restore still puts back both. */
static void mods_and_package_share_the_record(void)
{
    table_init();
    Env e; env_new(&e);
    char hd[65], m[512];
    sha_of(EXE, hd);
    char h_d3d9[65];
    sha_of("our d3d9", h_d3d9);
    snprintf(m, sizeof m, "exe speed.exe %s\nrequire GLOBAL\nfile d3d9.dll %s 8\n", hd, h_d3d9);
    tmp_put(e.pkg, "manifest.txt", m);
    tmp_put(e.pkg, "d3d9.dll", "our d3d9");
    tmp_put(e.game, "d3d9.dll", "player's d3d9");
    char why[200] = "";
    CHECK_EQ(jk_patch_install(&e.p, NULL, NULL, why, sizeof why), 0);
    int done = 0;
    CHECK_EQ(jk_mods_install_table(table, 4, &e.p, e.mods, NULL, NULL, &done, why, sizeof why), 0);
    JkCheck c;
    jk_patch_check(&e.p, &c);
    CHECK_EQ(c.state, JK_ST_READY);
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    char buf[64];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "player's d3d9"));
    CHECK(tmp_get(e.game, "GLOBAL/InGameA.bun", buf, sizeof buf) > 0 && !strcmp(buf, "EA hud"));
    tmp_rm_rf(e.root);
}

/* A BUILD file made on the console from the player's own files with the
 * package's delta (tests/fixtures/delta: a.bin + b.bin -> target.bin). */
#define FX JK_TEST_DIR "/fixtures/delta"
static char fx_sha[65];
static JkModFile built[1];
static void built_init(void)
{
    jk_fs_sha256(FX "/target.bin", fx_sha, NULL);
    built[0] = (JkModFile){ "GLOBAL/GlobalB.lzc", fx_sha, 6012, JK_MOD_BUILD, "fixture.jkd" };
}
static void copy_file(const char *from, const char *dir, const char *rel)
{
    char to[600];
    snprintf(to, sizeof to, "%s/%s", dir, rel);
    jk_fs_mkdir_parent(to);
    CHECK_EQ(jk_fs_copy_checked(from, to, NULL), 0);
}

static void builds_from_players_files_with_delta(void)
{
    built_init();
    Env e; env_new(&e);
    copy_file(FX "/fixture.jkd", e.pkg, "deltas/fixture.jkd");
    copy_file(FX "/a.bin", e.game, "GLOBAL/GlobalB.lzc");               /* source 0: the game's own file */
    copy_file(FX "/b.bin", e.mods, "original/GLOBAL/GlobalB.lzc");      /* source 1: on the USB drive */
    JkModsScan s;
    CHECK_EQ(jk_mods_scan_table(built, 1, e.mods, e.game, &s), 0);
    CHECK_EQ(s.ok, 0);
    char why[200] = "";
    int done = -1;
    CHECK_EQ(jk_mods_install_table(built, 1, &e.p, e.mods, NULL, NULL, &done, why, sizeof why), 0);
    if (why[0]) fprintf(stderr, "    why: %s\n", why);
    CHECK_EQ(done, 1);
    char got[65];
    char p[600];
    snprintf(p, sizeof p, "%s/GLOBAL/GlobalB.lzc", e.game);
    jk_fs_sha256(p, got, NULL);
    CHECK(!strcmp(got, fx_sha));
    char a_sha[65], bk_sha[65];
    jk_fs_sha256(FX "/a.bin", a_sha, NULL);
    snprintf(p, sizeof p, "%s/backup/GLOBAL/GlobalB.lzc", e.data);
    CHECK(jk_fs_sha256(p, bk_sha, NULL) == 0 && !strcmp(a_sha, bk_sha));   /* the player's file is kept */
    CHECK(!tmp_exists(e.data, "staging/GLOBAL/GlobalB.lzc"));
    CHECK_EQ(jk_patch_restore(&e.p, why, sizeof why), 0);
    snprintf(p, sizeof p, "%s/GLOBAL/GlobalB.lzc", e.game);
    jk_fs_sha256(p, got, NULL);
    CHECK(!strcmp(got, a_sha));
    tmp_rm_rf(e.root);
}

/* A mod author's file, extracted as released anywhere under
 * <usb>/Jk4Speed/mods/author/, is found by its hash. */
static void author_source_found_by_hash(void)
{
    built_init();
    Env e; env_new(&e);
    copy_file(FX "/fixture.jkd", e.pkg, "deltas/fixture.jkd");
    copy_file(FX "/a.bin", e.game, "GLOBAL/GlobalB.lzc");
    copy_file(FX "/b.bin", e.mods, "author/Upscaled HUD/assets/GLOBAL/SOME_TEXTURE.dds");
    tmp_put(e.mods, "author/readme.txt", "the author's readme");
    JkModsScan s;
    CHECK_EQ(jk_mods_scan_paths_table(built, 1, &e.p, e.mods, &s), 0);
    CHECK_EQ(s.buildable, 1);
    char why[200] = "";
    int done = -1;
    CHECK_EQ(jk_mods_install_table(built, 1, &e.p, e.mods, NULL, NULL, &done, why, sizeof why), 0);
    CHECK_EQ(done, 1);
    tmp_rm_rf(e.root);
}

static void build_needs_every_source(void)
{
    built_init();
    Env e; env_new(&e);
    copy_file(FX "/fixture.jkd", e.pkg, "deltas/fixture.jkd");
    copy_file(FX "/a.bin", e.game, "GLOBAL/GlobalB.lzc");               /* b.bin nowhere */
    char why[200] = "";
    int done = -1;
    CHECK(jk_mods_install_table(built, 1, &e.p, e.mods, NULL, NULL, &done, why, sizeof why) < 0);
    CHECK(strstr(why, "GLOBAL/GlobalB.lzc") != NULL);
    char a_sha[65], got[65], p[600];
    jk_fs_sha256(FX "/a.bin", a_sha, NULL);
    snprintf(p, sizeof p, "%s/GLOBAL/GlobalB.lzc", e.game);
    jk_fs_sha256(p, got, NULL);
    CHECK(!strcmp(got, a_sha));                                         /* untouched */
    JkModsScan s;
    CHECK_EQ(jk_mods_scan_paths_table(built, 1, &e.p, e.mods, &s), 0);
    CHECK_EQ(s.buildable, 0);
    copy_file(FX "/b.bin", e.data, "backup/GLOBAL/GlobalB.lzc");        /* found in our own backup too */
    CHECK_EQ(jk_mods_scan_paths_table(built, 1, &e.p, e.mods, &s), 0);
    CHECK_EQ(s.buildable, 1);
    tmp_rm_rf(e.root);
}

int main(void)
{
    RUN(builtin_table_is_the_release_set);
    RUN(package_cannot_carry_mods);
    RUN(scan_counts_found_ok_bad_installed);
    RUN(install_backs_up_and_restore_returns_originals);
    RUN(wrong_hash_skipped_and_reported);
    RUN(refused_without_game_or_mods);
    RUN(mods_and_package_share_the_record);
    RUN(builds_from_players_files_with_delta);
    RUN(build_needs_every_source);
    RUN(author_source_found_by_hash);
    return jk_test_report("test_mods");
}
