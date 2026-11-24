/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The mod set, declarative (launcher r4). See jk_modset.h. */
#include "jk_modset.h"
#include "jk_archive.h"
#include "jk_delta.h"
#include "jk_fs.h"
#include "jk_jdlz.h"
#include "jk_manifest.h"
#include "jk_sha256.h"
#include "jk_strings.h"
#include "jk_ups.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "jk_modset_catalog.inc"

static const JkMsCatalog CATALOG = {
    MS_ARCHIVES, (int)(sizeof MS_ARCHIVES / sizeof MS_ARCHIVES[0]),
    MS_FILES, (int)(sizeof MS_FILES / sizeof MS_FILES[0]),
    MS_RECIPES, (int)(sizeof MS_RECIPES / sizeof MS_RECIPES[0]),
    MS_PATHS, (int)(sizeof MS_PATHS / sizeof MS_PATHS[0]),
    MS_BAD, (int)(sizeof MS_BAD / sizeof MS_BAD[0]),
};

const JkMsCatalog *jk_modset_catalog(void) { return &CATALOG; }

const char *jk_modset_name(int mod)
{
    return mod == JK_MOD_X ? JK_S_E_NAME_X : mod == JK_MOD_H ? JK_S_E_NAME_H : mod == JK_MOD_R ? JK_S_E_NAME_R : "?";
}

#define PATH 1024
static int restore_mode;          /* jk_modset_apply(none_only): Restore */
static int say(char *why, size_t cap, const char *fmt, const char *a, const char *b)
{
    if (why && cap) snprintf(why, cap, fmt, a ? a : "", b ? b : "");
    return -1;
}

/* <data_root>/top[/sub[/rel]] */
static int dr_path(char *out, const JkPaths *p, const char *top, const char *sub, const char *rel)
{
    char a[PATH], b[PATH];
    if (!p || !p->data_root || jk_fs_join(a, sizeof a, p->data_root, top) < 0) return -1;
    if (!sub) { snprintf(out, PATH, "%s", a); return 0; }
    if (jk_fs_join(b, sizeof b, a, sub) < 0) return -1;
    if (!rel) { snprintf(out, PATH, "%s", b); return 0; }
    return jk_fs_join(out, PATH, b, rel);
}
/* <data_root>/modset[/sub[/rel]] */
static int ms_path(char *out, const JkPaths *p, const char *sub, const char *rel) { return dr_path(out, p, "modset", sub, rel); }
/* <data_root>/backup/pristine[/rel] */
static int pristine_path(char *out, const JkPaths *p, const char *rel) { return dr_path(out, p, "backup", "pristine", rel); }
static int pristine_list(char *out, const JkPaths *p) { return dr_path(out, p, "backup", "pristine", "pristine.txt"); }
static int cleanup_list(char *out, const JkPaths *p) { return dr_path(out, p, "state", "cleanup.txt", NULL); }

/* ---- the player's choice: "on <bits>" ---- */

int jk_modset_choice(const JkPaths *p)
{
    char path[PATH], line[64];
    int on = -1;
    if (ms_path(path, p, "choice.txt", NULL) == 0) {
        FILE *f = fopen(path, "r");
        if (f && fgets(line, sizeof line, f) && sscanf(line, "on %d", &on) != 1) on = -1;
        if (f) fclose(f);
    }
    if (on < 0 || on >= JK_MS_BIT(JK_MOD_N)) return JK_MS_DEFAULT_ON;
    return on;
}

int jk_modset_set_choice(const JkPaths *p, int on)
{
    char path[PATH], buf[32];
    if (on < 0 || on >= JK_MS_BIT(JK_MOD_N) || ms_path(path, p, "choice.txt", NULL) < 0 || jk_fs_mkdir_parent(path) < 0) return -1;
    int n = snprintf(buf, sizeof buf, "on %d\n", on);
    return jk_fs_write_atomic(path, buf, (size_t)n);
}

/* ---- hashes, cached for the run by path + size + time + inode ---- */

typedef struct HashEnt { char path[PATH]; long long size, mtime, ino; char sha[65]; } HashEnt;
#define HASH_MAX 96
static HashEnt hcache[HASH_MAX];
static int hcache_n;

static long long stat_of(const char *path, long long *size, long long *ino)
{
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    if (size) *size = (long long)st.st_size;
    if (ino) *ino = (long long)st.st_ino;
    return (long long)st.st_mtime;
}

/* sha of path; 0 ok, -1 missing. want_size >= 0: a file of another size is
 * reported with sha "" (no 532 MB hash of a file that cannot match). */
static int hash_of(const char *path, long long want_size, char sha[65])
{
    long long size = -1, ino = -1, mt = stat_of(path, &size, &ino);
    sha[0] = 0;
    if (mt < 0) return -1;
    if (want_size >= 0 && size != want_size) return 0;
    for (int i = 0; i < hcache_n; i++)
        if (hcache[i].size == size && hcache[i].mtime == mt && hcache[i].ino == ino && !strcmp(hcache[i].path, path)) {
            memcpy(sha, hcache[i].sha, 65);
            return 0;
        }
    if (jk_fs_sha256(path, sha, NULL) < 0) { sha[0] = 0; return -1; }
    HashEnt *e = &hcache[hcache_n < HASH_MAX ? hcache_n++ : HASH_MAX - 1];
    snprintf(e->path, sizeof e->path, "%s", path);
    e->size = size;
    e->mtime = mt;
    e->ino = ino;
    memcpy(e->sha, sha, 65);
    return 0;
}

static void hash_forget(const char *path)
{
    for (int i = 0; i < hcache_n; i++)
        if (!strcmp(hcache[i].path, path)) hcache[i].path[0] = 0;
}

/* ---- catalog lookups ---- */

static const JkMsFile *file_by_sha(const JkMsCatalog *c, const char *sha)
{
    for (int i = 0; i < c->nfiles; i++) if (!strcmp(c->files[i].sha, sha)) return &c->files[i];
    return NULL;
}
static const JkMsRecipe *recipe_by_sha(const JkMsCatalog *c, const char *sha)
{
    for (int i = 0; i < c->nrecipes; i++) if (!strcmp(c->recipes[i].sha, sha)) return &c->recipes[i];
    return NULL;
}
static int is_bad(const JkMsCatalog *c, const char *sha)
{
    for (int i = 0; i < c->nbad; i++) if (!strcmp(c->bad[i], sha)) return 1;
    return 0;
}
/* one of our results (or a known-bad file) */
static int is_output(const JkMsCatalog *c, const char *sha)
{
    return sha[0] && (recipe_by_sha(c, sha) || is_bad(c, sha));
}
/* the untouched file of a managed path (a GAME entry of that name) */
static const JkMsFile *vanilla_of(const JkMsCatalog *c, const char *path)
{
    for (int i = 0; i < c->nfiles; i++)
        if (c->files[i].kind == JK_MS_GAME && !strcmp(c->files[i].name, path)) return &c->files[i];
    return NULL;
}
static int path_index(const JkMsCatalog *c, const char *path)
{
    for (int i = 0; i < c->npaths; i++) if (!strcmp(c->paths[i].path, path)) return i;
    return -1;
}
static const char *target_of(const JkMsPath *mp, int set)
{
    const char *t = set >= 0 && set < JK_MS_SETS ? mp->at[set] : NULL;
    return t ? t : "=";
}

/* the entry name matches a catalog name: exactly, or for the pack (a folder
 * download with its own top folder) as a '/'-bounded suffix */
static int name_match(int mod, const char *entry, const char *name)
{
    size_t a = strlen(entry), b = strlen(name);
    if (a == b) return !strcmp(entry, name);
    if (mod != JK_MOD_X || a < b + 1) return 0;
    return entry[a - b - 1] == '/' && !strcmp(entry + a - b, name);
}

static void rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[PATH];
        struct stat st;
        if (jk_fs_join(p, sizeof p, dir, e->d_name) < 0 || lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { rm_tree(p); rmdir(p); }
        else unlink(p);
    }
    if (d) closedir(d);
}
static void rm_sub(const JkPaths *p, const char *sub)
{
    char d[PATH];
    if (ms_path(d, p, sub, NULL) == 0) rm_tree(d);
}

/* ---- the game folder and the pristine store, per managed path ---- */

#define NP_MAX 64
typedef struct Cur { int present; char sha[65]; } Cur;   /* sha "": a file that is none of the catalog's */
static Cur cur[NP_MAX];
static int pr_ok[NP_MAX];                                  /* the pristine store holds the untouched file */

static void pristine_load(const JkMsCatalog *c, const JkPaths *p)
{
    char list[PATH], line[PATH + 128];
    memset(pr_ok, 0, sizeof pr_ok);
    if (pristine_list(list, p) < 0) return;
    FILE *f = fopen(list, "r");
    while (f && fgets(line, sizeof line, f)) {
        char sha[65], rel[PATH], pp[PATH];
        long long size;
        if (sscanf(line, "%64s %lld %1023[^\n]", sha, &size, rel) != 3) continue;
        int i = path_index(c, rel);
        const JkMsFile *v = i >= 0 && i < NP_MAX ? vanilla_of(c, rel) : NULL;
        if (!v || strcmp(v->sha, sha) || v->size != size) continue;
        if (pristine_path(pp, p, rel) == 0 && jk_fs_size(pp) == size) pr_ok[i] = 1;
    }
    if (f) fclose(f);
}

