/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Package manifest parser and allow-list. See jk_manifest.h. */
#include "jk_manifest.h"
#include "jk_mods.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Non-game files the patcher may write, replace or move (spec 8.2). */
static const char *const TOP_NAMES[] = {
    "d3d9.dll", "dinput8.dll", "dsound.dll", "Xidi.32.dll", "Xidi.ini", "dxvk.conf", "jk4speed-dev.ini",
};
static const char *const SCRIPT_EXTS[] = { ".asi", ".cfg", ".ini", ".tpk" };

/* Owner decision D3 (2026-11-23), narrowed the same day: the HD road/HUD
 * files and the X360 Stuff Pack files may replace game files, but only from
 * the player's USB drop folder and only at their pinned hashes (jk_mods.c).
 * A package manifest may never carry them. */
const char *jk_manifest_hd_sha(const char *path)
{
    const JkModFile *f = jk_mods_find(path);
    return f ? f->sha : NULL;
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int iends(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);
    return n > k && ieq(s + n - k, suffix);
}

/* Relative, no empty or dot segments, no backslash, printable ASCII. */
static int safe_relative(const char *p)
{
    if (!p || !*p || *p == '/' || strlen(p) >= JK_PATH_MAX) return 0;
    for (const char *s = p; *s; s++)
        if (*s == '\\' || *s == ':' || (unsigned char)*s < 0x21 || (unsigned char)*s > 0x7E) return 0;
    const char *seg = p;
    for (;;) {
        const char *end = strchr(seg, '/');
        size_t len = end ? (size_t)(end - seg) : strlen(seg);
        if (len == 0 || (len == 1 && seg[0] == '.') || (len == 2 && seg[0] == '.' && seg[1] == '.')) return 0;
        if (!end) break;
        seg = end + 1;
    }
    return 1;
}

int jk_manifest_path_allowed(JkEntryKind kind, const char *path)
{
    if (!safe_relative(path)) return 0;
    switch (kind) {
    case JK_M_REQUIRE:
        return 1;
    case JK_M_EXE:
        return !strchr(path, '/') && iends(path, ".exe");
    case JK_M_SEED:
        return !strchr(path, '/') && iends(path, ".reg");
    case JK_M_SERVER:
        return !strcmp(path, "dxvknative.prx");
    case JK_M_PROFILE:
        return !strchr(path, '/') && iends(path, ".profile");
    case JK_M_ACCEPT:
        if (!strcmp(path, "dxvknative.prx")) return 1;     /* the server's load-segment digest */
        /* fall through */
    case JK_M_FILE:
        if (jk_manifest_hd_sha(path)) return 1;
        /* fall through */
    case JK_M_CONFIG:
    case JK_M_REMOVE: {
        for (size_t i = 0; i < sizeof TOP_NAMES / sizeof TOP_NAMES[0]; i++)
            if (ieq(path, TOP_NAMES[i])) return 1;
        if (strncmp(path, "scripts/", 8) && strncmp(path, "SCRIPTS/", 8) && strncmp(path, "Scripts/", 8)) return 0;
        const char *name = path + 8;
        if (!*name || strchr(name, '/')) return 0;
        for (size_t i = 0; i < sizeof SCRIPT_EXTS / sizeof SCRIPT_EXTS[0]; i++)
            if (iends(name, SCRIPT_EXTS[i])) return 1;
        return 0;
    }
    }
    return 0;
}

