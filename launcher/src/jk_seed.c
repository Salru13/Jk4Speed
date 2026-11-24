/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The Wine prefix seed. See jk_seed.h. */
#include "jk_seed.h"
#include "jk_fs.h"
#include "jk_sha256.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define INDEX_MAX (4u * 1024u * 1024u)
#define MARGIN (16LL << 20)               /* room left over after the copy */

typedef struct Entry {
    char kind;                            /* 'd', 'l', 'f' */
    const char *rel, *target, *sha, *src; /* src: "obj" or "app:<path>" */
    long long size;
} Entry;

typedef struct Index {
    char *text;                           /* the parsed copy: entries point into it */
    Entry *e;
    int n;
    char sha[65];
} Index;

static void say(char *why, size_t cap, const char *fmt, const char *a)
{
    if (why && cap) snprintf(why, cap, fmt, a ? a : "");
}

static int is_hex64(const char *s)
{
    if (!s || strlen(s) != 64) return 0;
    for (const char *p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return 0;
    return 1;
}

/* Relative, inside the prefix: no leading '/', no '.' or '..' segment, no
 * backslash, no control byte. */
static int safe_rel(const char *p)
{
    if (!p || !*p || *p == '/' || strlen(p) >= 512) return 0;
    for (const char *s = p; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == '\\') return 0;
    const char *seg = p;
    for (;;) {
        const char *end = strchr(seg, '/');
        size_t n = end ? (size_t)(end - seg) : strlen(seg);
        if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) return 0;
        if (!end) return 1;
        seg = end + 1;
    }
}

static void index_free(Index *ix)
{
    free(ix->text);
    free(ix->e);
    memset(ix, 0, sizeof *ix);
}

/* Last space in s (for "<relpath with spaces> <source>"). */
static char *last_space(char *s)
{
    char *r = NULL;
    for (; *s; s++)
        if (*s == ' ') r = s;
    return r;
}

static int index_load(const char *seed_dir, Index *ix, char *why, size_t cap)
{
    char path[1024];
    size_t len = 0;
    memset(ix, 0, sizeof *ix);
    if (!seed_dir || jk_fs_join(path, sizeof path, seed_dir, JK_SEED_INDEX) < 0) { say(why, cap, "no seed in the package", NULL); return -1; }
    ix->text = jk_fs_read(path, INDEX_MAX, &len);
    if (!ix->text) { say(why, cap, "the package has no prefix seed", NULL); return -1; }
    jk_sha256_hex(ix->text, len, ix->sha);
    int lines = 0;
    for (size_t i = 0; i < len; i++) lines += ix->text[i] == '\n';
    ix->e = calloc((size_t)lines + 1, sizeof *ix->e);
    if (!ix->e) { say(why, cap, "out of memory", NULL); index_free(ix); return -1; }
    char *line = ix->text, *next;
    int first = 1, ln = 0;
    for (; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ln++;
        size_t l = strlen(line);
        if (l && line[l - 1] == '\r') line[--l] = 0;
        if (first) {
            if (strcmp(line, "JKSEED 1")) { say(why, cap, "the prefix seed index has an unknown format", NULL); index_free(ix); return -1; }
            first = 0;
            continue;
        }
        if (!l) continue;
        Entry *e = &ix->e[ix->n];
        char *rest = line + 2;
        int ok = l > 2 && line[1] == ' ';
        e->kind = line[0];
        if (ok && e->kind == 'd') {
            e->rel = rest;
        } else if (ok && e->kind == 'l') {
            char *sp = strchr(rest, ' ');
            ok = sp != NULL && sp != rest;
            if (ok) { *sp = 0; e->target = rest; e->rel = sp + 1; }
        } else if (ok && e->kind == 'f') {
            char *sp1 = strchr(rest, ' '), *sp2 = sp1 ? strchr(sp1 + 1, ' ') : NULL, *last = sp2 ? last_space(sp2 + 1) : NULL;
            ok = last != NULL;
            if (ok) {
                *sp1 = *sp2 = *last = 0;
                e->sha = rest;
                char *end = NULL;
                errno = 0;
                e->size = strtoll(sp1 + 1, &end, 10);
                e->rel = sp2 + 1;
                e->src = last + 1;
                ok = is_hex64(e->sha) && end && !*end && errno == 0 && e->size >= 0 &&
                     (!strcmp(e->src, "obj") || (!strncmp(e->src, "app:", 4) && safe_rel(e->src + 4)));
            }
        } else {
            ok = 0;
        }
        if (!ok || !safe_rel(e->rel)) {
            char n[16];
            snprintf(n, sizeof n, "%d", ln);
            say(why, cap, "the prefix seed index is damaged (line %s)", n);
            index_free(ix);
            return -1;
        }
        ix->n++;
    }
    if (first) { say(why, cap, "the prefix seed index is empty", NULL); index_free(ix); return -1; }
    return 0;
}