static int pristine_save(const JkMsCatalog *c, const JkPaths *p)
{
    char list[PATH];
    static char buf[NP_MAX * (PATH + 100)];
    size_t n = 0;
    for (int i = 0; i < c->npaths && i < NP_MAX; i++) {
        const JkMsFile *v = pr_ok[i] ? vanilla_of(c, c->paths[i].path) : NULL;
        if (v) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %lld %s\n", v->sha, v->size, v->name);
    }
    if (pristine_list(list, p) < 0 || jk_fs_mkdir_parent(list) < 0) return -1;
    return jk_fs_write_atomic(list, buf, n);
}

/* The install record: "JK4MS-REC 1", "set N", then per path
 * "<sha|-|?> size mtime ino path". Trusted when every game file still has its
 * size, time and inode, so a scan does not hash 532 MB. */
static const char REC_HEAD[] = "JK4MS-REC 1\n";

static int record_read(const JkMsCatalog *c, const JkPaths *p)
{
    char rec[PATH], line[PATH + 200];
    int set = -1, seen = 0;
    if (ms_path(rec, p, "installed.txt", NULL) < 0) return -1;
    FILE *f = fopen(rec, "r");
    if (!f) return -1;
    if (!fgets(line, sizeof line, f) || strcmp(line, REC_HEAD) || !fgets(line, sizeof line, f) || sscanf(line, "set %d", &set) != 1
        || set < 0 || set >= JK_MS_SETS) { fclose(f); return -1; }
    while (fgets(line, sizeof line, f)) {
        char sha[65], rel[PATH], game[PATH];
        long long size, mt, ino, s2 = -1, i2 = -1;
        if (sscanf(line, "%64s %lld %lld %lld %1023[^\n]", sha, &size, &mt, &ino, rel) != 5) { set = -1; break; }
        int i = path_index(c, rel);
        if (i < 0 || i >= NP_MAX || jk_fs_join(game, sizeof game, p->game_dir, rel) < 0) { set = -1; break; }
        long long m2 = stat_of(game, &s2, &i2);
        if (!strcmp(sha, "-")) {
            if (m2 >= 0) { set = -1; break; }
            cur[i].present = 0;
            cur[i].sha[0] = 0;
        } else {
            if (m2 != mt || s2 != size || i2 != ino) { set = -1; break; }
            cur[i].present = 1;
            snprintf(cur[i].sha, sizeof cur[i].sha, "%s", strcmp(sha, "?") ? sha : "");
        }
        seen++;
    }
    fclose(f);
    return seen == c->npaths ? set : -1;
}

static void record_write(const JkMsCatalog *c, const JkPaths *p, int set)
{
    char path[PATH], game[PATH];
    static char buf[NP_MAX * (PATH + 200) + 64];
    size_t n = (size_t)snprintf(buf, sizeof buf, "%sset %d\n", REC_HEAD, set);
    for (int i = 0; i < c->npaths && i < NP_MAX; i++) {
        long long size = 0, ino = 0, mt = -1;
        if (jk_fs_join(game, sizeof game, p->game_dir, c->paths[i].path) == 0) mt = stat_of(game, &size, &ino);
        if (mt < 0) n += (size_t)snprintf(buf + n, sizeof buf - n, "- 0 0 0 %s\n", c->paths[i].path);
        else n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %lld %lld %lld %s\n", cur[i].sha[0] ? cur[i].sha : "?",
                                   size, mt, ino, c->paths[i].path);
        if (n >= sizeof buf) return;
    }
    if (ms_path(path, p, "installed.txt", NULL) == 0 && jk_fs_mkdir_parent(path) == 0) jk_fs_write_atomic(path, buf, n);
}

/* path i holds what subset s names there */
static int path_matches(const JkMsCatalog *c, int i, int s)
{
    const JkMsPath *mp = &c->paths[i];
    const char *t = target_of(mp, s);
    if (strcmp(t, "=")) return cur[i].present && !strcmp(cur[i].sha, t);
    if (!cur[i].present) return 1;
    if (mp->vanilla) {
        const JkMsFile *v = vanilla_of(c, mp->path);
        return v && !strcmp(cur[i].sha, v->sha);
    }
    return 0;                                               /* a path the game does not have ends absent */
}

/* A game file's hash, cached across runs in <modset>/hashes.txt by size,
 * time and inode ("size mtime ino sha path"), so scans before the first
 * PATCH do not hash the 532 MB road every time. */
static int game_hash(const JkPaths *p, const char *path, char sha[65])
{
    long long size = -1, ino = -1, mt = stat_of(path, &size, &ino);
    char cache[PATH], line[PATH + 160];
    sha[0] = 0;
    if (mt < 0) return -1;
    if (ms_path(cache, p, "hashes.txt", NULL) == 0) {
        FILE *f = fopen(cache, "r");
        while (f && fgets(line, sizeof line, f)) {
            long long s, m, n;
            char h[65], q[PATH];
            if (sscanf(line, "%lld %lld %lld %64s %1023[^\n]", &s, &m, &n, h, q) == 5 && s == size && m == mt && n == ino
                && !strcmp(q, path)) {
                memcpy(sha, h, 65);
                fclose(f);
                return 0;
            }
        }
        if (f) fclose(f);
    }
    if (hash_of(path, -1, sha) < 0) return -1;
    if (ms_path(cache, p, "hashes.txt", NULL) == 0 && jk_fs_mkdir_parent(cache) == 0) {
        if (jk_fs_size(cache) > (256 << 10)) jk_fs_remove(cache);       /* old states: start over */
        FILE *f = fopen(cache, "a");
        if (f) { fprintf(f, "%lld %lld %lld %s %s\n", size, mt, ino, sha, path); fclose(f); }
    }
    return 0;
}

/* cur[] and the set in place: from the record, else from the hashes. */
static int load_state(const JkMsCatalog *c, const JkPaths *p)
{
    memset(cur, 0, sizeof cur);
    int set = record_read(c, p);
    if (set >= 0) return set;
    for (int i = 0; i < c->npaths && i < NP_MAX; i++) {
        const JkMsPath *mp = &c->paths[i];
        char game[PATH];
        long long size = -1;
        memset(&cur[i], 0, sizeof cur[i]);
        if (jk_fs_join(game, sizeof game, p->game_dir, mp->path) < 0 || stat_of(game, &size, NULL) < 0) continue;
        cur[i].present = 1;
        int cand = 0;                                       /* only a size the catalog knows is worth a hash */
        const JkMsFile *v = vanilla_of(c, mp->path);
        if (v && v->size == size) cand = 1;
        for (int s = 0; s < JK_MS_SETS && !cand; s++) {
            const JkMsRecipe *r = recipe_by_sha(c, target_of(mp, s));
            if (r && r->size == size) cand = 1;
        }
        if (cand && game_hash(p, game, cur[i].sha) < 0) cur[i].sha[0] = 0;
        if (!file_by_sha(c, cur[i].sha) && !is_output(c, cur[i].sha)) cur[i].sha[0] = 0;
    }
    for (int s = 0; s < JK_MS_SETS; s++) {
        int all = 1;
        for (int i = 0; i < c->npaths && all; i++) all = path_matches(c, i, s);
        if (all) return s;
    }
    return -1;
}

/* ---- the drop folders ---- */

typedef struct Arc {
    char path[PATH];
    char name[128];
    long long size;
    int mod;              /* JK_MOD_* when it is one of ours, -1 otherwise */
    int ready;            /* H/R: the whole archive matched its pin */
} Arc;
#define ARC_MAX 32
static Arc arcs[ARC_MAX];
static int narcs;
static unsigned char xfound[1024];      /* per catalog file: seen in a pack archive */

/* whole-archive hashes cached in <modset>/archives.txt: "size mtime sha path" */
static int arc_sha(const JkPaths *p, const char *path, char sha[65])
{
    long long size = -1, mt = stat_of(path, &size, NULL);
    char cache[PATH], line[PATH + 128];
    if (mt < 0) return -1;
    if (ms_path(cache, p, "archives.txt", NULL) == 0) {
        FILE *f = fopen(cache, "r");
        while (f && fgets(line, sizeof line, f)) {
            long long s, m;
            char h[65], q[PATH];
            if (sscanf(line, "%lld %lld %64s %1023[^\n]", &s, &m, h, q) == 4 && s == size && m == mt && !strcmp(q, path)) {
                memcpy(sha, h, 65);
                fclose(f);
                return 0;
            }
        }
        if (f) fclose(f);
    }
    if (jk_fs_sha256(path, sha, NULL) < 0) return -1;
    if (ms_path(cache, p, "archives.txt", NULL) == 0 && jk_fs_mkdir_parent(cache) == 0) {
        FILE *f = fopen(cache, "a");
        if (f) { fprintf(f, "%lld %lld %s %s\n", size, mt, sha, path); fclose(f); }
    }
    return 0;
}

