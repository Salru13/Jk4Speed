/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The declarative mod set (launcher r4) on a small synthetic world with the
 * shape of the release catalog:
 *
 *   GLOBAL/A.bin       vanilla; X: JKD1 from the original + a pack entry
 *   GLOBAL/B.lzc       vanilla; X: the pack's UPS over the original
 *   GLOBAL/InGame.bun  vanilla; X, H (H0: without X) and X+H each their own file
 *                      (X+H is built over the X file: a nested build)
 *   GLOBAL/HUD.bin     vanilla; H: JKD1 from the original + a HUD entry
 *   TRACKS/ROAD.BUN    vanilla; R: JKD1 from the original + a road entry
 *   scripts/X.asi      X: a pack entry as it is (no file without X)
 *   scripts/Xenon.asi  X: a package file (no file without X)
 *
 * Covered: every subset from vanilla, all 64 transitions, idempotent PATCH,
 * a power cut after every rename, partial downloads, a missing download that
 * keeps its installed layer, OFF served from the cache, a refused OFF that
 * changes nothing, the pristine store (sidecar, legacy, refused without
 * space, no clean copy anywhere), bench files never touched, Restore, and
 * (JK_REAL_MODS=1) the release catalog on the real game and downloads. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fx.h"
#include "jk_fs.h"
#include "jk_modset.h"
#include "jk_sha256.h"

#include <dirent.h>
#include <sys/stat.h>

typedef struct W { char root[256], game[300], data[300], pkg[300], drop[300], deltas[340]; JkPaths p; } W;

/* contents */
static unsigned char VA[3000], VB[2500], VI[4000], VHUD[2000], VROAD[8000], XB[2500];
static unsigned char P1[900], XASI[700], H1[1100], R1[1300], XEN[500];
static unsigned char UPS[20000], XA[8192], XI[8192], H0I[8192], XHI[16384], HHUD[8192], RROAD[16384];
static size_t ups_n, xa_n, xi_n, h0i_n, xhi_n, hhud_n, rroad_n;

static char s_va[65], s_vb[65], s_vi[65], s_vhud[65], s_vroad[65], s_xb[65], s_p1[65], s_xasi[65], s_h1[65], s_r1[65],
            s_xen[65], s_ups[65], s_xa[65], s_xi[65], s_h0i[65], s_xhi[65], s_hhud[65], s_rroad[65], s_hudzip[65], s_roadzip[65];
static long long hudzip_n, roadzip_n;

#define NPATHS 7
static JkMsArchive ARCS[2];
static JkMsFile FILES[11];
static JkMsRecipe RECIPES[9];
static JkMsPath PATHS[NPATHS];
static const char *BAD[1] = { "0000000000000000000000000000000000000000000000000000000000000000" };
static JkMsCatalog CAT;

static void fill(unsigned char *b, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; b[i] = (unsigned char)(seed >> 16); }
}
static void sha(const unsigned char *d, size_t n, char out[65]) { jk_sha256_hex(d, n, out); }

static void world_contents(void)
{
    fill(VA, sizeof VA, 1); fill(VB, sizeof VB, 2); fill(VI, sizeof VI, 3); fill(VHUD, sizeof VHUD, 4); fill(VROAD, sizeof VROAD, 5);
    fill(P1, sizeof P1, 6); fill(XASI, sizeof XASI, 7); fill(H1, sizeof H1, 8); fill(R1, sizeof R1, 9); fill(XEN, sizeof XEN, 10);
    memcpy(XB, VB, sizeof XB);
    for (size_t i = 100; i < 2000; i += 3) XB[i] ^= 0x5A;                    /* the pack's UPS change */
    ups_n = fx_ups(VB, sizeof VB, XB, sizeof XB, UPS);
    sha(VA, sizeof VA, s_va); sha(VB, sizeof VB, s_vb); sha(VI, sizeof VI, s_vi); sha(VHUD, sizeof VHUD, s_vhud);
    sha(VROAD, sizeof VROAD, s_vroad); sha(XB, sizeof XB, s_xb); sha(P1, sizeof P1, s_p1); sha(XASI, sizeof XASI, s_xasi);
    sha(H1, sizeof H1, s_h1); sha(R1, sizeof R1, s_r1); sha(XEN, sizeof XEN, s_xen); sha(UPS, ups_n, s_ups);
}

static void put_bytes(const char *dir, const char *rel, const void *d, size_t n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    jk_fs_mkdir_parent(p);
    jk_fs_write_atomic(p, d, n);
}