/* 1 when dir/.jkseed-complete is exactly "<sha>\n". */
static int marker_matches(const char *dir, const char *sha)
{
    char path[1024];
    size_t len = 0;
    if (jk_fs_join(path, sizeof path, dir, JK_SEED_MARKER) < 0) return 0;
    char *t = jk_fs_read(path, 256, &len);
    int ok = t && len == 65 && !memcmp(t, sha, 64) && t[64] == '\n';
    free(t);
    return ok;
}

/* 1 when dir/.jkseed-complete is a marker of ours ("<64 hex>\n"), any index;
 * its sha goes to out. */
static int own_marker(const char *dir, char out[65])
{
    char path[1024];
    size_t len = 0;
    if (jk_fs_join(path, sizeof path, dir, JK_SEED_MARKER) < 0) return 0;
    char *t = jk_fs_read(path, 256, &len);
    int ok = t && len == 65 && t[64] == '\n';
    if (ok) {
        t[64] = 0;
        ok = is_hex64(t);
        if (ok) memcpy(out, t, 65);
    }
    free(t);
    return ok;
}

int jk_seed_state(const char *seed_dir, const char *prefix_dir)
{
    char path[1024];
    if (!seed_dir || jk_fs_join(path, sizeof path, seed_dir, JK_SEED_INDEX) < 0 || !jk_fs_exists(path)) return -1;
    size_t len = 0;
    char *t = jk_fs_read(path, INDEX_MAX, &len), sha[65];
    if (!t) return 0;
    jk_sha256_hex(t, len, sha);
    free(t);
    return prefix_dir && jk_fs_is_dir(prefix_dir) && marker_matches(prefix_dir, sha) ? 1 : 0;
}

/* The file's source: seed/objects/<sha> or the first app root that has it. */
static int source_of(const Entry *e, const char *seed_dir, const char *const *roots, int nroots, char *out, size_t cap)
{
    char rel[100];
    if (!strcmp(e->src, "obj")) {
        snprintf(rel, sizeof rel, "objects/%s", e->sha);
        return jk_fs_join(out, cap, seed_dir, rel);
    }
    for (int i = 0; i < nroots; i++) {
        if (!roots || !roots[i]) continue;
        if (jk_fs_join(out, cap, roots[i], e->src + 4) == 0 && jk_fs_exists(out)) return 0;
    }
    return -1;
}

/* 1 when path is a regular file of the size and hash pinned. */
static int file_matches(const char *path, const Entry *e)
{
    struct stat st;
    char got[65];
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || (long long)st.st_size != e->size) return 0;
    return jk_fs_sha256(path, got, NULL) == 0 && !strcmp(got, e->sha);
}

static int no_link_inside(const char *root, const char *rel);