typedef struct ListCtx { const JkMsCatalog *c; int x, h, r; } ListCtx;
static int on_entry(void *ctx, const JkArcEntry *e)
{
    ListCtx *l = ctx;
    for (int i = 0; i < l->c->nfiles && i < (int)sizeof xfound; i++) {
        const JkMsFile *f = &l->c->files[i];
        if (f->kind >= JK_MOD_N || !name_match(f->kind, e->name, f->name)) continue;
        if (f->kind == JK_MOD_X && e->size == f->size) { l->x++; xfound[i] = 1; }
        if (f->kind == JK_MOD_H) l->h++;
        if (f->kind == JK_MOD_R) l->r++;
    }
    return 0;
}

static void set_mod(JkMsScan *s, int mod, int state, const char *archive, const char *note)
{
    JkMsMod *m = &s->mod[mod];
    if (m->state == JK_MS_READY && state != JK_MS_READY) return;       /* a good copy wins over a bad one */
    m->state = state;
    snprintf(m->archive, sizeof m->archive, "%s", archive ? archive : "");
    snprintf(m->note, sizeof m->note, "%s", note ? note : "");
}

static void scan_archives(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, JkMsScan *out)
{
    memset(xfound, 0, sizeof xfound);
    narcs = 0;
    for (int d = 0; d < ndirs && d < JK_MS_DIRS_MAX; d++) {
        DIR *dir = dirs && dirs[d] ? opendir(dirs[d]) : NULL;
        struct dirent *e;
        while (dir && (e = readdir(dir)) && narcs < ARC_MAX) {
            Arc *a = &arcs[narcs];
            if (e->d_name[0] == '.' || jk_fs_join(a->path, sizeof a->path, dirs[d], e->d_name) < 0) continue;
            if (stat_of(a->path, &a->size, NULL) < 0) continue;
            JkArcType t = jk_archive_type(a->path);
            if (t == JK_ARC_UNKNOWN) continue;                    /* notes, screenshots: not ours to judge */
            snprintf(a->name, sizeof a->name, "%.127s", e->d_name);
            a->mod = -1;
            a->ready = 0;
            narcs++;
            char why[160] = "", sha[65] = "";
            ListCtx l = { c, 0, 0, 0 };
            int listed = t == JK_ARC_RAR ? -1 : jk_archive_list(a->path, on_entry, &l, why, sizeof why);
            int pinned = -1;
            for (int k = 0; k < c->narcs; k++) if (c->arcs[k].size == a->size) pinned = k;
            if (pinned >= 0 && arc_sha(p, a->path, sha) == 0 && !strcmp(sha, c->arcs[pinned].sha)) {
                a->mod = c->arcs[pinned].mod;
                a->ready = 1;
                set_mod(out, a->mod, JK_MS_READY, a->name, NULL);
            } else if (listed == 0 && l.x > 0) {
                a->mod = JK_MOD_X;
            } else if (pinned >= 0 || (listed == 0 && (l.h || l.r))) {
                int mod = pinned >= 0 ? c->arcs[pinned].mod : l.h ? JK_MOD_H : JK_MOD_R;
                char note[160];
                snprintf(note, sizeof note, JK_S_E_NOT_RELEASED,
                         a->name, jk_modset_name(mod));
                set_mod(out, mod, JK_MS_BAD, a->name, note);
            } else if (!out->unknown++) {
                if (t == JK_ARC_RAR) snprintf(out->unknown_name, sizeof out->unknown_name, JK_S_E_RAR, a->name);
                else if (listed < 0) snprintf(out->unknown_name, sizeof out->unknown_name, JK_S_E_DAMAGED_ARC, a->name);
                else snprintf(out->unknown_name, sizeof out->unknown_name, "%s", a->name);
            }
        }
        if (dir) closedir(dir);
    }
    /* the pack: every file we use must be in its archive(s) */
    int xarcs = 0;
    const char *first = NULL;
    for (int i = 0; i < narcs; i++) if (arcs[i].mod == JK_MOD_X) { xarcs++; if (!first) first = arcs[i].name; }
    if (xarcs) {
        const char *missing = NULL;
        for (int i = 0; i < c->nfiles && i < (int)sizeof xfound; i++)
            if (c->files[i].kind == JK_MOD_X && !xfound[i]) { missing = c->files[i].name; break; }
        if (!missing) {
            set_mod(out, JK_MOD_X, JK_MS_READY, first, NULL);
            if (xarcs > 1) snprintf(out->mod[JK_MOD_X].archive, sizeof out->mod[0].archive, JK_S_E_AND_MORE, first, xarcs - 1);
        } else {
            char note[160];
            snprintf(note, sizeof note, JK_S_E_INCOMPLETE, first, missing);
            set_mod(out, JK_MOD_X, JK_MS_BAD, first, note);
        }
    }
}

/* ---- planning and building ---- */

typedef struct Need { int mod; const char *name; const char *sha; long long size; int got; } Need;
#define NEED_MAX 512
#define PROB_MAX 8
typedef struct Ctx {
    const JkMsCatalog *c;
    const JkPaths *p;
    int build;                     /* 0: plan only */
    long long space;               /* bytes the build will write */
    Need need[NEED_MAX];
    int nneed;
    int ready;                     /* mods whose download is here and checked */
    int missing;                   /* mods whose download a needed file is missing from */
    const char *path;              /* the managed path being planned */
    char prob[PROB_MAX][160];
    int nprob;
    char npr_path[JK_MS_PROBLEMS][64];   /* paths the target needs an untouched copy of, and none exists */
    int npr;
    char left[JK_MS_PROBLEMS][64];       /* Restore: paths with no unchanged copy, left as they are */
    int nleft;
    JkProgress cb; void *cbx; int step, steps;
    char label[200];
} Ctx;
static Ctx X;

static void progress(Ctx *x, const char *what)
{
    snprintf(x->label, sizeof x->label, "%s", what ? what : "");
    if (x->cb) x->cb(x->cbx, x->step, x->steps, what);
}

/* r5: while one big file is read, built or checked, the line moves in MB
 * (console 2026-11-24: "0 of 60" stood still for minutes) */
static void mb_tick(void *ctx, long long done)
{
    Ctx *x = ctx;
    if (!x->cb) return;
    char line[260];
    snprintf(line, sizeof line, "%s (%lld MB)", x->label, done >> 20);
    x->cb(x->cbx, x->step, x->steps, line);
}

static int prob(Ctx *x, const char *fmt, const char *a, const char *b)
{
    char t[160];
    snprintf(t, sizeof t, fmt, a ? a : "", b ? b : "");
    for (int i = 0; i < x->nprob && i < PROB_MAX; i++) if (!strcmp(x->prob[i], t)) return -1;
    if (x->nprob < PROB_MAX) snprintf(x->prob[x->nprob], sizeof x->prob[0], "%s", t);
    x->nprob++;
    return -1;
}

/* A problem: the target needs the untouched rel and the store has none. */
static int no_pristine(Ctx *x, const char *rel)
{
    int dup = 0;
    for (int i = 0; i < x->npr && i < JK_MS_PROBLEMS; i++) dup |= !strcmp(x->npr_path[i], rel);
    if (!dup) {
        if (x->npr < JK_MS_PROBLEMS) snprintf(x->npr_path[x->npr], sizeof x->npr_path[0], "%s", rel);
        x->npr++;
    }
    return prob(x, JK_S_E_NO_PRISTINE, rel, NULL);
}

/* A file with this sha: found or made. out gets its path. 0 ok, -1 with a
 * problem noted. depth 0 is a result to install: it comes from the cache or
 * is built into it (built/<sha>). Deeper: a source, also taken from the game
 * folder or the pristine store as it is. */