static void world_new(W *w)
{
    tmp_dir(w->root, sizeof w->root);
    snprintf(w->game, sizeof w->game, "%s/game", w->root);
    snprintf(w->data, sizeof w->data, "%s/System", w->root);
    snprintf(w->pkg, sizeof w->pkg, "%s/pkg", w->root);
    snprintf(w->drop, sizeof w->drop, "%s/Mods", w->root);
    snprintf(w->deltas, sizeof w->deltas, "%s/deltas", w->pkg);
    jk_fs_mkdir_p(w->drop);
    jk_fs_mkdir_p(w->data);
    jk_fs_mkdir_p(w->deltas);
    put_bytes(w->game, "GLOBAL/A.bin", VA, sizeof VA);
    put_bytes(w->game, "GLOBAL/B.lzc", VB, sizeof VB);
    put_bytes(w->game, "GLOBAL/InGame.bun", VI, sizeof VI);
    put_bytes(w->game, "GLOBAL/HUD.bin", VHUD, sizeof VHUD);
    put_bytes(w->game, "TRACKS/ROAD.BUN", VROAD, sizeof VROAD);
    tmp_put(w->game, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_NFS_PACE=60\r\n");
    put_bytes(w->pkg, "xenon/Xenon.asi", XEN, sizeof XEN);
    /* the package deltas */
    char p[512];
    FxEnt a[2] = { { "", VA, sizeof VA }, { "", P1, sizeof P1 } };
    snprintf(p, sizeof p, "%s/X_A.jkd", w->deltas); xa_n = fx_delta(p, a, 2, "X", XA);
    FxEnt xi[2] = { { "", VI, sizeof VI }, { "", P1, sizeof P1 } };
    snprintf(p, sizeof p, "%s/X_InGame.jkd", w->deltas); xi_n = fx_delta(p, xi, 2, "XI", XI);
    FxEnt h0[2] = { { "", VI, sizeof VI }, { "", H1, sizeof H1 } };
    snprintf(p, sizeof p, "%s/H0_InGame.jkd", w->deltas); h0i_n = fx_delta(p, h0, 2, "H0", H0I);
    FxEnt xh[2] = { { "", XI, xi_n }, { "", H1, sizeof H1 } };
    snprintf(p, sizeof p, "%s/H_InGame.jkd", w->deltas); xhi_n = fx_delta(p, xh, 2, "XH", XHI);
    FxEnt h[2] = { { "", VHUD, sizeof VHUD }, { "", H1, sizeof H1 } };
    snprintf(p, sizeof p, "%s/H_HUD.jkd", w->deltas); hhud_n = fx_delta(p, h, 2, "H", HHUD);
    FxEnt r[2] = { { "", VROAD, sizeof VROAD }, { "", R1, sizeof R1 } };
    snprintf(p, sizeof p, "%s/R_ROAD.jkd", w->deltas); rroad_n = fx_delta(p, r, 2, "R", RROAD);
    sha(XA, xa_n, s_xa); sha(XI, xi_n, s_xi); sha(H0I, h0i_n, s_h0i); sha(XHI, xhi_n, s_xhi);
    sha(HHUD, hhud_n, s_hhud); sha(RROAD, rroad_n, s_rroad);
    w->p = (JkPaths){ 0 };
    w->p.game_dir = w->game; w->p.data_root = w->data; w->p.files_dir = w->pkg;
}

/* archives, built once in a stash folder and copied into the drop folder */
static char stash[256];
static void make_archives(void)
{
    char p[512];
    FxEnt pack[4] = { { "X360 Stuff Pack v4.2/ModData/Textures/P1.dds", P1, sizeof P1 },
                      { "X360 Stuff Pack v4.2/ModData/Files/scripts/X.asi", XASI, sizeof XASI },
                      { "X360 Stuff Pack v4.2/Install-Part-3.ups", UPS, ups_n },
                      { "X360 Stuff Pack v4.2/ReadMe.txt", (const unsigned char *)"readme", 6 } };
    snprintf(p, sizeof p, "%s/pack.zip", stash); fx_zip(p, pack, 4);
    FxEnt hud[1] = { { "assets/H1.dds", H1, sizeof H1 } };
    snprintf(p, sizeof p, "%s/hud.zip", stash); fx_zip(p, hud, 1);
    jk_fs_sha256(p, s_hudzip, &hudzip_n);
    FxEnt road[1] = { { "R1.dds", R1, sizeof R1 } };
    snprintf(p, sizeof p, "%s/road.zip", stash); fx_zip(p, road, 1);
    jk_fs_sha256(p, s_roadzip, &roadzip_n);
    size_t n = 0;
    snprintf(p, sizeof p, "%s/hud.zip", stash);
    char *d = jk_fs_read(p, 1 << 20, &n);
    d[200] ^= 1;
    snprintf(p, sizeof p, "%s/hud-bad.zip", stash); jk_fs_write_atomic(p, d, n);
    free(d);
    static unsigned char p1bad[sizeof P1];
    memcpy(p1bad, P1, sizeof P1); p1bad[10] ^= 1;
    FxEnt packbad[3] = { { "X360 Stuff Pack v4.2/ModData/Textures/P1.dds", p1bad, sizeof P1 },
                         { "X360 Stuff Pack v4.2/ModData/Files/scripts/X.asi", XASI, sizeof XASI },
                         { "X360 Stuff Pack v4.2/Install-Part-3.ups", UPS, ups_n } };
    snprintf(p, sizeof p, "%s/pack-bad.zip", stash); fx_zip(p, packbad, 3);
}

static void catalog(void)
{
    ARCS[0] = (JkMsArchive){ JK_MOD_H, s_hudzip, hudzip_n };
    ARCS[1] = (JkMsArchive){ JK_MOD_R, s_roadzip, roadzip_n };
    FILES[0] = (JkMsFile){ s_va, sizeof VA, JK_MS_GAME, "GLOBAL/A.bin" };
    FILES[1] = (JkMsFile){ s_vb, sizeof VB, JK_MS_GAME, "GLOBAL/B.lzc" };
    FILES[2] = (JkMsFile){ s_vi, sizeof VI, JK_MS_GAME, "GLOBAL/InGame.bun" };
    FILES[3] = (JkMsFile){ s_vhud, sizeof VHUD, JK_MS_GAME, "GLOBAL/HUD.bin" };
    FILES[4] = (JkMsFile){ s_vroad, sizeof VROAD, JK_MS_GAME, "TRACKS/ROAD.BUN" };
    FILES[5] = (JkMsFile){ s_p1, sizeof P1, JK_MOD_X, "ModData/Textures/P1.dds" };
    FILES[6] = (JkMsFile){ s_xasi, sizeof XASI, JK_MOD_X, "ModData/Files/scripts/X.asi" };
    FILES[7] = (JkMsFile){ s_ups, (long long)ups_n, JK_MOD_X, "Install-Part-3.ups" };
    FILES[8] = (JkMsFile){ s_h1, sizeof H1, JK_MOD_H, "assets/H1.dds" };
    FILES[9] = (JkMsFile){ s_r1, sizeof R1, JK_MOD_R, "R1.dds" };
    FILES[10] = (JkMsFile){ s_xen, sizeof XEN, JK_MS_PKG, "xenon/Xenon.asi" };
    RECIPES[0] = (JkMsRecipe){ s_xa, (long long)xa_n, JK_MS_DELTA, "X_A.jkd", NULL };
    RECIPES[1] = (JkMsRecipe){ s_xb, sizeof XB, JK_MS_UPS, s_ups, s_vb };
    RECIPES[2] = (JkMsRecipe){ s_xi, (long long)xi_n, JK_MS_DELTA, "X_InGame.jkd", NULL };
    RECIPES[3] = (JkMsRecipe){ s_h0i, (long long)h0i_n, JK_MS_DELTA, "H0_InGame.jkd", NULL };
    RECIPES[4] = (JkMsRecipe){ s_xhi, (long long)xhi_n, JK_MS_DELTA, "H_InGame.jkd", NULL };
    RECIPES[5] = (JkMsRecipe){ s_hhud, (long long)hhud_n, JK_MS_DELTA, "H_HUD.jkd", NULL };
    RECIPES[6] = (JkMsRecipe){ s_rroad, (long long)rroad_n, JK_MS_DELTA, "R_ROAD.jkd", NULL };
    RECIPES[7] = (JkMsRecipe){ s_xasi, sizeof XASI, JK_MS_COPY, s_xasi, NULL };
    RECIPES[8] = (JkMsRecipe){ s_xen, sizeof XEN, JK_MS_COPY, s_xen, NULL };
    /* at[set], bits X=1, H=2, R=4: none, X, H, X+H, R, X+R, H+R, X+H+R */
    PATHS[0] = (JkMsPath){ "GLOBAL/A.bin", 1, { "=", s_xa, "=", s_xa, "=", s_xa, "=", s_xa } };
    PATHS[1] = (JkMsPath){ "GLOBAL/B.lzc", 1, { "=", s_xb, "=", s_xb, "=", s_xb, "=", s_xb } };
    PATHS[2] = (JkMsPath){ "GLOBAL/InGame.bun", 1, { "=", s_xi, s_h0i, s_xhi, "=", s_xi, s_h0i, s_xhi } };
    PATHS[3] = (JkMsPath){ "GLOBAL/HUD.bin", 1, { "=", "=", s_hhud, s_hhud, "=", "=", s_hhud, s_hhud } };
    PATHS[4] = (JkMsPath){ "TRACKS/ROAD.BUN", 1, { "=", "=", "=", "=", s_rroad, s_rroad, s_rroad, s_rroad } };
    PATHS[5] = (JkMsPath){ "scripts/X.asi", 0, { "=", s_xasi, "=", s_xasi, "=", s_xasi, "=", s_xasi } };
    PATHS[6] = (JkMsPath){ "scripts/Xenon.asi", 0, { "=", s_xen, "=", s_xen, "=", s_xen, "=", s_xen } };
    CAT = (JkMsCatalog){ ARCS, 2, FILES, 11, RECIPES, 9, PATHS, NPATHS, BAD, 1 };
}

static void drop(W *w, const char *name, int on)
{
    char from[512], to[512];
    snprintf(from, sizeof from, "%s/%s", stash, name);
    snprintf(to, sizeof to, "%s/%s", w->drop, name);
    if (on) jk_fs_copy_checked(from, to, NULL);
    else jk_fs_remove(to);
}
static void all_archives(W *w, int on) { drop(w, "pack.zip", on); drop(w, "hud.zip", on); drop(w, "road.zip", on); }

/* PATCH as the frontend runs it: pristine capture, apply, cleanup. */
static int patch_mode(W *w, int none_only, JkMsScan *s, int *changed, char *why, size_t cap)
{
    const char *dirs[1] = { w->drop };
    int r = jk_modset_pristine_capture(&CAT, &w->p, NULL, NULL, why, cap);
    if (r < 0 && r != -2) return r;
    r = jk_modset_apply(&CAT, &w->p, dirs, 1, none_only, NULL, NULL, s, changed, why, cap);
    if (r == 0) jk_modset_cleanup(&w->p, NULL, 0);
    return r;
}
static int patch(W *w, JkMsScan *s, int *changed, char *why, size_t cap) { return patch_mode(w, 0, s, changed, why, cap); }
static int patch_to(W *w, int on, JkMsScan *s, int *changed, char *why, size_t cap)
{
    jk_modset_set_choice(&w->p, on);
    return patch(w, s, changed, why, cap);
}
static void scan(W *w, JkMsScan *s)
{
    const char *dirs[1] = { w->drop };
    jk_modset_scan(&CAT, &w->p, dirs, 1, s);
}

static int file_is(const char *dir, const char *rel, const unsigned char *d, size_t n)
{
    char p[512];
    size_t got = 0;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    char *b = jk_fs_read(p, 1 << 20, &got);
    int ok = b && got == n && !memcmp(b, d, n);
    free(b);
    return ok;
}

/* The bytes the catalog names for path k in subset s (NULL: no file). */
static const unsigned char *expect(int k, int s, size_t *n)
{
    int x = s & 1, h = s & 2, r = s & 4;
    switch (k) {
    case 0: *n = x ? xa_n : sizeof VA; return x ? XA : VA;
    case 1: *n = sizeof XB; return x ? XB : VB;
    case 2: if (x && h) { *n = xhi_n; return XHI; }
            if (x) { *n = xi_n; return XI; }
            if (h) { *n = h0i_n; return H0I; }
            *n = sizeof VI; return VI;
    case 3: *n = h ? hhud_n : sizeof VHUD; return h ? HHUD : VHUD;
    case 4: *n = r ? rroad_n : sizeof VROAD; return r ? RROAD : VROAD;
    case 5: *n = sizeof XASI; return x ? XASI : NULL;
    default: *n = sizeof XEN; return x ? XEN : NULL;
    }
}

/* The game folder is exactly column s of the catalog (and the ini kept). */
static int at_set(W *w, int s)
{
    int ok = 1;
    for (int k = 0; k < NPATHS; k++) {
        size_t n = 0;
        const unsigned char *d = expect(k, s, &n);
        int good = d ? file_is(w->game, PATHS[k].path, d, n) : !tmp_exists(w->game, PATHS[k].path);
        if (!good) { ok = 0; fprintf(stderr, "    set %d: %s is not as the catalog says\n", s, PATHS[k].path); }
    }
    char ini[128];
    ok &= tmp_get(w->game, "jk4speed-dev.ini", ini, sizeof ini) > 0 && strstr(ini, "PACE=60") != NULL;
    return ok;
}

static int dir_empty(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 1;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n == 0;
}

/* ------------------------------------------------------------------ */

static void all_8_subsets_from_vanilla(void)
{
    for (int s = 0; s < 8; s++) {
        W w; world_new(&w);
        all_archives(&w, 1);
        JkMsScan sc;
        int changed = -1;
        char why[600] = "";
        int rc = patch_to(&w, s, &sc, &changed, why, sizeof why);
        if (rc) fprintf(stderr, "    subset %d: %s\n", s, why);
        CHECK_EQ(rc, 0);
        CHECK(at_set(&w, s));
        CHECK_EQ(sc.target, s);
        CHECK_EQ(sc.installed, s);
        CHECK(!tmp_exists(w.data, "modset/journal.txt"));
        tmp_rm_rf(w.root);
    }
}

static void all_64_transitions(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0, bad = 0;
    char why[600] = "";
    for (int from = 0; from < 8; from++)
        for (int to = 0; to < 8; to++) {
            int r1 = patch_to(&w, from, &sc, &changed, why, sizeof why);
            int r2 = patch_to(&w, to, &sc, &changed, why, sizeof why);
            if (r1 || r2 || !at_set(&w, to) || sc.installed != to) {
                if (!bad++) fprintf(stderr, "    transition %d -> %d failed: %s\n", from, to, why);
            }
        }
    CHECK_EQ(bad, 0);
    tmp_rm_rf(w.root);
}

static void idempotent_second_patch_changes_nothing(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK(changed > 0);
    CHECK_EQ(patch(&w, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(changed, 0);
    CHECK(at_set(&w, 7));
    all_archives(&w, 0);                                     /* also without any download */
    CHECK_EQ(patch(&w, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(changed, 0);
    CHECK(at_set(&w, 7));
    tmp_rm_rf(w.root);
}

/* X in place, then H+R: a power cut after every rename of the swap. The next
 * start's recover rolls back to exactly the before-set. */
static void power_loss_every_step(void)
{
    int cuts = 0;
    for (int n = 1; n < 40; n++) {
        W w; world_new(&w);
        all_archives(&w, 1);
        JkMsScan sc;
        int changed = 0;
        char why[600] = "";
        CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
        jk_modset_set_choice(&w.p, 6);
        jk_modset_test_fault(n);
        int r = patch(&w, &sc, &changed, why, sizeof why);
        jk_modset_test_fault(0);
        if (r == 0) {                                        /* past the last rename: done */
            CHECK(at_set(&w, 6));
            tmp_rm_rf(w.root);
            break;
        }
        cuts++;
        CHECK_EQ(r, JK_MS_PENDING);                         /* the frontend says: start again, it rolls back */
        CHECK(tmp_exists(w.data, "modset/journal.txt"));
        CHECK_EQ(jk_modset_recover(&w.p, why, sizeof why), 1);
        CHECK(at_set(&w, 1));
        CHECK(!tmp_exists(w.data, "modset/journal.txt"));
        CHECK_EQ(jk_modset_recover(&w.p, why, sizeof why), 0);
        scan(&w, &sc);
        CHECK_EQ(sc.installed, 1);
        CHECK_EQ(patch(&w, &sc, &changed, why, sizeof why), 0);   /* and the next PATCH finishes the job */
        CHECK(at_set(&w, 6));
        tmp_rm_rf(w.root);
    }
    fprintf(stderr, "    power cuts tried: %d, every one rolled back to the before-set\n", cuts);
    CHECK(cuts >= 10);
}

static void partial_archives_one_or_two(void)
{
    W w; world_new(&w);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    drop(&w, "hud.zip", 1);
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 2));
    CHECK_EQ(sc.target, 2);
    CHECK_EQ(sc.installed, 2);
    CHECK_EQ(sc.row[JK_MOD_X], JK_MS_ROW_ON_NO_ARCHIVE);
    CHECK_EQ(sc.row[JK_MOD_H], JK_MS_ROW_ON_INSTALLED);
    CHECK_EQ(sc.row[JK_MOD_R], JK_MS_ROW_ON_NO_ARCHIVE);
    CHECK_EQ(sc.problems, 0);
    tmp_rm_rf(w.root);

    world_new(&w);
    drop(&w, "pack.zip", 1); drop(&w, "road.zip", 1);
    scan(&w, &sc);
    CHECK_EQ(sc.target, 5);
    CHECK_EQ(sc.row[JK_MOD_X], JK_MS_ROW_ON_PENDING);
    CHECK_EQ(sc.row[JK_MOD_H], JK_MS_ROW_ON_NO_ARCHIVE);
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 5));
    CHECK_EQ(sc.target, 5);
    tmp_rm_rf(w.root);
}

static void missing_archive_keeps_installed_layer(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    all_archives(&w, 0);
    char d[512];
    snprintf(d, sizeof d, "%s/modset/built", w.data);
    tmp_rm_rf(d);                                            /* no cache either */
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(changed, 0);
    CHECK(at_set(&w, 7));
    CHECK_EQ(sc.target, 7);
    for (int m = 0; m < JK_MOD_N; m++) {
        CHECK_EQ(sc.row[m], JK_MS_ROW_ON_INSTALLED);
        CHECK_EQ(sc.cached[m], 1);
    }
    tmp_rm_rf(w.root);
}

static void off_uses_cache_not_archive(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    all_archives(&w, 0);
    scan(&w, &sc);
    jk_modset_set_choice(&w.p, 3);
    scan(&w, &sc);
    CHECK_EQ(sc.row[JK_MOD_R], JK_MS_ROW_OFF_PENDING);
    CHECK_EQ(sc.problems, 0);
    int rc = patch(&w, &sc, &changed, why, sizeof why);
    if (rc) fprintf(stderr, "    7 -> 3: %s\n", why);
    CHECK_EQ(rc, 0);
    CHECK(at_set(&w, 3));                                    /* the road back to pristine */
    char rel[128];
    snprintf(rel, sizeof rel, "modset/built/%s", s_rroad);
    CHECK(tmp_exists(w.data, rel));                          /* the HD road kept in the cache */
    CHECK_EQ(sc.row[JK_MOD_R], JK_MS_ROW_OFF);
    jk_modset_set_choice(&w.p, 7);
    scan(&w, &sc);
    CHECK_EQ(sc.target, 7);
    CHECK_EQ(sc.row[JK_MOD_R], JK_MS_ROW_ON_PENDING);
    CHECK_EQ(sc.cached[JK_MOD_R], 1);
    rc = patch(&w, &sc, &changed, why, sizeof why);          /* back ON without its download: the cache */
    if (rc) fprintf(stderr, "    3 -> 7: %s\n", why);
    CHECK_EQ(rc, 0);
    CHECK(at_set(&w, 7));
    tmp_rm_rf(w.root);
}

static void off_needs_archive_refused_atomically(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 3, &sc, &changed, why, sizeof why), 0);
    all_archives(&w, 0);
    char d[512];
    snprintf(d, sizeof d, "%s/modset/built", w.data);
    tmp_rm_rf(d);
    why[0] = 0;
    CHECK(patch_to(&w, 2, &sc, &changed, why, sizeof why) < 0);   /* X OFF: the H-only InGame needs the HUD download */
    CHECK(strstr(why, "Upscaled HUD") != NULL);
    CHECK_EQ(changed, 0);
    CHECK(at_set(&w, 3));
    CHECK(sc.problems >= 1);
    CHECK(strstr(sc.problem[0], "Upscaled HUD") != NULL);
    CHECK(!tmp_exists(w.data, "modset/journal.txt"));
    fprintf(stderr, "    refused: %s\n", why);
    tmp_rm_rf(w.root);
}