static int is_sha(const char *s)
{
    if (strlen(s) != 64) return 0;
    for (; *s; s++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return 0;
    return 1;
}

static int fail(JkManifest *m, int line, const char *why)
{
    snprintf(m->err, sizeof m->err, "line %d: %s", line, why);
    return -1;
}

int jk_manifest_parse(const char *text, JkManifest *m)
{
    if (!m) return -1;
    memset(m, 0, sizeof *m);
    if (!text) { snprintf(m->err, sizeof m->err, "no manifest"); return -1; }
    int line = 0, exes = 0;
    const char *p = text;
    while (*p) {
        line++;
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char buf[512];
        if (len >= sizeof buf) return fail(m, line, "line too long");
        memcpy(buf, p, len);
        buf[len] = 0;
        p = eol ? eol + 1 : p + len;
        if (len && buf[len - 1] == '\r') buf[--len] = 0;

        char *tok[5];
        int nt = 0;
        for (char *s = strtok(buf, " \t"); s && nt < 5; s = strtok(NULL, " \t")) tok[nt++] = s;
        if (nt == 0 || tok[0][0] == '#') continue;

        JkEntry e;
        memset(&e, 0, sizeof e);
        e.size = -1;
        int want;
        if (!strcmp(tok[0], "exe")) { e.kind = JK_M_EXE; want = 3; }
        else if (!strcmp(tok[0], "require")) { e.kind = JK_M_REQUIRE; want = 2; }
        else if (!strcmp(tok[0], "file")) { e.kind = JK_M_FILE; want = 4; }
        else if (!strcmp(tok[0], "config")) { e.kind = JK_M_CONFIG; want = 4; }
        else if (!strcmp(tok[0], "remove")) { e.kind = JK_M_REMOVE; want = 2; }
        else if (!strcmp(tok[0], "seed")) { e.kind = JK_M_SEED; want = 4; }
        else if (!strcmp(tok[0], "profile")) { e.kind = JK_M_PROFILE; want = 4; }
        else if (!strcmp(tok[0], "server")) { e.kind = JK_M_SERVER; want = 4; }
        else if (!strcmp(tok[0], "accept")) { e.kind = JK_M_ACCEPT; want = 3; }
        else return fail(m, line, "unknown kind");
        if (nt != want) return fail(m, line, "wrong number of fields");
        if (!jk_manifest_path_allowed(e.kind, tok[1])) return fail(m, line, "path not allowed");
        if (jk_mods_find(tok[1]) && e.kind != JK_M_REQUIRE)
            return fail(m, line, "a mod file comes from the player's mods folder, never from the package");
        if (e.kind == JK_M_ACCEPT) {
            int has_file = 0;
            for (int i = 0; i < m->n; i++)
                if ((m->e[i].kind == JK_M_FILE || m->e[i].kind == JK_M_SERVER) && !strcmp(m->e[i].path, tok[1])) has_file = 1;
            if (!has_file) return fail(m, line, "accept without a file line before it");
        }
        snprintf(e.path, sizeof e.path, "%s", tok[1]);
        if (want >= 3) {
            if (!is_sha(tok[2])) return fail(m, line, "bad sha256");
            memcpy(e.sha, tok[2], 65);
        }
        if (want == 4) {
            char *end;
            long long v = strtoll(tok[3], &end, 10);
            if (*end || v < 0) return fail(m, line, "bad size");
            e.size = v;
        }
        if (e.kind == JK_M_EXE && ++exes > 1) return fail(m, line, "more than one exe");
        if (m->n >= JK_MANIFEST_MAX) return fail(m, line, "too many entries");
        m->e[m->n++] = e;
    }
    if (exes != 1) { snprintf(m->err, sizeof m->err, "no exe line"); return -1; }
    return 0;
}

const JkEntry *jk_manifest_exe(const JkManifest *m)
{
    for (int i = 0; m && i < m->n; i++)
        if (m->e[i].kind == JK_M_EXE) return &m->e[i];
    return NULL;
}

int jk_manifest_accepts(const JkManifest *m, const char *path, const char *sha)
{
    if (!m || !path || !sha || !*sha) return 0;
    for (int i = 0; i < m->n; i++) {
        const JkEntry *e = &m->e[i];
        if (e->kind != JK_M_FILE && e->kind != JK_M_PROFILE && e->kind != JK_M_SEED && e->kind != JK_M_ACCEPT
            && e->kind != JK_M_SERVER) continue;
        if (!strcmp(e->path, path) && !strcmp(e->sha, sha)) return 1;
    }
    return 0;
}