static int need(Ctx *x, const char *sha, char out[PATH], int depth)
{
    char h[65];
    if (depth > 6) return prob(x, JK_S_E_INCONSISTENT, x->path, NULL);
    if (ms_path(out, x->p, "built", sha) == 0 && jk_fs_exists(out)) return 0;
    if (depth > 0) {
        if (ms_path(out, x->p, "src", sha) == 0 && jk_fs_exists(out)) return 0;
        for (int i = 0; i < x->c->npaths && i < NP_MAX; i++)
            if (cur[i].present && !strcmp(cur[i].sha, sha) && jk_fs_join(out, PATH, x->p->game_dir, x->c->paths[i].path) == 0)
                return 0;
    }
    const JkMsFile *f = depth > 0 ? file_by_sha(x->c, sha) : NULL;
    if (f && f->kind == JK_MS_GAME) {
        int i = path_index(x->c, f->name);
        if (i >= 0 && i < NP_MAX && pr_ok[i] && pristine_path(out, x->p, f->name) == 0) return 0;
        if (i < 0 && jk_fs_join(out, PATH, x->p->game_dir, f->name) == 0 && hash_of(out, f->size, h) == 0 && !strcmp(h, sha))
            return 0;                                       /* a game file we never change */
        return no_pristine(x, f->name);
    }
    if (f && f->kind < JK_MOD_N) {
        ms_path(out, x->p, "src", sha);
        if (x->build) return prob(x, JK_S_E_MISSING_IN_DL, jk_modset_name(f->kind), f->name);
        if (!(x->ready & JK_MS_BIT(f->kind))) {
            if (x->missing & JK_MS_BIT(f->kind)) return -1;
            x->missing |= JK_MS_BIT(f->kind);
            return prob(x, JK_S_E_NEED_DL, jk_modset_name(f->kind), x->path);
        }
        for (int i = 0; i < x->nneed; i++) if (!strcmp(x->need[i].sha, sha)) return 0;
        if (x->nneed >= NEED_MAX) return prob(x, JK_S_E_TOO_MANY, NULL, NULL);
        x->need[x->nneed++] = (Need){ f->kind, f->name, f->sha, f->size, 0 };
        x->space += f->size;
        return 0;
    }
    if (f && f->kind == JK_MS_PKG) {
        if (!x->p->files_dir || jk_fs_join(out, PATH, x->p->files_dir, f->name) < 0
            || hash_of(out, f->size, h) < 0 || strcmp(h, sha))
            return prob(x, JK_S_E_PKG_FILE, f->name, NULL);
        return 0;
    }
    const JkMsRecipe *r = recipe_by_sha(x->c, sha);
    if (!r) return prob(x, JK_S_E_INCONSISTENT, x->path, NULL);
    char dst[PATH], a[PATH], b[PATH];
    ms_path(dst, x->p, "built", sha);
    if (!x->build) x->space += r->size;
    if (r->how == JK_MS_COPY) {
        if (need(x, r->arg, a, depth + 1) < 0) return -1;
        if (x->build && (jk_fs_mkdir_parent(dst) < 0 || jk_fs_copy_checked(a, dst, sha) < 0))
            return prob(x, JK_S_E_WRITE, x->path, NULL);
    } else if (r->how == JK_MS_UPS) {
        int bad = need(x, r->arg, a, depth + 1) < 0;
        bad |= need(x, r->in, b, depth + 1) < 0;
        if (bad) return -1;
        char w[160];
        if (x->build) {
            char what[200];
            snprintf(what, sizeof what, JK_S_E_BUILDING, x->path);
            progress(x, what);
            if (jk_fs_mkdir_parent(dst) < 0 || jk_ups_apply(a, b, dst, w, sizeof w) < 0) return prob(x, JK_S_E_UPS_FAILED, w, NULL);
            hash_forget(dst);
            if (hash_of(dst, r->size, h) < 0 || strcmp(h, sha)) { jk_fs_remove(dst); return prob(x, JK_S_E_UPS_MISMATCH, NULL, NULL); }
        }
    } else {
        char delta[PATH], dd[PATH];
        JkDeltaInfo *in = malloc(sizeof *in);
        if (!in) return prob(x, JK_S_E_NO_MEMORY, NULL, NULL);
        if (!x->p->files_dir || jk_fs_join(dd, sizeof dd, x->p->files_dir, "deltas") < 0
            || jk_fs_join(delta, sizeof delta, dd, r->arg) < 0 || jk_delta_info(delta, in) < 0 || strcmp(in->target_sha, sha)) {
            free(in);
            return prob(x, JK_S_E_PKG_DELTA, r->arg, NULL);
        }
        /* per call: a source may itself be built first (H over the X InGameA) */
        char (*srcs)[PATH] = malloc((size_t)JK_DELTA_MAX_SRC * PATH);
        const char **sp = malloc(sizeof *sp * JK_DELTA_MAX_SRC);
        int n = in->nsrc, rc = 0;
        if (!srcs || !sp) rc = prob(x, JK_S_E_NO_MEMORY, NULL, NULL);
        for (int i = 0; srcs && sp && i < n; i++) {                 /* every source, to name every problem */
            if (need(x, in->src_sha[i], srcs[i], depth + 1) < 0) rc = -1;
            else sp[i] = srcs[i];
        }
        free(in);
        if (rc == 0 && x->build) {
            char w[160], what[200];
            snprintf(what, sizeof what, JK_S_E_BUILDING, x->path);
            progress(x, what);
            if (jk_fs_mkdir_parent(dst) < 0 || jk_delta_apply(delta, sp, n, dst, w, sizeof w) < 0)
                rc = prob(x, JK_S_E_BUILD_FAILED, w, NULL);
        }
        free(srcs);
        free(sp);
        if (rc < 0) return -1;
    }
    snprintf(out, PATH, "%s", dst);                   /* made (or, when planning, to be made) here */
    return 0;
}

/* ---- the plan: one action per path that changes ---- */

enum { A_INSTALL = 1, A_DELETE };
enum { S_BUILT = 1, S_PRISTINE };
/* what becomes of the file swapped out: B into built/ (ours), D dropped (the
 * untouched file, the store has the same bytes), A added to the pristine
 * store, K anything else: moved to backup/kept/<path> (.2, .3 ...), never
 * deleted and never put back automatically */
typedef struct Act { int kind, src; const JkMsPath *mp; int idx; char from[PATH]; char sha[65]; char disp; char old[65]; } Act;
#define ACT_MAX 64
static Act acts[ACT_MAX];
static int nacts;

static char disp_of(const JkMsCatalog *c, int i)
{
    if (!cur[i].present) return '-';
    if (recipe_by_sha(c, cur[i].sha)) return 'B';
    const JkMsFile *v = c->paths[i].vanilla ? vanilla_of(c, c->paths[i].path) : NULL;
    if (v && !strcmp(cur[i].sha, v->sha)) return pr_ok[i] ? 'D' : 'A';
    return 'K';
}

static void plan(Ctx *x, int set)
{
    const JkMsCatalog *c = x->c;
    nacts = 0;
    for (int i = 0; i < c->npaths && i < NP_MAX && nacts < ACT_MAX; i++) {
        const JkMsPath *mp = &c->paths[i];
        const char *t = target_of(mp, set);
        x->path = mp->path;
        char game[PATH];
        if (jk_fs_join(game, sizeof game, x->p->game_dir, mp->path) < 0 || jk_fs_safe_path(x->p->game_dir, mp->path) < 0) {
            prob(x, JK_S_E_UNSAFE, mp->path, NULL);
            continue;
        }
        if (path_matches(c, i, set) && (strcmp(t, "=") || !mp->vanilla || cur[i].present || !pr_ok[i])) continue;
        Act *a = &acts[nacts];
        memset(a, 0, sizeof *a);
        a->mp = mp;
        a->idx = i;
        a->kind = A_INSTALL;
        if (strcmp(t, "=")) {
            if (need(x, t, a->from, 0) < 0) continue;
            ms_path(a->from, x->p, "built", t);
            a->src = S_BUILT;
            snprintf(a->sha, sizeof a->sha, "%s", t);
        } else if (mp->vanilla) {
            const JkMsFile *v = vanilla_of(c, mp->path);
            if (!v || !pr_ok[i]) {
                if (restore_mode) {                       /* Restore: no unchanged copy, so it stays as it is */
                    if (x->nleft < JK_MS_PROBLEMS) snprintf(x->left[x->nleft], sizeof x->left[0], "%s", mp->path);
                    x->nleft++;
                } else no_pristine(x, mp->path);
                continue;
            }
            ms_path(a->from, x->p, "stage", mp->path);
            a->src = S_PRISTINE;
            snprintf(a->sha, sizeof a->sha, "%s", v->sha);
            x->space += v->size;
        } else {
            a->kind = A_DELETE;                               /* out of the game folder (kept if foreign) */
        }
        a->disp = disp_of(c, i);
        snprintf(a->old, sizeof a->old, "%s", cur[i].sha[0] ? cur[i].sha : "-");
        nacts++;
    }
}

static void ctx_init(Ctx *x, const JkMsCatalog *c, const JkPaths *p, int ready)
{
    memset(x, 0, sizeof *x);
    x->c = c;
    x->p = p;
    x->ready = ready;
}

/* The target (target rule in jk_modset.h); leaves the final dry plan in X. */
static int compute_target(const JkMsCatalog *c, const JkPaths *p, int on, int inst, int ready, int force)
{
    Ctx *x = &X;
    int set = force >= 0 ? force : on;
    for (int iter = 0; force < 0 && iter < JK_MOD_N; iter++) {
        ctx_init(x, c, p, ready);
        plan(x, set);
        if (!x->nprob) break;
        /* ON, not installed, no download: left out when the target cannot be
         * made with it (the ones a needed file is missing from first) */
        int cand = set & ~inst & ~ready;
        int drop = x->missing & cand ? x->missing & cand : cand;
        if (!drop) break;
        set &= ~drop;
    }
    ctx_init(x, c, p, ready);
    plan(x, set);
    return set;
}