static void pristine_from_sidecar(void)
{
    for (int edit = 0; edit < 2; edit++) {
        W w; world_new(&w);
        put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);                         /* staged by hand: the X file */
        put_bytes(w.game, "GLOBAL/A.bin.release-lock-1.original", VA, sizeof VA);
        char why[600] = "";
        CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
        CHECK(file_is(w.data, "backup/pristine/GLOBAL/A.bin", VA, sizeof VA));
        CHECK(file_is(w.data, "backup/pristine/TRACKS/ROAD.BUN", VROAD, sizeof VROAD));
        CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));                    /* the game folder untouched */
        char list[1024] = "";
        CHECK(tmp_get(w.data, "state/cleanup.txt", list, sizeof list) > 0);
        CHECK(strstr(list, "GLOBAL/A.bin.release-lock-1.original") != NULL);
        CHECK(strstr(list, s_va) != NULL);
        char pl[2048] = "";
        CHECK(tmp_get(w.data, "backup/pristine/pristine.txt", pl, sizeof pl) > 0);
        CHECK(strstr(pl, s_va) != NULL && strstr(pl, "GLOBAL/A.bin") != NULL);
        if (edit) tmp_put(w.game, "GLOBAL/A.bin.release-lock-1.original", "the player edited this");
        jk_modset_cleanup(&w.p, why, sizeof why);
        CHECK_EQ(tmp_exists(w.game, "GLOBAL/A.bin.release-lock-1.original"), edit);
        CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));
        /* captured once: a second capture finds everything in place */
        CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
        tmp_rm_rf(w.root);
    }
    /* r3's own originals, moved aside by the r4 layout, count as well */
    W w; world_new(&w);
    put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);
    put_bytes(w.data, "legacy-r3/modset/original/GLOBAL/A.bin", VA, sizeof VA);
    char why[600] = "";
    CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
    CHECK(file_is(w.data, "backup/pristine/GLOBAL/A.bin", VA, sizeof VA));
    CHECK(file_is(w.data, "legacy-r3/modset/original/GLOBAL/A.bin", VA, sizeof VA));
    tmp_rm_rf(w.root);
}