static int make_link(const char *root, const Entry *e)
{
    char path[1024], have[512], parent[512];
    if (jk_fs_join(path, sizeof path, root, e->rel) < 0) return -1;
    snprintf(parent, sizeof parent, "%s", e->rel);
    char *slash = strrchr(parent, '/');
    if (slash && (*slash = 0, no_link_inside(root, parent) < 0)) return -1;   /* never through another link */
    ssize_t n = readlink(path, have, sizeof have - 1);
    if (n >= 0) {
        have[n] = 0;
        if (!strcmp(have, e->target)) return 0;
    }
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return -1;          /* a folder where the link goes: refused, never emptied */
        if (unlink(path) != 0) return -1;
    }
    if (jk_fs_mkdir_parent(path) < 0) return -1;
    return symlink(e->target, path);
}

/* 0 when no existing component of root/rel is a link and every existing
 * component but the last is a folder (the last may be anything: the caller
 * decides). Keeps a write from leaving the prefix through z: -> /. */
static int no_link_inside(const char *root, const char *rel)
{
    char path[1024];
    if (jk_fs_join(path, sizeof path, root, rel) < 0) return -1;
    size_t base = strlen(root);
    for (char *s = path + base + 1;; s++) {
        if (*s != '/' && *s) continue;
        char keep = *s;
        *s = 0;
        struct stat st;
        int r = lstat(path, &st);
        *s = keep;
        if (r != 0) return errno == ENOENT ? 0 : -1;
        if (S_ISLNK(st.st_mode)) return -1;
        if (!keep) return 0;
        if (!S_ISDIR(st.st_mode)) return -1;
    }
}

static void parent_of(const char *path, char *out, size_t cap)
{
    snprintf(out, cap, "%s", path);
    char *s = strrchr(out, '/');
    if (s && s != out) *s = 0;
}