static int scan_internal(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, int force, JkMsScan *out)
{
    if (!c || !out) return -1;
    memset(out, 0, sizeof *out);
    if (!p || !p->game_dir || !p->data_root || c->npaths > NP_MAX) return -1;
    scan_archives(c, p, dirs, ndirs, out);
    out->on = jk_modset_choice(p);
    out->installed = load_state(c, p);
    pristine_load(c, p);
    for (int i = 0; i < c->npaths; i++) {
        if (!c->paths[i].vanilla || pr_ok[i]) continue;
        if (out->nmissing_pristine < JK_MS_PROBLEMS)
            snprintf(out->missing_pristine[out->nmissing_pristine], sizeof out->missing_pristine[0], "%s", c->paths[i].path);
        out->nmissing_pristine++;
    }
    int ready = 0;
    for (int m = 0; m < JK_MOD_N; m++) if (out->mod[m].state == JK_MS_READY) ready |= JK_MS_BIT(m);
    int inst = out->installed >= 0 ? out->installed : 0;
    out->target = compute_target(c, p, out->on, inst, ready, force);
    out->want = out->target;
    out->problems = X.nprob;
    for (int i = 0; i < X.nprob && i < JK_MS_PROBLEMS; i++) snprintf(out->problem[i], sizeof out->problem[0], "%s", X.prob[i]);
    out->npristine_needed = X.npr;
    out->nleft = X.nleft;
    for (int i = 0; i < X.nleft && i < JK_MS_PROBLEMS; i++) snprintf(out->left[i], sizeof out->left[0], "%s", X.left[i]);
    for (int i = 0; i < X.npr && i < JK_MS_PROBLEMS; i++)
        snprintf(out->pristine_needed[i], sizeof out->pristine_needed[0], "%s", X.npr_path[i]);
    for (int m = 0; m < JK_MOD_N; m++) {
        int bit = JK_MS_BIT(m);
        if (out->on & bit) {
            if (out->target & bit) out->row[m] = inst & bit ? JK_MS_ROW_ON_INSTALLED : JK_MS_ROW_ON_PENDING;
            else out->row[m] = out->mod[m].state == JK_MS_BAD ? JK_MS_ROW_BAD : JK_MS_ROW_ON_NO_ARCHIVE;
        } else {
            out->row[m] = inst & bit ? JK_MS_ROW_OFF_PENDING : JK_MS_ROW_OFF;
        }
        out->cached[m] = (out->target & bit) && !(ready & bit);
    }
    return 0;
}

int jk_modset_scan(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, JkMsScan *out)
{
    return scan_internal(c, p, dirs, ndirs, -1, out);
}

/* ---- extraction of the archive entries the plan needs ---- */

typedef struct Ex { Ctx *x; int mod; int cur; int fd; JkSha256 h; long long n; char tmp[PATH + 8]; } Ex;

static int ex_want(void *ctx, const JkArcEntry *e)
{
    Ex *ex = ctx;
    ex->cur = -1;
    for (int i = 0; i < ex->x->nneed; i++) {
        Need *nd = &ex->x->need[i];
        if (nd->got || nd->mod != ex->mod || nd->size != e->size || !name_match(nd->mod, e->name, nd->name)) continue;
        char dst[PATH];
        if (ms_path(dst, ex->x->p, "src", nd->sha) < 0 || jk_fs_mkdir_parent(dst) < 0) return 0;
        snprintf(ex->tmp, sizeof ex->tmp, "%s.part", dst);
        unlink(ex->tmp);
        ex->fd = open(ex->tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
        if (ex->fd < 0) return 0;
        jk_sha256_init(&ex->h);
        ex->n = 0;
        ex->cur = i;
        return 1;
    }
    return 0;
}
static int ex_sink(void *ctx, const JkArcEntry *e, const unsigned char *p, size_t n)
{
    Ex *ex = ctx;
    (void)e;
    jk_sha256_update(&ex->h, p, n);
    if (((ex->n + (long long)n) >> 23) != (ex->n >> 23)) mb_tick(ex->x, ex->n + (long long)n);
    ex->n += (long long)n;
    while (n) {
        ssize_t w = write(ex->fd, p, n);
        if (w <= 0) return 1;                    /* disk full: stop */
        p += w;
        n -= (size_t)w;
    }
    return 0;
}
static int ex_done(void *ctx, const JkArcEntry *e, int ok)
{
    Ex *ex = ctx;
    (void)e;
    if (ex->cur < 0) return 0;
    Need *nd = &ex->x->need[ex->cur];
    char sha[65], dst[PATH];
    jk_sha256_final_hex(&ex->h, sha);
    int good = ok && ex->n == nd->size && !strcmp(sha, nd->sha) && fsync(ex->fd) == 0;
    close(ex->fd);
    ex->fd = -1;
    if (good && ms_path(dst, ex->x->p, "src", nd->sha) == 0 && jk_fs_rename(ex->tmp, dst) == 0) nd->got = 1;
    else unlink(ex->tmp);
    ex->cur = -1;
    return 0;
}

static int extract_all(Ctx *x)
{
    int bad = 0;
    for (int mod = 0; mod < JK_MOD_N; mod++) {
        int wanted = 0;
        for (int i = 0; i < x->nneed; i++) if (x->need[i].mod == mod && !x->need[i].got) wanted++;
        if (!wanted) continue;
        for (int a = 0; a < narcs; a++) {
            if (arcs[a].mod != mod || (mod != JK_MOD_X && !arcs[a].ready)) continue;
            char what[200];
            snprintf(what, sizeof what, JK_S_E_READING, arcs[a].name);
            progress(x, what);
            static Ex ex;
            memset(&ex, 0, sizeof ex);
            ex.x = x;
            ex.mod = mod;
            ex.cur = -1;
            ex.fd = -1;
            JkArcOps ops = { &ex, ex_want, ex_sink, ex_done };
            char w[160] = "";
            int r = jk_archive_extract(arcs[a].path, &ops, w, sizeof w);
            if (ex.fd >= 0) { close(ex.fd); unlink(ex.tmp); }
            if (r < 0) { prob(x, "%s: %s", arcs[a].name, w); bad = 1; }
        }
        for (int i = 0; i < x->nneed; i++)
            if (x->need[i].mod == mod && !x->need[i].got) {
                prob(x, JK_S_E_MISSING_IN_DL, jk_modset_name(mod), x->need[i].name);
                bad = 1;
            }
    }
    return bad ? -1 : 0;
}

/* ---- the journal ---- */

static int test_fault;
void jk_modset_test_fault(int after_renames) { test_fault = after_renames > 0 ? after_renames : 0; }

/* The journal names the game folder it was written for and what ran
 * ("patch" or "restore"): "JK4MS 3", "game <dir>", "op <op>", "state <s>",
 * then one line per action. A journal is acted on only in its own folder. */

static int write_journal(const JkPaths *p, const char *state)
{
    char path[PATH];
    static char buf[ACT_MAX * (2 * PATH + 200) + 2 * PATH];
    size_t n = (size_t)snprintf(buf, sizeof buf, "JK4MS 3\ngame %s\nop %s\nstate %s\n", p->game_dir,
                                restore_mode ? "restore" : "patch", state);
    for (int i = 0; i < nacts && n < sizeof buf; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%d\t%s\t%s\t%c\t%s\n", acts[i].kind, acts[i].mp->path,
                              acts[i].from[0] ? acts[i].from : "-", acts[i].disp ? acts[i].disp : '-', acts[i].old);
    if (n >= sizeof buf || ms_path(path, p, "journal.txt", NULL) < 0 || jk_fs_mkdir_parent(path) < 0) return -1;
    return jk_fs_write_atomic(path, buf, n);
}

typedef struct JLine { int kind; char path[JK_PATH_MAX]; char from[PATH]; char disp; char old[65]; } JLine;
static JLine jl[ACT_MAX];
static char jgame[PATH];          /* the journal's game folder ("" when it names none: JK4MS 2) */
static int jrestore;              /* the journal was written by a Restore */

/* 0 no journal, 1 read, -1 damaged (jgame is still set when its line was read). */
static int read_journal(const JkPaths *p, char state[16], int *n)
{
    char path[PATH], line[2 * PATH + 300];
    *n = 0;
    state[0] = 0;
    jgame[0] = 0;
    jrestore = 0;
    if (ms_path(path, p, "journal.txt", NULL) < 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(line, sizeof line, f) || strncmp(line, "JK4MS ", 6)) { fclose(f); return -1; }
    for (;;) {
        if (!fgets(line, sizeof line, f) || !strchr(line, '\n')) { fclose(f); return -1; }
        *strchr(line, '\n') = 0;
        if (!strncmp(line, "game ", 5) && line[5]) { snprintf(jgame, sizeof jgame, "%.1000s", line + 5); continue; }
        if (!strncmp(line, "op ", 3)) { jrestore = !strcmp(line + 3, "restore"); continue; }
        if (sscanf(line, "state %15s", state) != 1) { fclose(f); return -1; }
        break;
    }
    while (*n < ACT_MAX && fgets(line, sizeof line, f)) {
        JLine *j = &jl[*n];
        char *fld[5] = { line, NULL, NULL, NULL, NULL };
        char *nl = strchr(line, '\n');
        if (!nl) { fclose(f); return -1; }
        *nl = 0;
        int k = 1;
        for (char *q = line; *q && k < 5; q++) if (*q == '\t') { *q = 0; fld[k++] = q + 1; }
        if (k < 3) { fclose(f); return -1; }
        j->kind = atoi(fld[0]);
        snprintf(j->path, sizeof j->path, "%s", fld[1]);
        snprintf(j->from, sizeof j->from, "%s", strcmp(fld[2], "-") ? fld[2] : "");
        j->disp = fld[3] ? fld[3][0] : 'K';
        snprintf(j->old, sizeof j->old, "%s", fld[4] ? fld[4] : "-");
        if (strstr(j->path, "..") || j->path[0] == '/') { fclose(f); return -1; }
        (*n)++;
    }
    fclose(f);
    return 1;
}

/* A file the launcher did not make leaves the game folder: into
 * <data_root>/backup/kept/<rel>, or <rel>.2, .3 ... (never overwritten). 0 ok;
 * -1 leaves it where it is. */
static int keep_file(const JkPaths *p, const char *from, const char *rel)
{
    char to[PATH], alt[PATH];
    if (dr_path(to, p, "backup", "kept", rel) < 0) return -1;
    if (!jk_fs_exists(to)) return jk_fs_rename(from, to);
    for (int k = 2; k < 1000; k++) {
        if (snprintf(alt, sizeof alt, "%.1000s.%d", to, k) >= (int)sizeof alt) return -1;
        if (!jk_fs_exists(alt)) return jk_fs_rename(from, alt);
    }
    return -1;
}

/* Leftovers in pre/ (a keep that failed): into backup/kept, never deleted. */
static void sweep_pre(const JkPaths *p, const char *dir, const char *rel)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char f[PATH], r[PATH];
        struct stat st;
        if (jk_fs_join(f, sizeof f, dir, e->d_name) < 0 || lstat(f, &st) != 0) continue;
        if (rel[0] ? jk_fs_join(r, sizeof r, rel, e->d_name) < 0 : snprintf(r, sizeof r, "%s", e->d_name) >= (int)sizeof r) continue;
        if (S_ISDIR(st.st_mode)) { sweep_pre(p, f, r); rmdir(f); }
        else if (S_ISREG(st.st_mode)) keep_file(p, f, r);
    }
    if (d) closedir(d);
}
static void sweep_pre_all(const JkPaths *p)
{
    char d[PATH];
    if (ms_path(d, p, "pre", NULL) == 0) sweep_pre(p, d, "");
}