static void no_clean_original_one_problem(void)
{
    W w; world_new(&w);
    put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);                             /* the X file, no untouched copy anywhere */
    char why[600] = "";
    CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), -2);
    CHECK(strstr(why, "GLOBAL/A.bin") != NULL);
    CHECK(strstr(why, "copy") == NULL);                                       /* never asks the player to copy files */
    JkMsScan sc;
    scan(&w, &sc);
    CHECK_EQ(sc.nmissing_pristine, 1);
    CHECK(!strcmp(sc.missing_pristine[0], "GLOBAL/A.bin"));
    CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));
    CHECK(file_is(w.game, "GLOBAL/HUD.bin", VHUD, sizeof VHUD));
    /* a PATCH that needs it is refused, with the file named, and changes nothing */
    all_archives(&w, 1);
    int changed = 0;
    why[0] = 0;
    CHECK(patch_to(&w, 2, &sc, &changed, why, sizeof why) < 0);
    CHECK(strstr(why, "GLOBAL/A.bin") != NULL);
    CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));
    CHECK(file_is(w.game, "GLOBAL/InGame.bun", VI, sizeof VI));
    tmp_rm_rf(w.root);
}

static long long tiny_disk(void *ctx, const char *path) { (void)ctx; (void)path; return 1000; }
static long long unknown_disk(void *ctx, const char *path) { (void)ctx; (void)path; return -1; }