int jk_seed_import(const char *seed_dir, const char *const *app_roots, int nroots, const char *prefix_dir,
                   JkProgress cb, void *ctx, char *why, size_t cap)
{
    char dummy[8], staging[1024], path[1024], src[1024], part[1100];
    if (!why || !cap) { why = dummy; cap = sizeof dummy; }
    why[0] = 0;
    if (!seed_dir || !prefix_dir || prefix_dir[0] != '/' || nroots < 0) { say(why, cap, "bad arguments", NULL); return -1; }
    Index ix;
    if (index_load(seed_dir, &ix, why, cap) < 0) return -1;
    int rc = -1;

    /* 1. Our marker: present. A prefix without it is someone else's, except
     *    an empty folder (jk_layout_make makes System/prefix): rmdir only
     *    succeeds on an empty one. */
    if (jk_fs_exists(prefix_dir)) {
        if (jk_fs_is_dir(prefix_dir) && marker_matches(prefix_dir, ix.sha)) { rc = 1; goto done; }
        if (jk_fs_is_dir(prefix_dir) && rmdir(prefix_dir) == 0) goto fresh;
        /* An older seed of ours (a well-formed marker of another index): moved
         * aside to <prefix>.old-<8 hex>[.N], never deleted (lead ruling; the
         * saves live outside the prefix), then a fresh import. */
        char old[65];
        if (jk_fs_is_dir(prefix_dir) && own_marker(prefix_dir, old)) {
            char aside[1100];
            int k;
            for (k = 1; k < 100; k++) {
                if (k == 1) snprintf(aside, sizeof aside, "%s.old-%.8s", prefix_dir, old);
                else snprintf(aside, sizeof aside, "%s.old-%.8s.%d", prefix_dir, old, k);
                if (!jk_fs_exists(aside)) break;
            }
            if (k == 100 || jk_fs_rename(prefix_dir, aside) < 0) {
                say(why, cap, "could not move the older Wine prefix aside; it was left untouched", NULL);
                goto done;
            }
            const char *base = strrchr(aside, '/');
            say(why, cap, "the older Wine prefix was moved aside to %s", base ? base + 1 : aside);
            goto fresh;
        }
        say(why, cap, "a Wine prefix folder is already there and was not made by this package; it was left untouched", NULL);
        rc = -2;
        goto done;
    }

fresh:
    /* 2. Room for what is still to copy (stat only; unknown free space goes on). */
    if (snprintf(staging, sizeof staging, "%s.staging", prefix_dir) >= (int)sizeof staging) { say(why, cap, "path too long", NULL); goto done; }
    if (jk_fs_exists(staging) && !jk_fs_is_dir(staging)) { say(why, cap, "the prefix staging entry is not a folder", NULL); rc = -2; goto done; }
    {
        long long need = 0;
        for (int i = 0; i < ix.n; i++) {
            if (ix.e[i].kind != 'f') continue;
            if (jk_fs_join(path, sizeof path, staging, ix.e[i].rel) < 0) continue;
            if (jk_fs_size(path) != ix.e[i].size) need += ix.e[i].size;
        }
        char parent[1024];
        parent_of(prefix_dir, parent, sizeof parent);
        long long free_b = jk_fs_free_bytes(parent);
        if (free_b >= 0 && need > 0 && free_b < need + MARGIN) {
            char n[96];
            snprintf(n, sizeof n, "%lld MB, %lld MB free", (need + MARGIN + (1 << 20) - 1) >> 20, free_b >> 20);
            say(why, cap, "not enough space for the Wine prefix: it needs %s", n);
            goto done;
        }
    }

    /* 3. Build in staging: folders, links, then every file checked. */
    if (jk_fs_mkdir_p(staging) < 0) { say(why, cap, "could not make the prefix staging folder", NULL); goto done; }
    int files = 0, done_n = 0;
    for (int i = 0; i < ix.n; i++) files += ix.e[i].kind == 'f';
    for (int i = 0; i < ix.n; i++) {
        const Entry *e = &ix.e[i];
        if (e->kind != 'd') continue;
        if (no_link_inside(staging, e->rel) == 0 && jk_fs_join(path, sizeof path, staging, e->rel) == 0 &&
            (jk_fs_is_dir(path) || jk_fs_mkdir_p(path) == 0))
            continue;
        say(why, cap, "could not make the prefix folder %s", e->rel);
        goto done;
    }
    for (int i = 0; i < ix.n; i++) {
        const Entry *e = &ix.e[i];
        if (e->kind != 'l') continue;
        if (make_link(staging, e) < 0) { say(why, cap, "could not make the prefix link %s", e->rel); goto done; }
    }
    for (int i = 0; i < ix.n; i++) {
        const Entry *e = &ix.e[i];
        if (e->kind != 'f') continue;
        if (cb) cb(ctx, done_n, files, e->rel);
        if (no_link_inside(staging, e->rel) < 0 || jk_fs_join(path, sizeof path, staging, e->rel) < 0) {
            say(why, cap, "the prefix seed names a file through a link: %s", e->rel);
            goto done;
        }
        if (!file_matches(path, e)) {
            if (source_of(e, seed_dir, app_roots, nroots, src, sizeof src) < 0 || !jk_fs_exists(src)) {
                say(why, cap, "the package is missing a prefix file (%s); reinstall JK4SPEED", e->rel);
                goto done;
            }
            snprintf(part, sizeof part, "%s.jkseed-part", path);
            if (jk_fs_copy_checked(src, part, e->sha) < 0) {
                (void)unlink(part);
                say(why, cap, "a prefix file in the package is damaged or could not be copied (%s)", e->rel);
                goto done;
            }
            if (jk_fs_rename(part, path) < 0) { (void)unlink(part); say(why, cap, "could not place the prefix file %s", e->rel); goto done; }
        }
        done_n++;
    }
    if (cb) cb(ctx, files, files, "");

    /* 4. Every file checked: the marker last, then the rename into place. */
    {
        char text[66];
        snprintf(text, sizeof text, "%s\n", ix.sha);
        if (jk_fs_join(path, sizeof path, staging, JK_SEED_MARKER) < 0 || jk_fs_write_atomic(path, text, 65) < 0) {
            say(why, cap, "could not write the prefix marker", NULL);
            goto done;
        }
    }
    if (jk_fs_rename(staging, prefix_dir) < 0) { say(why, cap, "could not move the finished prefix into place", NULL); goto done; }
    rc = 0;
done:
    index_free(&ix);
    return rc;
}