/* After the swap: the file that was in the game folder (pre/<rel>). */
static void finish_pre(const JkPaths *p, const char *rel, char disp, const char *old)
{
    char pre[PATH], to[PATH];
    if (ms_path(pre, p, "pre", rel) < 0 || !jk_fs_exists(pre)) return;
    if (disp == 'B' && strcmp(old, "-") && ms_path(to, p, "built", old) == 0) {
        if (!jk_fs_exists(to) && jk_fs_rename(pre, to) == 0) return;
        jk_fs_remove(pre);                                    /* ours, and the cache holds the same bytes */
        return;
    }
    if (disp == 'D') { jk_fs_remove(pre); return; }           /* the untouched file: the store holds the same bytes */
    if (disp == 'A' && pristine_path(to, p, rel) == 0 && !jk_fs_exists(to) && jk_fs_rename(pre, to) == 0) return;
    keep_file(p, pre, rel);
}

static int rollback(const JkPaths *p, int n)
{
    int bad = 0;
    for (int i = n - 1; i >= 0; i--) {
        char game[PATH], pre[PATH];
        if (jk_fs_join(game, sizeof game, p->game_dir, jl[i].path) < 0 || ms_path(pre, p, "pre", jl[i].path) < 0) { bad = 1; continue; }
        if (jl[i].from[0] && !jk_fs_exists(jl[i].from) && jk_fs_exists(game) && jk_fs_rename(game, jl[i].from) < 0) bad = 1;
        if (jk_fs_exists(pre)) {
            if (jk_fs_exists(game) && jk_fs_remove(game) < 0) bad = 1;      /* a new file with no source left: drop it */
            if (jk_fs_rename(pre, game) < 0) bad = 1;
        }
    }
    return bad ? -1 : 0;
}

/* The journal names another game folder than p's. */
static int journal_foreign(const JkPaths *p) { return jgame[0] && strcmp(jgame, p->game_dir); }

int jk_modset_pending(const JkPaths *p)
{
    char state[16];
    int n = 0;
    if (!p || !p->game_dir || !p->data_root) return 0;
    int r = read_journal(p, state, &n);
    if (r == 0 || journal_foreign(p)) return JK_MS_PEND_NONE;
    if (r < 0) return JK_MS_PEND_DAMAGED;
    return jrestore ? JK_MS_PEND_RESTORE : JK_MS_PEND_PATCH;
}

int jk_modset_recover(const JkPaths *p, char *why, size_t cap)
{
    char state[16], jpath[PATH];
    int n = 0, r = read_journal(p, state, &n);
    if (r == 0) return 0;
    if (journal_foreign(p)) {                         /* another folder's: never acted on here */
        if (why && cap) snprintf(why, cap, "the journal belongs to %s: left as it is", jgame);
        return JK_MS_OTHER_FOLDER;
    }
    if (r < 0) return say(why, cap, JK_S_E_JOURNAL, NULL, NULL);
    ms_path(jpath, p, "journal.txt", NULL);
    int rc;
    if (!strcmp(state, "commit")) {
        for (int i = 0; i < n; i++) finish_pre(p, jl[i].path, jl[i].disp, jl[i].old);
        rc = 2;
    } else {
        if (rollback(p, n) < 0) return say(why, cap, JK_S_E_ROLLBACK, NULL, NULL);
        rc = 1;
    }
    jk_fs_remove(jpath);
    rm_sub(p, "stage");
    rm_sub(p, "src");
    sweep_pre_all(p);
    return rc;
}

/* The journal to <modset>/<name> (an older one there is replaced: it holds
 * no player data, only the record). */
static void journal_aside(const JkPaths *p, const char *name)
{
    char from[PATH], to[PATH];
    if (ms_path(from, p, "journal.txt", NULL) == 0 && ms_path(to, p, name, NULL) == 0) {
        jk_fs_remove(to);
        if (jk_fs_rename(from, to) < 0) jk_fs_remove(from);
    }
}


/* A damaged journal, on an explicit PATCH or Restore: at each managed path
 * whose earlier file waits in pre/, the file in the game folder is taken out
 * only when it is provably ours (a catalog output, a package file, or the
 * untouched file the store holds) or when there is none; then the earlier
 * file goes back. Everything else stays where it is, and what is left in
 * pre/ is kept aside (backup/kept) by the caller. Nothing is deleted that
 * the launcher did not make. */
static void salvage(const JkMsCatalog *c, const JkPaths *p)
{
    pristine_load(c, p);
    for (int i = 0; i < c->npaths && i < NP_MAX; i++) {
        const char *rel = c->paths[i].path;
        char pre[PATH], game[PATH], h[65], to[PATH];
        if (ms_path(pre, p, "pre", rel) < 0 || !jk_fs_exists(pre) || jk_fs_join(game, sizeof game, p->game_dir, rel) < 0
            || jk_fs_safe_path(p->game_dir, rel) < 0)
            continue;
        if (jk_fs_exists(game)) {
            hash_forget(game);
            if (hash_of(game, -1, h) < 0) continue;
            const JkMsFile *v = c->paths[i].vanilla ? vanilla_of(c, rel) : NULL;
            const JkMsFile *f = file_by_sha(c, h);
            int recipe = recipe_by_sha(c, h) != NULL;
            int ours = recipe || is_bad(c, h) || (f && f->kind == JK_MS_PKG) || (v && !strcmp(v->sha, h) && pr_ok[i]);
            if (!ours) continue;                                  /* someone's file: left, pre/ kept aside */
            if (recipe && ms_path(to, p, "built", h) == 0 && !jk_fs_exists(to) && jk_fs_mkdir_parent(to) == 0
                && jk_fs_rename(game, to) == 0) { /* into the cache */ }
            else if (jk_fs_remove(game) < 0) continue;
        }
        if (jk_fs_mkdir_parent(game) == 0) jk_fs_rename(pre, game);
    }
    journal_aside(p, "journal.damaged.txt");
}

/* Before a PATCH or Restore: a journal left by an earlier one. 0 go on;
 * JK_MS_PENDING its rollback failed; -1 it belongs to another game folder
 * that still exists (its own open finishes it). The journal of a folder that
 * is gone is moved aside (its pre/ files are kept aside, never lost). */
static int prepare(const JkMsCatalog *c, const JkPaths *p, char *why, size_t cap)
{
    char state[16];
    int n = 0, r = read_journal(p, state, &n);
    if (r == 0) return 0;
    if (journal_foreign(p)) {
        if (jk_fs_is_dir(jgame)) return say(why, cap, JK_S_E_OTHER_GAME, jgame, NULL);
        journal_aside(p, "journal.other.txt");
        return 0;
    }
    if (r < 0) { salvage(c, p); return 0; }
    return jk_modset_recover(p, why, cap) < 0 ? JK_MS_PENDING : 0;
}