static void pristine_capture_refused_without_space(void)
{
    W w; world_new(&w);
    char why[600] = "";
    jk_fs_free_fn = tiny_disk;
    CHECK(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why) < 0);
    jk_fs_free_fn = NULL;
    CHECK(strstr(why, "MB needed") != NULL);
    CHECK(strstr(why, "MB free") != NULL);
    CHECK(!tmp_exists(w.data, "backup/pristine"));                            /* nothing written */
    CHECK(!tmp_exists(w.data, "state/cleanup.txt"));
    jk_fs_free_fn = unknown_disk;                                             /* unknown: goes ahead, says so */
    why[0] = 0;
    CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
    jk_fs_free_fn = NULL;
    CHECK(strstr(why, "unknown") != NULL);
    CHECK(file_is(w.data, "backup/pristine/GLOBAL/A.bin", VA, sizeof VA));
    tmp_rm_rf(w.root);
}

static void bench_files_untouched_by_patch(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    tmp_put(w.game, "d3d9.dll", "bench d3d9");
    tmp_put(w.game, "d3d9.dll.ff93d597", "bench d3d9 copy");
    tmp_put(w.game, "Xidi.ini", "[Xidi]");
    tmp_put(w.game, "scripts/bench.asi", "bench asi");
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(patch_mode(&w, 1, &sc, &changed, why, sizeof why), 0);
    char b[64];
    CHECK(tmp_get(w.game, "d3d9.dll", b, sizeof b) > 0 && !strcmp(b, "bench d3d9"));
    CHECK(tmp_get(w.game, "d3d9.dll.ff93d597", b, sizeof b) > 0 && !strcmp(b, "bench d3d9 copy"));
    CHECK(tmp_get(w.game, "Xidi.ini", b, sizeof b) > 0 && !strcmp(b, "[Xidi]"));
    CHECK(tmp_get(w.game, "scripts/bench.asi", b, sizeof b) > 0 && !strcmp(b, "bench asi"));
    tmp_rm_rf(w.root);
}

