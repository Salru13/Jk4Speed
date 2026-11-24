/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Mods from the USB drop folder. See jk_mods.h. */
#include "jk_mods.h"
#include "jk_delta.h"
#include "jk_fs.h"
#include "jk_manifest.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* engine-1.0 manifest 2.1 (our builds, console readback C) and 2.2 (pack
 * files; the shaders hashed from the uploaded copy, L). HUDS_Custom_00.bin is the
 * JDLZ-fixed a67a45eb (docs/status/2026-11-23-jdlz-crash.md: 08b9d1ef crashed
 * the race load with the Golf GTI). BUILD files name their delta in the
 * package (jk4speed-files/deltas/). */
static const JkModFile TABLE[] = {
    { "TRACKS/STREAML2RA.BUN", "e2a7a504a2563b72ef0674108cbc14dfb24b1896c2e880ca937d4e95e161c226", 532663556LL, JK_MOD_BUILD, "TRACKS_STREAML2RA.BUN.jkd" },
    { "GLOBAL/HUDTEXRACE.BIN", "6e57f398c77618651ba849429679807d5dec08a4632c9e3582feeb7f283118da", 2696832LL, JK_MOD_BUILD, "GLOBAL_HUDTEXRACE.BIN.jkd" },
    { "GLOBAL/InGameA.bun", "9825274a478bafa22c0b61f2059552ca45c6c14b8052a1884e5bc26d20069b59", 11755264LL, JK_MOD_BUILD, "GLOBAL_InGameA.bun.jkd" },
    { "GLOBAL/HUDS_Custom_00.bin", "a67a45eb357f0a1479b84ad568891f6368aeb69e5259861c1472eb1d2c2b2ffd", 133632LL, JK_MOD_BUILD, "GLOBAL_HUDS_Custom_00.bin.jkd" },
    { "GLOBAL/attributes.bin", "c52182d2338449c670a4fcb5972461f570b86466d387dcc2483adfc99200bb71", 689728LL, JK_MOD_BUILD, "GLOBAL_attributes.bin.jkd" },
    { "GLOBAL/GlobalB.lzc", "a2eda728dc7cf733862464b51ba2f82e536886195d61f06ea6632cc1c8fcdb84", 10360768LL, JK_MOD_BUILD, "GLOBAL_GlobalB.lzc.jkd" },
    { "scripts/X360Stuff.ini", "8436599baf4dbd02f7382801b778d4fa49f6137343da470bb92c0c1e676f166f", 7265LL, JK_MOD_BUILD, "scripts_X360Stuff.ini.jkd" },
    { "scripts/X360Stuff.asi", "70171ad75d5ccdf6032bed161fbcdad6a0799b2f7c717af6d977e3571dd185bf", 402944LL, JK_MOD_PACK, NULL },
    { "scripts/NFSMW_XenonEffects.asi", "5dcad612125be0f13426644b044a2a18874fc2d84726c033e97975e262256692", 72192LL, JK_MOD_PACK, NULL },
    { "scripts/NFSMW_XenonEffects.ini", "140cb48f22406fb3acce35756d959824bb1b9561b8d0a71aebd2a0d6c646b1e0", 2124LL, JK_MOD_PACK, NULL },
    { "scripts/XenonEffects.tpk", "b7d3fe2ebdc520a650b0972eb71cf7ba0de27a6789a32501f5f8aee311008328", 353152LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/car.fx.o", "46d45c0bdbc921ff1ba92d7b0362fe243495e3e00fbff5f9c2a8be2aa4e50d83", 36116LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/carnormalmap.fx.o", "67946a29cdef62e3ee1f129042b91c336502f4f1a32b8df7819c00976a51b1b6", 33540LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/debugPoly.fx.o", "266e11361d566e7da0180f44ee79fe33e8dd7da8359f6e95c88b83e7b6e8ad64", 6308LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/glassreflectshader.fx.o", "cde37809ead3eebdcc065429dad4f608d0cd12fe8154d49507dd70edb62f1b1c", 22552LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/grass.fx.o", "3d328d3a8827e9cc4faffcac38847f2c5058ac2cc3838c697783bb73e15a8fa8", 99640LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/hdr.fx.o", "3bd86ef4c60a0fec44d48c1cbdc8c8be396736b83b75362e13dd0aadeb44373f", 23084LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/particles.fx.o", "a65890cf2b26f82c2841f69b247b9fa0cd7752503add20483f95bc59d1ca7bd4", 19500LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/shadowscreenfilter.fx.o", "f4400ced679f56a07101b8aefbd5177873696d4721509d1ae9a095a5084cc0f3", 688LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/shadowvolume.fx.o", "2468d6746b777cde99b267b5fbbede8518f487bcdfc24a41e53efe7502f8e057", 6648LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/sky.fx.o", "ce9fbc32e9403cbff6fa52d81a761877fbb3e7610845d6530b5e6fbc91789a0f", 15728LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/standard.fx.o", "a287f0ab377359487dc244d0d01c7de0114a693ce664fb842d512927475bdcd8", 10368LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/trees.fx.o", "80acf84aebba827e6f24283e61d439c4f506df77e65a966b13f10e1eee639b56", 16880LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/visualtreatment.fx.o", "dcbf8fa0d6f8ad5e3f2df602fc5ba812346bec1d4b4c66ec4d5d0eddfb182bb2", 12600LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/waterseashader.fx.o", "1de277d831aebcccac6df8d68460f8a83246898a5ef97053feffedf50ed59391", 14856LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/world.fx.o", "e59cf7cb15679bfaed0a640938fa26c4d9c3abc81c537611b9499b78fe4c13c2", 25076LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/worldbone.fx.o", "95749ef27c423ca134566d7dbe2e8b401b666b13c5298f814fa4a392dfc6258d", 32012LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/worldnormalmap.fx.o", "9adb16d7158889c59226e76a30e11942ec01d37ed852010048995444b0b5e72b", 37340LL, JK_MOD_PACK, NULL },
    { "FX/COMPILED/worldreflect.fx.o", "a8f595409569e7a13a9c91e3be92ce4532f644a9a4696c80799c73eb7ed32ca0", 53624LL, JK_MOD_PACK, NULL },
};
#define TABLE_N ((int)(sizeof TABLE / sizeof TABLE[0]))

const JkModFile *jk_mods_table(int *count)
{
    if (count) *count = TABLE_N;
    return TABLE;
}

const JkModFile *jk_mods_find(const char *path)
{
    for (int i = 0; path && i < TABLE_N; i++)
        if (!strcmp(TABLE[i].path, path)) return &TABLE[i];
    return NULL;
}

/* 1 when dir/rel exists with the given size and sha256. *present gets 1 when it exists. */
static int at_hash(const char *dir, const JkModFile *f, int *present)
{
    char p[1024], sha[65];
    long long size = -1;
    if (present) *present = 0;
    if (!dir || jk_fs_join(p, sizeof p, dir, f->path) < 0 || jk_fs_size(p) < 0) return 0;
    if (present) *present = 1;
    if (jk_fs_size(p) != f->size) return 0;                 /* cheap first: no 532 MB hash of a wrong file */
    return jk_fs_sha256(p, sha, &size) == 0 && !strcmp(sha, f->sha);
}

int jk_mods_scan_table(const JkModFile *t, int n, const char *mods_dir, const char *game_dir, JkModsScan *out)
{
    if (!out || !t) return -1;
    memset(out, 0, sizeof *out);
    out->total = n;
    for (int i = 0; i < n; i++) {
        int present = 0;
        int ok = at_hash(mods_dir, &t[i], &present);
        out->found += present;
        out->ok += ok;
        if (present && !ok && !out->bad++) snprintf(out->first_bad, sizeof out->first_bad, "%s", t[i].path);
        out->installed += at_hash(game_dir, &t[i], NULL);
    }
    return 0;
}

int jk_mods_scan(const char *mods_dir, const char *game_dir, JkModsScan *out)
{
    return jk_mods_scan_table(TABLE, TABLE_N, mods_dir, game_dir, out);
}

/* ---- BUILD files from deltas ---- */

/* The mod authors' releases, extracted anywhere under <mods>/author/: a list
 * of (path, size) made once per scan or install; a file is used only when its
 * size and SHA-256 are the ones a delta names. */
#define AUTHOR_MAX 8192
#define AUTHOR_DEPTH 8
typedef struct AuthorFile { char path[512]; long long size; } AuthorFile;
static AuthorFile authors[AUTHOR_MAX];
static int author_n;

static void author_walk(const char *dir, int depth)
{
    DIR *d;
    if (depth > AUTHOR_DEPTH || !(d = opendir(dir))) return;
    struct dirent *e;
    while ((e = readdir(d)) && author_n < AUTHOR_MAX) {
        char p[1024];
        struct stat st;
        if (e->d_name[0] == '.' || jk_fs_join(p, sizeof p, dir, e->d_name) < 0 || lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) author_walk(p, depth + 1);
        else if (S_ISREG(st.st_mode) && strlen(p) < sizeof authors[0].path) {
            memcpy(authors[author_n].path, p, strlen(p) + 1);
            authors[author_n++].size = (long long)st.st_size;
        }
    }
    closedir(d);
}

static void author_index(const char *mods_dir)
{
    char dir[1024];
    author_n = 0;
    if (mods_dir && jk_fs_join(dir, sizeof dir, mods_dir, "author") == 0) author_walk(dir, 0);
}

/* Finds the player's file with this hash and size for the delta source of f:
 * the game folder, the drop folder, its original/ folder, our backup. */
static int find_source(const JkPaths *p, const char *mods_dir, const JkModFile *f, const char *sha, long long size,
                       char *out, size_t cap)
{
    char cand[4][1024];
    int nc = 0;
    if (jk_fs_join(cand[nc], sizeof cand[0], p->game_dir, f->path) == 0) nc++;
    if (mods_dir && jk_fs_join(cand[nc], sizeof cand[0], mods_dir, f->path) == 0) nc++;
    char orig[1024];
    if (mods_dir && jk_fs_join(orig, sizeof orig, mods_dir, "original") == 0
        && jk_fs_join(cand[nc], sizeof cand[0], orig, f->path) == 0) nc++;
    char bk[1024];
    if (p->data_root && jk_fs_join(bk, sizeof bk, p->data_root, "backup") == 0
        && jk_fs_join(cand[nc], sizeof cand[0], bk, f->path) == 0) nc++;
    for (int i = 0; i < nc; i++) {
        char got[65];
        if (jk_fs_size(cand[i]) != size || jk_fs_sha256(cand[i], got, NULL) < 0 || strcmp(got, sha)) continue;
        snprintf(out, cap, "%s", cand[i]);
        return 0;
    }
    for (int i = 0; i < author_n; i++) {
        char got[65];
        if (authors[i].size != size || jk_fs_sha256(authors[i].path, got, NULL) < 0 || strcmp(got, sha)) continue;
        snprintf(out, cap, "%s", authors[i].path);
        return 0;
    }
    return -1;
}

/* All sources of f's delta found: 0, paths in src[]. */
static int sources_of(const JkPaths *p, const char *mods_dir, const JkModFile *f, char delta[1024],
                      char src[JK_DELTA_MAX_SRC][1024], int *n)
{
    char dd[1024];
    static JkDeltaInfo in;
    if (!f->delta || !p->files_dir || jk_fs_join(dd, sizeof dd, p->files_dir, "deltas") < 0
        || jk_fs_join(delta, 1024, dd, f->delta) < 0 || jk_delta_info(delta, &in) < 0
        || strcmp(in.target_sha, f->sha) || in.target_size != f->size)
        return -1;
    for (int i = 0; i < in.nsrc; i++)
        if (find_source(p, mods_dir, f, in.src_sha[i], in.src_size[i], src[i], 1024) < 0) return -1;
    *n = in.nsrc;
    return 0;
}

int jk_mods_scan_paths_table(const JkModFile *t, int n, const JkPaths *p, const char *mods_dir, JkModsScan *out)
{
    if (!p || jk_mods_scan_table(t, n, mods_dir, p->game_dir, out) < 0) return -1;
    author_index(mods_dir);
    for (int i = 0; i < n; i++) {
        if (t[i].src != JK_MOD_BUILD || at_hash(p->game_dir, &t[i], NULL) || at_hash(mods_dir, &t[i], NULL)) continue;
        static char delta[1024], src[JK_DELTA_MAX_SRC][1024];
        int k;
        if (sources_of(p, mods_dir, &t[i], delta, src, &k) == 0) out->buildable++;
    }
    return 0;
}

int jk_mods_scan_paths(const JkPaths *p, const char *mods_dir, JkModsScan *out)
{
    return jk_mods_scan_paths_table(TABLE, TABLE_N, p, mods_dir, out);
}

int jk_mods_install_table(const JkModFile *t, int n, const JkPaths *p, const char *mods_dir, JkProgress cb, void *ctx,
                          int *installed, char *why, size_t cap)
{
    char dummy[8];
    if (!why || !cap) { why = dummy; cap = sizeof dummy; }
    why[0] = 0;
    if (installed) *installed = 0;
    if (!p || !t) { snprintf(why, cap, "no folders"); return -1; }
    int have_mods = mods_dir && jk_fs_is_dir(mods_dir);
    author_index(have_mods ? mods_dir : NULL);
    if (!have_mods) mods_dir = NULL;
    static JkEntry list[JK_MANIFEST_MAX];
    int k = 0, done = 0;
    /* 1. the release files as they are on the drive */
    for (int i = 0; have_mods && i < n && k < JK_MANIFEST_MAX; i++) {
        if (at_hash(p->game_dir, &t[i], NULL)) continue;     /* already there */
        if (!at_hash(mods_dir, &t[i], NULL)) continue;       /* missing or not the release file */
        memset(&list[k], 0, sizeof list[k]);
        list[k].kind = JK_M_FILE;
        snprintf(list[k].path, sizeof list[k].path, "%s", t[i].path);
        snprintf(list[k].sha, sizeof list[k].sha, "%s", t[i].sha);
        list[k].size = t[i].size;
        k++;
    }
    if (k) {
        if (jk_patch_install_entries(p, list, k, mods_dir, cb, ctx, why, cap) < 0) return -1;
        done += k;
    }
    /* 2. our builds, made from the player's own files with the package's deltas */
    char staging[1024], missing[JK_PATH_MAX] = "";
    if (!p->data_root || jk_fs_join(staging, sizeof staging, p->data_root, "staging") < 0) {
        snprintf(why, cap, "no data folder");
        return -1;
    }
    k = 0;
    for (int i = 0; i < n && k < JK_MANIFEST_MAX; i++) {
        if (t[i].src != JK_MOD_BUILD || !t[i].delta || at_hash(p->game_dir, &t[i], NULL)) continue;
        static char delta[1024], src[JK_DELTA_MAX_SRC][1024];
        const char *sp[JK_DELTA_MAX_SRC];
        int ns = 0;
        if (sources_of(p, mods_dir, &t[i], delta, src, &ns) < 0) {
            if (!missing[0]) snprintf(missing, sizeof missing, "%s", t[i].path);
            continue;
        }
        for (int s = 0; s < ns; s++) sp[s] = src[s];
        char out[1024], dwhy[160];
        if (cb) cb(ctx, i, n, t[i].path);
        if (jk_fs_join(out, sizeof out, staging, t[i].path) < 0
            || jk_delta_apply(delta, sp, ns, out, dwhy, sizeof dwhy) < 0) {
            snprintf(why, cap, "could not build %s: %s", t[i].path, dwhy);
            return -1;
        }
        memset(&list[k], 0, sizeof list[k]);
        list[k].kind = JK_M_FILE;
        snprintf(list[k].path, sizeof list[k].path, "%s", t[i].path);
        snprintf(list[k].sha, sizeof list[k].sha, "%s", t[i].sha);
        list[k].size = t[i].size;
        k++;
    }
    if (k) {
        int r = jk_patch_install_entries(p, list, k, staging, cb, ctx, why, cap);
        for (int i = 0; i < k; i++) {                       /* the staged copies go either way */
            char s[1024];
            if (jk_fs_join(s, sizeof s, staging, list[i].path) == 0) jk_fs_remove(s);
        }
        if (r < 0) return -1;
        done += k;
    }
    if (installed) *installed = done;
    if (done) return 0;
    JkModsScan s;
    jk_mods_scan_table(t, n, mods_dir, p->game_dir, &s);
    if (s.installed == n) return 0;
    if (have_mods && s.found)
        snprintf(why, cap, "no verified mod file in " JK_MODS_DIR " (%d found, %d at another version)", s.found, s.bad);
    else if (missing[0])
        snprintf(why, cap, "cannot build %s: a file it is made from is not in the game folder or %s", missing,
                 have_mods ? JK_MODS_DIR : "on a USB drive");
    else if (!have_mods)
        snprintf(why, cap, "no " JK_MODS_DIR " folder on the USB drive");
    else
        snprintf(why, cap, "no verified mod file in " JK_MODS_DIR " (%d found, %d at another version)", s.found, s.bad);
    return -1;
}

int jk_mods_install(const JkPaths *p, const char *mods_dir, JkProgress cb, void *ctx, int *installed,
                    char *why, size_t cap)
{
    return jk_mods_install_table(TABLE, TABLE_N, p, mods_dir, cb, ctx, installed, why, cap);
}