/* ---- the pristine store ---- */

/* cleanup.txt starts with "# game <dir>": the folder its lines are for. 1
 * when text names another folder than p's (a list without it is p's). */
static int cleanup_foreign(const JkPaths *p, const char *text)
{
    if (!text || strncmp(text, "# game ", 7)) return 0;
    size_t l = strcspn(text + 7, "\n");
    return strlen(p->game_dir) != l || strncmp(text + 7, p->game_dir, l);
}

static int add_cleanup(const JkPaths *p, const char *sha, const char *rel)
{
    char list[PATH], line[PATH + 80];
    if (cleanup_list(list, p) < 0 || jk_fs_mkdir_parent(list) < 0) return -1;
    size_t n = 0;
    char *old = jk_fs_read(list, 1 << 20, &n);
    if (cleanup_foreign(p, old)) { free(old); return 0; }      /* another folder's list: this sidecar just stays */
    if (!old || !n) {
        free(old);
        n = (size_t)snprintf(line, sizeof line, "# game %s\n", p->game_dir);
        old = malloc(n + 1);
        if (!old) return -1;
        memcpy(old, line, n + 1);
    }
    snprintf(line, sizeof line, "%s %s\n", sha, rel);
    if (strstr(old, line)) { free(old); return 0; }
    size_t ln = strlen(line);
    char *buf = malloc(n + ln + 1);
    if (!buf) { free(old); return -1; }
    if (old) memcpy(buf, old, n);
    memcpy(buf + n, line, ln);
    free(old);
    int r = jk_fs_write_atomic(list, buf, n + ln);
    free(buf);
    return r;
}

/* A sidecar <base>.* next to game/<rel> at the untouched size and hash. */
static int find_sidecar(const JkPaths *p, const JkMsFile *v, char out[PATH], char rel_out[PATH])
{
    char game[PATH], dir[PATH], h[65];
    if (jk_fs_join(game, sizeof game, p->game_dir, v->name) < 0) return -1;
    snprintf(dir, sizeof dir, "%s", game);
    char *slash = strrchr(dir, '/');
    if (!slash) return -1;
    *slash = 0;
    const char *base = slash + 1;
    size_t bl = strlen(base);
    const char *rslash = strrchr(v->name, '/');
    size_t rdir = rslash ? (size_t)(rslash - v->name) + 1 : 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = -1;
    while (d && found < 0 && (e = readdir(d))) {
        if (strlen(e->d_name) <= bl + 1 || strncasecmp(e->d_name, base, bl) || e->d_name[bl] != '.') continue;
        if (jk_fs_join(out, PATH, dir, e->d_name) < 0) continue;
        if (hash_of(out, v->size, h) == 0 && !strcmp(h, v->sha)) {
            snprintf(rel_out, PATH, "%.*s%s", (int)rdir, v->name, e->d_name);
            found = 0;
        }
    }
    if (d) closedir(d);
    return found;
}

int jk_modset_pristine_capture(const JkMsCatalog *c, const JkPaths *p, JkProgress cb, void *ctx, char *why, size_t cap)
{
    if (why && cap) why[0] = 0;
    if (!c || !p || !p->game_dir || !p->data_root || c->npaths > NP_MAX) return say(why, cap, JK_S_E_NO_GAME, NULL, NULL);
    pristine_load(c, p);
    static char src[NP_MAX][PATH], side[NP_MAX][PATH];
    int todo[NP_MAX], ntodo = 0, missing = 0, listed_new = 0;
    long long bytes = 0;
    char miss[600] = "";
    for (int i = 0; i < c->npaths; i++) {
        const JkMsPath *mp = &c->paths[i];
        if (!mp->vanilla || pr_ok[i]) continue;
        const JkMsFile *v = vanilla_of(c, mp->path);
        char h[65];
        int ok = 0;
        side[i][0] = 0;
        if (v && pristine_path(src[i], p, mp->path) == 0 && hash_of(src[i], v->size, h) == 0 && !strcmp(h, v->sha)) {
            pr_ok[i] = 1;                                      /* already in the store, not listed yet */
            listed_new = 1;
            continue;
        }
        if (v && jk_fs_join(src[i], PATH, p->game_dir, mp->path) == 0 && hash_of(src[i], v->size, h) == 0 && !strcmp(h, v->sha)) ok = 1;
        if (!ok && v && find_sidecar(p, v, src[i], side[i]) == 0) ok = 1;
        const char *legacy[2] = { "modset/original", "backup" };
        for (int k = 0; !ok && v && k < 2; k++)
            if (dr_path(src[i], p, "legacy-r3", legacy[k], mp->path) == 0 && hash_of(src[i], v->size, h) == 0 && !strcmp(h, v->sha)) ok = 1;
        if (!ok) {
            size_t l = strlen(miss);
            snprintf(miss + l, sizeof miss - l, "%s%s", missing ? ", " : "", mp->path);
            missing++;
            continue;
        }
        todo[ntodo++] = i;
        bytes += v->size;
    }
    long long free_b = ntodo ? jk_fs_free_bytes(p->data_root) : 0;
    long long need_b = bytes + (16LL << 20);
    if (ntodo && free_b >= 0 && free_b < need_b) {
        char a[32], b[32];
        snprintf(a, sizeof a, "%lld", (need_b + (1 << 20) - 1) >> 20);
        snprintf(b, sizeof b, "%lld", free_b >> 20);
        return say(why, cap, JK_S_E_PRISTINE_SPACE, a, b);
    }
    for (int k = 0; k < ntodo; k++) {
        int i = todo[k];
        const JkMsFile *v = vanilla_of(c, c->paths[i].path);
        char dst[PATH];
        if (cb) cb(ctx, k, ntodo, c->paths[i].path);
        if (pristine_path(dst, p, c->paths[i].path) < 0 || jk_fs_mkdir_parent(dst) < 0 || jk_fs_copy_checked(src[i], dst, v->sha) < 0) {
            pristine_save(c, p);
            return say(why, cap, JK_S_E_PRISTINE_KEEP, c->paths[i].path, NULL);
        }
        pr_ok[i] = 1;
        if (side[i][0]) add_cleanup(p, v->sha, side[i]);
    }
    if ((ntodo || listed_new) && pristine_save(c, p) < 0) return say(why, cap, JK_S_E_PRISTINE_LIST, NULL, NULL);
    if (missing) {
        say(why, cap, JK_S_E_NO_PRISTINE, miss, NULL);
        return -2;
    }
    if (ntodo && free_b < 0) say(why, cap, JK_S_E_SPACE_UNKNOWN, NULL, NULL);
    return 0;
}

int jk_modset_cleanup(const JkPaths *p, char *why, size_t cap)
{
    char list[PATH], line[PATH + 100];
    if (why && cap) why[0] = 0;
    if (!p || !p->game_dir || cleanup_list(list, p) < 0) return -1;
    {
        char *text = jk_fs_read(list, 1 << 20, NULL);
        int other = cleanup_foreign(p, text);
        free(text);
        if (other) return 0;                                      /* another game folder's sidecars */
    }
    FILE *f = fopen(list, "r");
    if (!f) return 0;
    static char keep[64 * 1024];
    size_t kn = (size_t)snprintf(keep, sizeof keep, "# game %s\n", p->game_dir), head = kn;
    int done = 0;
    while (fgets(line, sizeof line, f)) {
        char sha[65], rel[PATH], game[PATH], h[65];
        if (line[0] == '#') continue;
        if (sscanf(line, "%64s %1023[^\n]", sha, rel) != 2) continue;
        if (strstr(rel, "..") || rel[0] == '/' || jk_fs_join(game, sizeof game, p->game_dir, rel) < 0
            || jk_fs_safe_path(p->game_dir, rel) < 0) continue;
        hash_forget(game);
        if (hash_of(game, -1, h) < 0) continue;                   /* gone already */
        if (strcmp(h, sha)) continue;                             /* changed since: not ours to delete */
        if (jk_fs_remove(game) == 0) { done++; continue; }
        kn += (size_t)snprintf(keep + kn, sizeof keep - kn, "%s %s\n", sha, rel);   /* try again next time */
        if (kn >= sizeof keep) kn = sizeof keep - 1;
    }
    fclose(f);
    if (kn > head) jk_fs_write_atomic(list, keep, kn);
    else jk_fs_remove(list);
    return done;
}

/* ---- PATCH ---- */

static void join_problems(Ctx *x, char *why, size_t cap)
{
    if (!why || !cap) return;
    why[0] = 0;
    for (int i = 0; i < x->nprob && i < PROB_MAX; i++) {
        size_t l = strlen(why);
        if (l + 4 >= cap) break;
        snprintf(why + l, cap - l, "%s%s", i ? "; " : "", x->prob[i]);
    }
}

static JkMsScan apply_scan;

/* Restore: the first file kept aside at a managed path (backup/kept/<path>,
 * never .2+) goes back when the path ends absent or at the untouched file
 * (the store holds those bytes). cur[] follows. */