static void restore_returns_all_pristine_and_clears_cache(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(patch_to(&w, 3, &sc, &changed, why, sizeof why), 0);   /* the road into the cache */
    char built[512];
    snprintf(built, sizeof built, "%s/modset/built", w.data);
    CHECK(!dir_empty(built));
    all_archives(&w, 0);
    CHECK_EQ(patch_mode(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 0));
    CHECK(dir_empty(built));
    CHECK_EQ(sc.installed, 0);
    tmp_rm_rf(w.root);
}

/* The launcher never deletes a file it did not make: a foreign file that must
 * leave a managed path goes to backup/kept/<path>, then <path>.2, .3 ... */
static void foreign_file_kept_never_deleted(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", b[64];
    tmp_put(w.game, "scripts/X.asi", "foreign 1");
    CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 1));
    CHECK(tmp_get(w.data, "backup/kept/scripts/X.asi", b, sizeof b) > 0 && !strcmp(b, "foreign 1"));
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 0));
    CHECK(!tmp_exists(w.game, "scripts/X.asi"));                     /* never put back automatically */
    tmp_put(w.game, "scripts/X.asi", "foreign 2");
    CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK(tmp_get(w.data, "backup/kept/scripts/X.asi", b, sizeof b) > 0 && !strcmp(b, "foreign 1"));
    CHECK(tmp_get(w.data, "backup/kept/scripts/X.asi.2", b, sizeof b) > 0 && !strcmp(b, "foreign 2"));
    /* a vanilla path: two custom files in turn, both kept; "=" gives the untouched file */
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    tmp_put(w.game, "GLOBAL/A.bin", "custom A 1");
    CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(file_is(w.game, "GLOBAL/A.bin", VA, sizeof VA));
    tmp_put(w.game, "GLOBAL/A.bin", "custom A 2");
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(file_is(w.game, "GLOBAL/A.bin", VA, sizeof VA));
    CHECK(tmp_get(w.data, "backup/kept/GLOBAL/A.bin", b, sizeof b) > 0 && !strcmp(b, "custom A 1"));
    CHECK(tmp_get(w.data, "backup/kept/GLOBAL/A.bin.2", b, sizeof b) > 0 && !strcmp(b, "custom A 2"));
    tmp_rm_rf(w.root);
}

/* A non-vanilla path at "=" ends absent, whatever was there. */
static void non_vanilla_eq_path_ends_absent(void)
{
    W w; world_new(&w);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", b[64];
    tmp_put(w.game, "scripts/X.asi", "a hand-staged X360Stuff.asi");
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(!tmp_exists(w.game, "scripts/X.asi"));
    CHECK(tmp_get(w.data, "backup/kept/scripts/X.asi", b, sizeof b) > 0 && !strcmp(b, "a hand-staged X360Stuff.asi"));
    CHECK(at_set(&w, 0));
    CHECK_EQ(sc.installed, 0);
    tmp_rm_rf(w.root);
}

/* r3 behaviour kept: a damaged download is named and skipped, other archives counted. */
static void damaged_archives_named_and_skipped(void)
{
    W w; world_new(&w);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    drop(&w, "pack.zip", 1);
    drop(&w, "hud-bad.zip", 1);
    tmp_put(w.drop, "notes.zip", "PK\3\4 not really a zip");
    tmp_put(w.drop, "readme.txt", "not an archive: ignored");
    CHECK_EQ(patch_to(&w, 7, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 1));
    CHECK_EQ(sc.mod[JK_MOD_H].state, JK_MS_BAD);
    CHECK_EQ(sc.row[JK_MOD_H], JK_MS_ROW_BAD);
    CHECK(strstr(sc.mod[JK_MOD_H].note, "hud-bad.zip") != NULL);
    CHECK_EQ(sc.unknown, 1);
    CHECK(strstr(sc.unknown_name, "notes.zip") != NULL);
    tmp_rm_rf(w.root);
}

static void bad_pack_entry_changes_nothing(void)
{
    W w; world_new(&w);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    drop(&w, "pack-bad.zip", 1);                             /* one entry damaged: found by its hash at the build */
    CHECK(patch_to(&w, 1, &sc, &changed, why, sizeof why) < 0);
    CHECK(strstr(why, "missing or damaged") != NULL);
    CHECK(at_set(&w, 0));
    CHECK_EQ(changed, 0);
    tmp_rm_rf(w.root);
}

static void full_disk_changes_nothing(void)
{
    W w; world_new(&w);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "";
    CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
    all_archives(&w, 1);
    jk_fs_free_fn = tiny_disk;
    const char *dirs[1] = { w.drop };
    CHECK(jk_modset_apply(&CAT, &w.p, dirs, 1, 0, NULL, NULL, &sc, &changed, why, sizeof why) < 0);
    jk_fs_free_fn = NULL;
    CHECK(strstr(why, "not enough free space") != NULL);
    CHECK(at_set(&w, 0));
    tmp_rm_rf(w.root);
}

static void choice_saved_and_checked(void)
{
    W w; world_new(&w);
    CHECK_EQ(jk_modset_choice(&w.p), JK_MS_DEFAULT_ON);
    CHECK_EQ(jk_modset_set_choice(&w.p, 5), 0);
    CHECK_EQ(jk_modset_choice(&w.p), 5);
    CHECK(jk_modset_set_choice(&w.p, 8) < 0);
    CHECK(jk_modset_set_choice(&w.p, -1) < 0);
    CHECK_EQ(jk_modset_choice(&w.p), 5);
    tmp_put(w.data, "modset/choice.txt", "garbage");
    CHECK_EQ(jk_modset_choice(&w.p), JK_MS_DEFAULT_ON);
    tmp_rm_rf(w.root);
}

/* ---- final review fixes ---- */

/* I-1: the journal names its game folder; recover acts only on a match and
 * leaves everything as it is otherwise. */
