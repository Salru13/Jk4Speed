/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "jk_layout.h"
#include "jk_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define GAME_LEAF "NFSMW"

#define SET(field, base, sub) ok = ok && snprintf(l->field, sizeof l->field, "%s/%s", base, sub) < (int)sizeof l->field

void jk_layout_init(JkLayout *l, const char *root)
{
    if (!l) return;
    memset(l, 0, sizeof *l);
    if (!root || !root[0]) root = JK_PRODUCT_ROOT;
    int ok = snprintf(l->root, sizeof l->root, "%s", root) < (int)sizeof l->root;
    SET(game, root, GAME_LEAF);
    SET(saves, root, "Saves");
    SET(saves_game, l->saves, "NFS Most Wanted");
    SET(mods, root, "Mods");
    SET(logs, root, "Logs");
    SET(support, l->logs, "support");
    SET(devtools, root, "devtools");
    SET(system, root, "System");
    SET(prefix, l->system, "prefix");
    SET(backup, l->system, "backup");
    SET(pristine, l->backup, "pristine");
    SET(modset, l->system, "modset");
    SET(state, l->system, "state");
    SET(cache, l->system, "cache");
    SET(profiles, l->system, "profiles");
    if (!ok) memset(l, 0, sizeof *l);
}

int jk_layout_make(const JkLayout *l)
{
    if (!l || !l->root[0]) return -1;
    const char *dirs[] = { l->root, l->saves, l->mods, l->logs, l->support, l->devtools, l->system, l->prefix,
                           l->backup, l->pristine, l->modset, l->state, l->cache, l->profiles };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        if (jk_fs_mkdir_p(dirs[i]) != 0) return -1;
    /* r5: open to every user (FTP drops into Mods/Saves; console 2026-11-24
     * FTP got 550 on 0755), also when an earlier launcher made them 0755 */
    const char *open_dirs[] = { l->root, l->saves, l->mods, l->logs, l->system };
    for (size_t i = 0; i < sizeof open_dirs / sizeof open_dirs[0]; i++) chmod(open_dirs[i], 0777);
    return 0;
}

/* Up to two matches of name (letter case ignored) among the folders in dir,
 * in strcmp order. Returns how many there are (2 means "two or more"). */
static int find_ci(const char *dir, const char *name, char m[2][256])
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, name) || strlen(e->d_name) >= 256) continue;
        char p[1024];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p || !jk_fs_is_dir(p)) continue;
        if (n < 2) snprintf(m[n++], 256, "%s", e->d_name);
        else if (strcmp(e->d_name, m[1]) < 0) snprintf(m[1], 256, "%s", e->d_name);
        if (n == 2 && strcmp(m[0], m[1]) > 0) { char t[256]; memcpy(t, m[0], 256); memcpy(m[0], m[1], 256); memcpy(m[1], t, 256); }
    }
    closedir(d);
    return n;
}

int jk_game_dir_find(const char *root, char *out, size_t cap, char *other, size_t ocap)
{
    if (other && ocap) other[0] = 0;
    if (!out || !cap) return 0;
    snprintf(out, cap, "%s/%s", root ? root : "", GAME_LEAF);
    if (!root) return 0;
    char m[2][256];
    int n = find_ci(root, GAME_LEAF, m);
    if (n == 0) return 0;
    snprintf(out, cap, "%s/%s", root, m[0]);
    if (n == 1) return 1;
    if (other && ocap) snprintf(other, ocap, "%s/%s", root, m[1]);
    return -1;
}

static void say(JkLayoutLog log, void *ctx, const char *fmt, const char *a, const char *b)
{
    if (!log) return;
    char line[2000];
    snprintf(line, sizeof line, fmt, a, b ? b : "");
    log(ctx, line);
}

static int entry_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

/* 1 when dir holds an entry spelled exactly name. On a volume that ignores
 * letter case, lstat("<root>/logs") also finds r4's own "Logs": only r3's
 * lower-case spelling is moved aside (Task 8: the title's session logs live in
 * /data/Jk4Speed/Logs). */
static int entry_named(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) found = !strcmp(e->d_name, name);
    closedir(d);
    return found;
}

int jk_layout_migrate_r3(const JkLayout *l, JkLayoutLog log, void *ctx)
{
    static const char *const NAMES[] = { "logs", "modset", "state", "backup", "registry", "support" };
    if (!l || !l->root[0]) return -1;
    int moved = 0, failed = 0;
    char legacy[160];
    if (snprintf(legacy, sizeof legacy, "%s/legacy-r3", l->system) >= (int)sizeof legacy) return -1;
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        char from[160], to[320], alt[340];
        if (snprintf(from, sizeof from, "%s/%s", l->root, NAMES[i]) >= (int)sizeof from) continue;
        if (!entry_exists(from) || !entry_named(l->root, NAMES[i])) continue;
        if (snprintf(to, sizeof to, "%s/%s", legacy, NAMES[i]) >= (int)sizeof to || jk_fs_mkdir_p(legacy) != 0) {
            failed++;
            say(log, ctx, "migrate: %s not moved: cannot make the legacy-r3 folder", from, NULL);
            continue;
        }
        const char *dest = to;
        if (entry_exists(to)) {
            dest = NULL;
            for (int k = 1; k <= 9 && !dest; k++) {
                if (k == 1) snprintf(alt, sizeof alt, "%s.old-r3", to);
                else snprintf(alt, sizeof alt, "%s.old-r3%d", to, k);
                if (!entry_exists(alt)) dest = alt;
            }
            if (!dest) { failed++; say(log, ctx, "migrate: %s not moved: %s and its .old-r3 names exist", from, to); continue; }
        }
        if (rename(from, dest) != 0) { failed++; say(log, ctx, "migrate: %s not moved to %s", from, dest); continue; }
        moved++;
        if (dest == to) say(log, ctx, "migrate: r3 %s moved aside to %s", from, to);
        else {
            char line[2000];
            snprintf(line, sizeof line, "migrate: kept both: %s exists, the r3 entry %s is now %s", to, from, dest);
            if (log) log(ctx, line);
        }
    }
    return failed ? -1 : moved;
}