static void return_kept(const JkMsCatalog *c, const JkPaths *p)
{
    for (int i = 0; i < c->npaths && i < NP_MAX; i++) {
        const char *rel = c->paths[i].path;
        char kept[PATH], game[PATH];
        struct stat st;
        if (dr_path(kept, p, "backup", "kept", rel) < 0 || lstat(kept, &st) != 0 || !S_ISREG(st.st_mode)
            || jk_fs_join(game, sizeof game, p->game_dir, rel) < 0 || jk_fs_safe_path(p->game_dir, rel) < 0)
            continue;
        const JkMsFile *v = c->paths[i].vanilla ? vanilla_of(c, rel) : NULL;
        int at_pristine = cur[i].present && v && pr_ok[i] && !strcmp(cur[i].sha, v->sha);
        if (cur[i].present && !at_pristine) continue;
        if (cur[i].present && jk_fs_remove(game) < 0) continue;
        hash_forget(game);
        if (jk_fs_mkdir_parent(game) < 0 || jk_fs_rename(kept, game) < 0) continue;
        cur[i].present = 1;
        if (hash_of(game, -1, cur[i].sha) < 0 || (!file_by_sha(c, cur[i].sha) && !is_output(c, cur[i].sha))) cur[i].sha[0] = 0;
    }
}

static int apply_inner(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, int none_only,
                    JkProgress cb, void *ctx, JkMsScan *out, int *changed, char *why, size_t cap)
{
    if (changed) *changed = 0;
    if (why && cap) why[0] = 0;
    if (!c || !p || !p->game_dir || !p->data_root) return say(why, cap, JK_S_E_NO_GAME, NULL, NULL);
    hcache_n = 0;
    {
        int pr = prepare(c, p, why, cap);   /* a journal left by an earlier PATCH or Restore */
        if (pr < 0) return pr;
    }
    rm_sub(p, "stage");
    rm_sub(p, "src");
    sweep_pre_all(p);
    if (scan_internal(c, p, dirs, ndirs, none_only ? 0 : -1, &apply_scan) < 0) return say(why, cap, JK_S_E_NO_GAME, NULL, NULL);
    if (out) *out = apply_scan;
    Ctx *x = &X;
    int set = apply_scan.target;
    if (x->nprob) { join_problems(x, why, cap); return -1; }
    x->cb = cb;
    x->cbx = ctx;
    if (nacts == 0) {
        if (none_only) return_kept(c, p);
        record_write(c, p, set);
        if (none_only) rm_sub(p, "built");
        restore_mode = 0;                                /* the rows as a PATCH sees them */
        if (out) scan_internal(c, p, dirs, ndirs, -1, out);
        return 0;
    }
    long long free_b = jk_fs_free_bytes(p->data_root);
    long long need_b = x->space + (16LL << 20);
    if (free_b >= 0 && free_b < need_b) {
        char a[32], b[32];
        snprintf(a, sizeof a, "%lld", (need_b + (1 << 20) - 1) >> 20);
        snprintf(b, sizeof b, "%lld", free_b >> 20);
        return say(why, cap, JK_S_E_SPACE, a, b);
    }

    /* build in staging and the cache: nothing in the game folder changes yet */
    int rc = -1;
    x->steps = nacts * 2 + 2;
    jk_fs_tick_fn = mb_tick;
    jk_fs_tick_ctx = x;
    if (extract_all(x) < 0) goto fail;
    x->build = 1;
    plan(x, set);
    if (x->nprob) goto fail;
    for (int i = 0; i < nacts; i++) {
        Act *a = &acts[i];
        x->step++;
        progress(x, a->mp->path);
        if (a->src != S_PRISTINE) continue;
        char pp[PATH];
        if (pristine_path(pp, p, a->mp->path) < 0 || jk_fs_mkdir_parent(a->from) < 0 || jk_fs_copy_checked(pp, a->from, a->sha) < 0) {
            prob(x, JK_S_E_COPY_PRISTINE, a->mp->path, NULL);
            goto fail;
        }
    }
    /* check every output: its hash, and compressed data with the game's own counter */
    for (int i = 0; i < nacts; i++) {
        Act *a = &acts[i];
        x->step++;
        if (a->kind != A_INSTALL || !a->sha[0]) continue;
        char sha[65], jw[160], what[200];
        snprintf(what, sizeof what, JK_S_E_CHECKING, a->mp->path);
        progress(x, what);
        long long cnt = 0, off = -1;
        hash_forget(a->from);
        if (hash_of(a->from, -1, sha) < 0 || strcmp(sha, a->sha) || is_bad(c, sha)) {
            if (a->src == S_BUILT) jk_fs_remove(a->from);             /* a damaged cache entry: made again next time */
            prob(x, JK_S_E_NOT_RIGHT, a->mp->path, NULL);
            goto fail;
        }
        if (jk_jdlz_check_file(a->from, &cnt, &off, jw, sizeof jw) < 0) {
            prob(x, JK_S_E_JDLZ, a->mp->path, jw);
            goto fail;
        }
    }

    /* swap under the journal */
    progress(x, "Installing");
    if (write_journal(p, "swapping") < 0) { prob(x, JK_S_E_WRITE_ANY, NULL, NULL); goto fail; }
    int n = 0, renames = 0;
    char state[16];
    read_journal(p, state, &n);
    for (int i = 0; i < nacts; i++) {
        Act *a = &acts[i];
        char game[PATH], pre[PATH];
        jk_fs_join(game, sizeof game, p->game_dir, a->mp->path);
        ms_path(pre, p, "pre", a->mp->path);
        hash_forget(game);
        if (jk_fs_exists(game)) {
            if (jk_fs_rename(game, pre) < 0) goto swap_fail;
            if (test_fault && ++renames >= test_fault) { say(why, cap, "power cut (test)", NULL, NULL); return JK_MS_PENDING; }
        }
        if (a->kind != A_DELETE) {
            if (jk_fs_rename(a->from, game) < 0) goto swap_fail;
            if (test_fault && ++renames >= test_fault) { say(why, cap, "power cut (test)", NULL, NULL); return JK_MS_PENDING; }
        }
        if (changed) (*changed)++;
    }
    if (write_journal(p, "commit") < 0) goto swap_fail;
    for (int i = 0; i < nacts; i++) finish_pre(p, acts[i].mp->path, acts[i].disp, acts[i].old);
    {
        char jpath[PATH];
        ms_path(jpath, p, "journal.txt", NULL);
        jk_fs_remove(jpath);
    }
    for (int i = 0; i < nacts; i++) {
        Act *a = &acts[i];
        Cur *cu = &cur[a->idx];
        cu->present = a->kind != A_DELETE;
        snprintf(cu->sha, sizeof cu->sha, "%s", cu->present ? a->sha : "");
        if (cu->present && !a->sha[0]) {                              /* the player's own file, back */
            char game[PATH];
            if (jk_fs_join(game, sizeof game, p->game_dir, a->mp->path) == 0 && hash_of(game, -1, cu->sha) < 0) cu->sha[0] = 0;
            if (!file_by_sha(c, cu->sha) && !is_output(c, cu->sha)) cu->sha[0] = 0;
        }
    }
    if (none_only) return_kept(c, p);
    record_write(c, p, set);
    rm_sub(p, "stage");
    rm_sub(p, "src");
    if (none_only) rm_sub(p, "built");
    restore_mode = 0;                                /* the rows as a PATCH sees them */
    if (out) scan_internal(c, p, dirs, ndirs, -1, out);
    return 0;
swap_fail:
    if (rollback(p, n) == 0) {
        char jpath[PATH];
        ms_path(jpath, p, "journal.txt", NULL);
        jk_fs_remove(jpath);
        say(why, cap, JK_S_E_SWAP_BACK, NULL, NULL);
    } else {
        say(why, cap, JK_S_E_SWAP_NEXT, NULL, NULL);
        rc = JK_MS_PENDING;                            /* the journal stays: the next start rolls back */
    }
    if (changed) *changed = 0;
    rm_sub(p, "stage");
    rm_sub(p, "src");
    return rc;
fail:
    join_problems(x, why, cap);
    if (out) {
        out->problems = x->nprob;
        for (int i = 0; i < x->nprob && i < JK_MS_PROBLEMS; i++) snprintf(out->problem[i], sizeof out->problem[0], "%s", x->prob[i]);
    }
    rm_sub(p, "stage");
    rm_sub(p, "src");
    return rc;
}

int jk_modset_apply(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, int none_only,
                    JkProgress cb, void *ctx, JkMsScan *out, int *changed, char *why, size_t cap)
{
    restore_mode = none_only != 0;
    memset(&apply_scan, 0, sizeof apply_scan);
    int r = apply_inner(c, p, dirs, ndirs, none_only, cb, ctx, out, changed, why, cap);
    jk_fs_tick_fn = NULL;                            /* the MB ticks are for this PATCH only */
    jk_fs_tick_ctx = NULL;
    restore_mode = 0;
    if (out && none_only) {                          /* what Restore left as it is, for its result */
        out->nleft = apply_scan.nleft;
        memcpy(out->left, apply_scan.left, sizeof out->left);
    }
    return r;
}