static void journal_of_other_game_dir_left_alone(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", other[400];
    CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
    jk_modset_set_choice(&w.p, 6);
    jk_modset_test_fault(3);
    CHECK_EQ(patch(&w, &sc, &changed, why, sizeof why), JK_MS_PENDING);
    jk_modset_test_fault(0);
    snprintf(other, sizeof other, "%s/other", w.root);
    put_bytes(other, "GLOBAL/A.bin", VA, sizeof VA);
    JkPaths q = w.p;
    q.game_dir = other;
    CHECK_EQ(jk_modset_recover(&q, why, sizeof why), 3);              /* left alone */
    CHECK(file_is(other, "GLOBAL/A.bin", VA, sizeof VA));
    CHECK(tmp_exists(w.data, "modset/journal.txt"));
    CHECK_EQ(jk_modset_pending(&q), 0);                               /* not this folder's */
    CHECK_EQ(jk_modset_pending(&w.p), 1);
    /* a PATCH on the other folder is refused while the journal's folder exists */
    const char *dirs[1] = { w.drop };
    CHECK(jk_modset_apply(&CAT, &q, dirs, 1, 0, NULL, NULL, &sc, &changed, why, sizeof why) < 0);
    CHECK(tmp_exists(w.data, "modset/journal.txt"));
    CHECK_EQ(jk_modset_recover(&w.p, why, sizeof why), 1);            /* its own folder: rolled back */
    CHECK(at_set(&w, 1));
    tmp_rm_rf(w.root);
}

/* Ledger (b): a rollback that fails inside a PATCH is JK_MS_PENDING, not -1. */
static void apply_rollback_failure_is_pending(void)
{
    W w; world_new(&w);
    char j[1200], why[600] = "";
    snprintf(j, sizeof j, "JK4MS 3\ngame %s\nop patch\nstate swapping\n1\tGLOBAL/A.bin\t%s/GLOBAL/B.lzc/x\tK\t-\n", w.game, w.game);   /* under a file: cannot be undone */
    tmp_put(w.data, "modset/journal.txt", j);
    JkMsScan sc;
    int changed = 0;
    CHECK_EQ(patch(&w, &sc, &changed, why, sizeof why), JK_MS_PENDING);
    CHECK(tmp_exists(w.data, "modset/journal.txt"));
    CHECK(file_is(w.game, "GLOBAL/A.bin", VA, sizeof VA));
    tmp_rm_rf(w.root);
}

/* Ledger (a): a damaged journal on a Patch: what is provably ours goes back
 * (a catalog output in the game, the file before it in pre/), the rest is
 * kept aside, the journal is moved aside and the Patch goes on. */
static void damaged_journal_rolls_back_proven(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", b[64];
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);   /* the unchanged copies taken */
    /* cut off half way to X: A.bin swapped (our output in, the original in pre/) */
    put_bytes(w.data, "modset/pre/GLOBAL/A.bin", VA, sizeof VA);
    put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);
    /* and a file that is nobody we know of in place of HUD.bin */
    put_bytes(w.data, "modset/pre/GLOBAL/HUD.bin", VHUD, sizeof VHUD);
    tmp_put(w.game, "GLOBAL/HUD.bin", "foreign hud");
    tmp_put(w.data, "modset/journal.txt", "JK4MS 3\ngame");
    CHECK_EQ(jk_modset_pending(&w.p), 3);
    CHECK(jk_modset_recover(&w.p, why, sizeof why) < 0);             /* at the open: reported, nothing done */
    CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(at_set(&w, 0));
    CHECK(!tmp_exists(w.data, "modset/journal.txt"));
    CHECK(tmp_exists(w.data, "modset/journal.damaged.txt"));
    int kept = (tmp_get(w.data, "backup/kept/GLOBAL/HUD.bin", b, sizeof b) > 0 && !strcmp(b, "foreign hud"))
            || (tmp_get(w.data, "backup/kept/GLOBAL/HUD.bin.2", b, sizeof b) > 0 && !strcmp(b, "foreign hud"));
    CHECK(kept);
    tmp_rm_rf(w.root);
}

/* I-2: Restore on a folder whose managed file has no unchanged copy: it goes
 * on, leaves that file as it is and names it. */
static void restore_without_pristine_leaves_and_names(void)
{
    W w; world_new(&w);
    put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);                  /* came with the folder, no untouched copy */
    tmp_put(w.game, "scripts/X.asi", "the player's asi");
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", b[64];
    CHECK_EQ(patch_mode(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK(file_is(w.game, "GLOBAL/A.bin", XA, xa_n));
    CHECK_EQ(sc.nleft, 1);
    CHECK(!strcmp(sc.left[0], "GLOBAL/A.bin"));
    /* the player's asi went aside and came back (Restore returns the first kept file) */
    CHECK(tmp_get(w.game, "scripts/X.asi", b, sizeof b) > 0 && !strcmp(b, "the player's asi"));
    CHECK(file_is(w.game, "GLOBAL/HUD.bin", VHUD, sizeof VHUD));
    tmp_rm_rf(w.root);
}

/* I-3 (b): Restore returns the first file kept aside at a path (never .2+)
 * when that path ends absent or at the untouched file. */
static void restore_returns_first_kept(void)
{
    W w; world_new(&w);
    all_archives(&w, 1);
    JkMsScan sc;
    int changed = 0;
    char why[600] = "", b[64];
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);   /* the unchanged copies taken */
    tmp_put(w.game, "scripts/X.asi", "foreign 1");
    tmp_put(w.game, "GLOBAL/HUD.bin", "custom hud 1");
    CHECK_EQ(patch_to(&w, 1, &sc, &changed, why, sizeof why), 0);
    tmp_put(w.game, "scripts/X.asi", "foreign 2");
    CHECK_EQ(patch_to(&w, 0, &sc, &changed, why, sizeof why), 0);
    CHECK(tmp_exists(w.data, "backup/kept/scripts/X.asi.2"));
    CHECK_EQ(patch_mode(&w, 1, &sc, &changed, why, sizeof why), 0);
    CHECK(tmp_get(w.game, "scripts/X.asi", b, sizeof b) > 0 && !strcmp(b, "foreign 1"));
    CHECK(tmp_get(w.game, "GLOBAL/HUD.bin", b, sizeof b) > 0 && !strcmp(b, "custom hud 1"));
    CHECK(!tmp_exists(w.data, "backup/kept/scripts/X.asi"));
    CHECK(tmp_get(w.data, "backup/kept/scripts/X.asi.2", b, sizeof b) > 0 && !strcmp(b, "foreign 2"));
    CHECK(file_is(w.game, "GLOBAL/A.bin", VA, sizeof VA));
    CHECK(!tmp_exists(w.game, "scripts/Xenon.asi"));
    tmp_rm_rf(w.root);
}

/* I-1: cleanup.txt names its game folder: another folder's sidecars stay. */
static void cleanup_only_in_its_game_dir(void)
{
    W w; world_new(&w);
    put_bytes(w.game, "GLOBAL/A.bin", XA, xa_n);
    put_bytes(w.game, "GLOBAL/A.bin.bak", VA, sizeof VA);
    char why[600] = "", other[400];
    CHECK_EQ(jk_modset_pristine_capture(&CAT, &w.p, NULL, NULL, why, sizeof why), 0);
    snprintf(other, sizeof other, "%s/other", w.root);
    put_bytes(other, "GLOBAL/A.bin.bak", VA, sizeof VA);
    JkPaths q = w.p;
    q.game_dir = other;
    CHECK_EQ(jk_modset_cleanup(&q, why, sizeof why), 0);
    CHECK(file_is(other, "GLOBAL/A.bin.bak", VA, sizeof VA));
    CHECK_EQ(jk_modset_cleanup(&w.p, why, sizeof why), 1);
    CHECK(!tmp_exists(w.game, "GLOBAL/A.bin.bak"));
    tmp_rm_rf(w.root);
}

/* ---- the release catalog on the real game and downloads (JK_REAL_MODS=1) ---- */

static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return v && *v ? v : def;
}

static void real_archives_8_subsets(void)
{
    if (!getenv("JK_REAL_MODS")) { fprintf(stderr, "    SKIP real_archives_8_subsets (set JK_REAL_MODS=1)\n"); return; }
    static char home[256], game[400], data[400], files[400], dropd[400];
    snprintf(home, sizeof home, "%s/jk4-build/r4-real", env_or("HOME", "/tmp"));
    snprintf(game, sizeof game, "%s", env_or("JK_REAL_GAME", ""));
    if (!game[0]) snprintf(game, sizeof game, "%s/game", home);
    snprintf(data, sizeof data, "%s/System", home);
    snprintf(files, sizeof files, "%s", env_or("JK_REAL_FILES", ""));
    if (!files[0]) snprintf(files, sizeof files, "%s/files", home);
    snprintf(dropd, sizeof dropd, "%s", env_or("JK_REAL_DROP", ""));
    if (!dropd[0]) snprintf(dropd, sizeof dropd, "%s/Mods", home);
    jk_fs_mkdir_p(data);
    JkPaths p = { 0 };
    p.game_dir = game; p.data_root = data; p.files_dir = files;
    const JkMsCatalog *c = jk_modset_catalog();
    const char *dirs[1] = { dropd };
    char why[1200] = "";
    int r = jk_modset_pristine_capture(c, &p, NULL, NULL, why, sizeof why);
    if (r) fprintf(stderr, "    capture: %d %s\n", r, why);
    CHECK_EQ(r, 0);
    for (int s = 0; s < 8; s++) {
        JkMsScan sc;
        int changed = 0;
        jk_modset_set_choice(&p, s);
        why[0] = 0;
        r = jk_modset_apply(c, &p, dirs, 1, 0, NULL, NULL, &sc, &changed, why, sizeof why);
        if (r) fprintf(stderr, "    subset %d: %s\n", s, why);
        CHECK_EQ(r, 0);
        CHECK_EQ(sc.installed, s);
        char line[4096];
        int n = snprintf(line, sizeof line, "    REAL subset %d (%s%s%s) changed %d:", s, s & 1 ? "X" : "", s & 2 ? "H" : "",
                         s & 4 ? "R" : "", changed);
        int bad = 0;
        for (int i = 0; i < c->npaths; i++) {
            const JkMsPath *mp = &c->paths[i];
            char gp[1024], got[65] = "-";
            snprintf(gp, sizeof gp, "%s/%s", game, mp->path);
            if (jk_fs_exists(gp) && jk_fs_sha256(gp, got, NULL) < 0) snprintf(got, sizeof got, "?");
            const char *want = mp->at[s];
            if (!strcmp(want, "=")) {
                want = "-";
                for (int k = 0; k < c->nfiles; k++)
                    if (c->files[k].kind == JK_MS_GAME && !strcmp(c->files[k].name, mp->path)) want = c->files[k].sha;
            }
            if (strcmp(got, want)) { bad++; fprintf(stderr, "    subset %d: %s is %.8s, want %.8s\n", s, mp->path, got, want); }
            if (n < (int)sizeof line - 40) n += snprintf(line + n, sizeof line - n, " %s=%.8s", mp->path, got);
        }
        CHECK_EQ(bad, 0);
        fprintf(stderr, "%s %s\n", line, bad ? "FAIL" : "ok");
    }
    JkMsScan sc;
    int changed = 0;
    r = jk_modset_apply(c, &p, dirs, 1, 1, NULL, NULL, &sc, &changed, why, sizeof why);   /* Restore: untouched again */
    CHECK_EQ(r, 0);
    CHECK_EQ(sc.installed, 0);
    fprintf(stderr, "    REAL restore: %d files back, installed %d\n", changed, sc.installed);
}

int main(void)
{
    world_contents();
    tmp_dir(stash, sizeof stash);
    W w0; world_new(&w0); tmp_rm_rf(w0.root);          /* the delta targets and their sizes */
    make_archives();
    catalog();
    RUN(journal_of_other_game_dir_left_alone);
    RUN(apply_rollback_failure_is_pending);
    RUN(damaged_journal_rolls_back_proven);
    RUN(restore_without_pristine_leaves_and_names);
    RUN(restore_returns_first_kept);
    RUN(cleanup_only_in_its_game_dir);
    RUN(all_8_subsets_from_vanilla);
    RUN(all_64_transitions);
    RUN(idempotent_second_patch_changes_nothing);
    RUN(power_loss_every_step);
    RUN(partial_archives_one_or_two);
    RUN(missing_archive_keeps_installed_layer);
    RUN(off_uses_cache_not_archive);
    RUN(off_needs_archive_refused_atomically);
    RUN(pristine_from_sidecar);
    RUN(no_clean_original_one_problem);
    RUN(pristine_capture_refused_without_space);
    RUN(bench_files_untouched_by_patch);
    RUN(restore_returns_all_pristine_and_clears_cache);
    RUN(foreign_file_kept_never_deleted);
    RUN(non_vanilla_eq_path_ends_absent);
    RUN(damaged_archives_named_and_skipped);
    RUN(bad_pack_entry_changes_nothing);
    RUN(full_disk_changes_nothing);
    RUN(choice_saved_and_checked);
    RUN(real_archives_8_subsets);
    tmp_rm_rf(stash);
    return jk_test_report("test_modset");
}
