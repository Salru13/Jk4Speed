/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The patcher. See jk_patcher.h and spec section 8. */
#include "jk_patcher.h"
#include "jk_config.h"
#include "jk_fs.h"
#include "jk_manifest.h"
#include "jk_elfdigest.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_MAX (64 * 1024)
#define FREE_MARGIN (16LL * 1024 * 1024)
#define RECORD_HEADER "# Jk4Speed install record v1: how kind sha path\n"

/* ------------------------------------------------------------------ */
/* Install record: <data_root>/state/installed.txt                     */
/* ------------------------------------------------------------------ */

typedef struct Rec { char how[8], kind[8], sha[65], path[JK_PATH_MAX]; } Rec;
typedef struct Record { Rec r[JK_MANIFEST_MAX]; int n, exists, bad; char game[1024]; } Record;

static const char *kind_name(JkEntryKind k)
{
    switch (k) {
    case JK_M_FILE: return "file";
    case JK_M_CONFIG: return "config";
    case JK_M_REMOVE: return "remove";
    case JK_M_SEED: return "seed";
    case JK_M_PROFILE: return "profile";
    case JK_M_SERVER: return "server";
    default: return "other";
    }
}

static int record_path(const JkPaths *p, char *out, size_t cap)
{
    return jk_fs_join(out, cap, p->data_root, "state/installed.txt");
}

/* A line we wrote: known how and kind, a sha256 or "-". */
static int rec_valid(const Rec *r)
{
    static const char *const HOW[] = { "new", "backup", "removed", "kept" };
    static const char *const KIND[] = { "file", "config", "remove", "seed", "profile", "server" };
    int how = 0, kind = 0;
    for (int i = 0; i < 4; i++) how |= !strcmp(r->how, HOW[i]);
    for (int i = 0; i < 6; i++) kind |= !strcmp(r->kind, KIND[i]);
    if (!how || !kind) return 0;
    if (!strcmp(r->sha, "-")) return 1;
    if (strlen(r->sha) != 64) return 0;
    for (const char *s = r->sha; *s; s++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) return 0;
    return 1;
}

static void record_load(const JkPaths *p, Record *rec)
{
    memset(rec, 0, sizeof *rec);
    char path[1024];
    if (record_path(p, path, sizeof path) < 0) return;
    char *text = jk_fs_read(path, MANIFEST_MAX, NULL);
    if (!text) return;
    rec->exists = 1;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "# game ", 7)) {                   /* the game folder this record is for */
            snprintf(rec->game, sizeof rec->game, "%s", line + 7);
            rec->game[strcspn(rec->game, "\r")] = 0;
            continue;
        }
        if (line[0] == '#' || line[0] == '\r') continue;
        Rec r;
        memset(&r, 0, sizeof r);
        if (rec->n >= JK_MANIFEST_MAX || sscanf(line, "%7s %7s %64s %159s", r.how, r.kind, r.sha, r.path) != 4
            || !rec_valid(&r)) {
            rec->bad = 1;
            continue;
        }
        rec->r[rec->n++] = r;
    }
    free(text);
    if (!rec->n) rec->bad = 1;     /* no entries: damaged; restore falls back to backup/ */
}

static int record_save(const JkPaths *p, const Record *rec)
{
    char path[1024];
    if (record_path(p, path, sizeof path) < 0 || jk_fs_mkdir_parent(path) < 0) return -1;
    size_t cap = sizeof RECORD_HEADER + 1100 + (size_t)rec->n * (8 + 8 + 65 + JK_PATH_MAX + 4);
    char *buf = malloc(cap);
    if (!buf) return -1;
    size_t n = (size_t)snprintf(buf, cap, "%s# game %s\n", RECORD_HEADER, p->game_dir ? p->game_dir : "");
    for (int i = 0; i < rec->n; i++)
        n += (size_t)snprintf(buf + n, cap - n, "%s %s %s %s\n", rec->r[i].how, rec->r[i].kind, rec->r[i].sha, rec->r[i].path);
    int r = jk_fs_write_atomic(path, buf, n);
    free(buf);
    return r;
}

/* r4: the launcher composes these two itself (jk_config.h), so whatever file
 * is there under these names is ours, at any hash. */
#define COMPOSED_INI "jk4speed-dev.ini"
/* 1.1: the composed profile is <profile_key>.profile (default nfsmw). */
static void composed_profile_name(const JkPaths *p, char *out, size_t cap)
{
    snprintf(out, cap, "%s.profile", p->profile_key && p->profile_key[0] ? p->profile_key : "nfsmw");
}

static Rec *record_find(Record *rec, JkEntryKind kind, const char *path)
{
    for (int i = 0; i < rec->n; i++)
        if (!strcmp(rec->r[i].kind, kind_name(kind)) && !strcmp(rec->r[i].path, path)) return &rec->r[i];
    return NULL;
}

static Rec *record_add(Record *rec, const char *how, JkEntryKind kind, const char *sha, const char *path)
{
    if (rec->n >= JK_MANIFEST_MAX) return NULL;
    Rec *r = &rec->r[rec->n++];
    snprintf(r->how, sizeof r->how, "%s", how);
    snprintf(r->kind, sizeof r->kind, "%s", kind_name(kind));
    snprintf(r->sha, sizeof r->sha, "%s", sha && *sha ? sha : "-");
    snprintf(r->path, sizeof r->path, "%s", path);
    return r;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int load_manifest(const JkPaths *p, JkManifest *m, char *reason, size_t cap)
{
    char path[1024];
    if (!p || !p->files_dir || jk_fs_join(path, sizeof path, p->files_dir, "manifest.txt") < 0) {
        snprintf(reason, cap, "the package has no manifest");
        return -1;
    }
    char *text = jk_fs_read(path, MANIFEST_MAX, NULL);
    if (!text) { snprintf(reason, cap, "the package has no manifest"); return -1; }
    int r = jk_manifest_parse(text, m);
    free(text);
    if (r < 0) { snprintf(reason, cap, "package manifest %s", m->err); return -1; }
    return 0;
}

/* Where an entry lives: the game folder, <data_root>/registry for seeds, the
 * profiles folder for profiles. */
static int target_of(const JkPaths *p, const JkEntry *e, char *out, size_t cap)
{
    if (e->kind == JK_M_SEED) {
        char reg[1024];
        if (jk_fs_join(reg, sizeof reg, p->data_root, "registry") < 0) return -1;
        return jk_fs_join(out, cap, reg, e->path);
    }
    if (e->kind == JK_M_PROFILE) return p->profiles_dir ? jk_fs_join(out, cap, p->profiles_dir, e->path) : -1;
    if (e->kind == JK_M_SERVER) return p->override_dir ? jk_fs_join(out, cap, p->override_dir, e->path) : -1;
    return jk_fs_join(out, cap, p->game_dir, e->path);
}

/* The folder a target's path is checked against for links. */
static const char *base_of(const JkPaths *p, JkEntryKind kind)
{
    return kind == JK_M_PROFILE ? p->profiles_dir : kind == JK_M_SERVER ? p->override_dir : p->game_dir;
}

/* The name under backup/: profiles go to backup/profiles/<name>. */
static void backup_rel(JkEntryKind kind, const char *path, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s", kind == JK_M_PROFILE ? "profiles/" : kind == JK_M_SERVER ? "override/" : "", path);
}

static int backup_of(const JkPaths *p, const char *rel, char *out, size_t cap)
{
    char bk[1024];
    if (jk_fs_join(bk, sizeof bk, p->data_root, "backup") < 0) return -1;
    return jk_fs_join(out, cap, bk, rel);
}

/* An entry that puts a file of ours somewhere (hash-checked). */
static int is_ours_kind(JkEntryKind k)
{
    return k == JK_M_FILE || k == JK_M_SEED || k == JK_M_PROFILE || k == JK_M_SERVER;
}

/* sha of path, "" when missing. */
static void sha_or_empty(const char *path, char out[65])
{
    if (jk_fs_sha256(path, out, NULL) < 0) out[0] = 0;
}

/* The file at tgt (sha cur) is one the package accepts. The server also by
 * its load-segment digest (jk_elfdigest.h): the same module raw, unwrapped by
 * FTP, or as the .oelf. Anything else is not ours: backed up, then replaced. */
static int accepted(const JkManifest *m, JkEntryKind kind, const char *rel, const char *tgt, const char *cur)
{
    if (!strcmp(rel, COMPOSED_INI) && kind == JK_M_FILE) return cur && cur[0];
    if (!strcmp(rel, "Xidi.ini") && kind == JK_M_FILE) return cur && cur[0];   /* composed from CONTROLS at PLAY */
    if (jk_manifest_accepts(m, rel, cur)) return 1;
    char d[65];
    return kind == JK_M_SERVER && cur && cur[0] && jk_load_digest(tgt, d) == 0 && jk_manifest_accepts(m, rel, d);
}

/* I-3: on a first install (no install record yet) an existing Xidi.ini that is not the package's is
 * the player's: backed up like any replaced file, and put back on Restore. */
static int players_xidi(const JkEntry *e, int first, const char *cur)
{
    return first && e->kind == JK_M_FILE && !strcmp(e->path, "Xidi.ini") && cur && cur[0] && strcmp(cur, e->sha) != 0;
}

/* ------------------------------------------------------------------ */
/* Check                                                              */
/* ------------------------------------------------------------------ */

/* The record names another game folder than p's (a record without the
 * "# game" line counts as p's). */
static int rec_foreign(const JkPaths *p, const Record *rec)
{
    return rec->exists && rec->game[0] && p && p->game_dir && strcmp(rec->game, p->game_dir) != 0;
}

int jk_patch_has_record(const JkPaths *p)
{
    static Record rec;
    if (!p || !p->data_root || !p->game_dir) return 0;
    record_load(p, &rec);
    return rec.exists && !rec_foreign(p, &rec);
}

/* from -> <data_root>/backup/kept/<rel>, or <rel>.2, .3 ... (never overwritten). 0 ok. */
static int keep_aside(const JkPaths *p, const char *from, const char *rel)
{
    char kept[1024], to[1200];
    if (backup_of(p, "kept", kept, sizeof kept) < 0 || jk_fs_join(to, sizeof to, kept, rel) < 0 || jk_fs_mkdir_parent(to) < 0)
        return -1;
    for (int k = 1; k < 1000; k++) {
        char alt[1300];
        if (k == 1) snprintf(alt, sizeof alt, "%s", to);
        else snprintf(alt, sizeof alt, "%s.%d", to, k);
        if (!jk_fs_exists(alt)) return jk_fs_rename(from, alt);
    }
    return -1;
}

/* Before an install: a record written for another game folder. While that
 * folder exists the install is refused (its backups must not be replaced);
 * when it is gone, the record and every backup it names are kept aside in
 * backup/kept and this folder starts with no record. 0 go on. */
static int foreign_gate(const JkPaths *p, Record *rec, char *reason, size_t cap)
{
    if (!rec_foreign(p, rec)) return 0;
    if (jk_fs_is_dir(rec->game)) {
        snprintf(reason, cap, "Jk4Speed's backup belongs to another game folder (%.200s): open Jk4Speed with that folder and restore it first",
                 rec->game);
        return -1;
    }
    for (int i = 0; i < rec->n; i++) {
        const Rec *r = &rec->r[i];
        if (strcmp(r->how, "backup") && strcmp(r->how, "removed")) continue;
        JkEntryKind k = !strcmp(r->kind, "profile") ? JK_M_PROFILE : !strcmp(r->kind, "server") ? JK_M_SERVER : JK_M_FILE;
        char rel[JK_PATH_MAX + 16], bk[1024];
        for (int c = 0; c < 2; c++) {
            backup_rel(k, r->path, rel, sizeof rel);
            if (c) strncat(rel, ".changed", sizeof rel - strlen(rel) - 1);
            if (backup_of(p, rel, bk, sizeof bk) == 0 && jk_fs_exists(bk) && keep_aside(p, bk, rel) < 0) {
                snprintf(reason, cap, "could not keep the old backup of %s", r->path);
                return -1;
            }
        }
    }
    char recp[1024];
    if (record_path(p, recp, sizeof recp) == 0 && keep_aside(p, recp, "installed-other-folder.txt") < 0) {
        snprintf(reason, cap, "could not keep the old install record");
        return -1;
    }
    memset(rec, 0, sizeof *rec);
    return 0;
}

/* Reads little-endian fields of a PE header straight from the file. */
int jk_exe_is_known_v13(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    unsigned char dos[64], nt[96];
    int ok = 0;
    if (fread(dos, 1, sizeof dos, f) == sizeof dos && dos[0] == 'M' && dos[1] == 'Z') {
        unsigned long off = (unsigned long)dos[60] | (unsigned long)dos[61] << 8 | (unsigned long)dos[62] << 16 | (unsigned long)dos[63] << 24;
        if (off < 0x100000 && fseek(f, (long)off, SEEK_SET) == 0 && fread(nt, 1, sizeof nt, f) == sizeof nt
            && !memcmp(nt, "PE\0\0", 4) && nt[24] == 0x0b && nt[25] == 0x01) {   /* PE32 optional header */
            unsigned long stamp = (unsigned long)nt[8] | (unsigned long)nt[9] << 8 | (unsigned long)nt[10] << 16 | (unsigned long)nt[11] << 24;
            unsigned long base = (unsigned long)nt[52] | (unsigned long)nt[53] << 8 | (unsigned long)nt[54] << 16 | (unsigned long)nt[55] << 24;
            ok = stamp == 0x438e4c8cul && base == 0x400000ul;
        }
    }
    fclose(f);
    return ok;
}

int jk_patch_check(const JkPaths *p, JkCheck *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    static JkManifest m;
    /* known first, so Restore stays possible even with a damaged package */
    out->has_backup = p && p->data_root && p->game_dir && jk_patch_has_record(p);
    if (load_manifest(p, &m, out->reason, sizeof out->reason) < 0) { out->state = JK_ST_BAD_PACKAGE; return 0; }
    for (int i = 0; i < m.n; i++) {
        if (m.e[i].kind == JK_M_SERVER && !p->override_dir) {
            snprintf(out->reason, sizeof out->reason, "the runtime override folder is not set");
            out->state = JK_ST_BAD_PACKAGE;
            return 0;
        }
        if (m.e[i].kind == JK_M_PROFILE && !p->profiles_dir) {
            snprintf(out->reason, sizeof out->reason, "the runtime profiles folder is not set");
            out->state = JK_ST_BAD_PACKAGE;
            return 0;
        }
    }
    if (!p->game_dir || !p->data_root || !jk_fs_is_dir(p->game_dir)) { out->state = JK_ST_NO_GAME; return 0; }

    char path[1024], cur[65];
    const JkEntry *exe = jk_manifest_exe(&m);
    if (jk_fs_join(path, sizeof path, p->game_dir, exe->path) < 0 || !jk_fs_exists(path)) { out->state = JK_ST_NO_GAME; return 0; }
    out->exe_known = jk_exe_is_known_v13(path);
    sha_or_empty(path, cur);
    if (!p->exe_any && strcmp(cur, exe->sha)) {
        snprintf(out->found_exe, sizeof out->found_exe, "%.8s", cur[0] ? cur : "unread");
        out->state = JK_ST_WRONG_EXE;
        return 0;
    }

    for (int i = 0; i < m.n; i++) {
        if (m.e[i].kind != JK_M_REQUIRE) continue;
        if (jk_fs_join(path, sizeof path, p->game_dir, m.e[i].path) == 0 && jk_fs_exists(path)) continue;
        if (!out->missing_count++) snprintf(out->first_missing, sizeof out->first_missing, "%s", m.e[i].path);
    }
    if (out->missing_count) { out->state = JK_ST_MISSING_FILES; return 0; }

    static Record rec;
    record_load(p, &rec);
    int has = rec.exists && !rec_foreign(p, &rec);   /* another folder's record is not this folder's */
    out->has_backup = has;
    int needs = 0, changed = 0;
    for (int i = 0; i < m.n; i++) {
        const JkEntry *e = &m.e[i];
        if (e->kind == JK_M_EXE || e->kind == JK_M_REQUIRE || e->kind == JK_M_ACCEPT
            || target_of(p, e, path, sizeof path) < 0) continue;
        switch (e->kind) {
        case JK_M_FILE:
        case JK_M_SEED:
        case JK_M_PROFILE:
        case JK_M_SERVER: {
            sha_or_empty(path, cur);
            if (accepted(&m, e->kind, e->path, path, cur)) break;
            needs = 1;
            Rec *r = record_find(&rec, e->kind, e->path);
            if (r && cur[0] && strcmp(cur, r->sha)) changed = 1;   /* neither ours nor our older one */
            break;
        }
        case JK_M_CONFIG:
            if (!jk_fs_exists(path)) needs = 1;
            break;
        case JK_M_REMOVE:
            if (jk_fs_exists(path)) needs = 1;
            break;
        default:
            break;
        }
    }
    if (!needs) out->state = JK_ST_READY;
    else if (!has) out->state = JK_ST_NOT_INSTALLED;
    else out->state = changed ? JK_ST_CHANGED : JK_ST_UPDATE;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Install                                                            */
/* ------------------------------------------------------------------ */

/* Copies target to backup/<rel><suffix>. Called only for a path the record
 * does not cover yet (suffix ""), so a backup found there is a leftover of a
 * crash before the record was written and is replaced by the current file;
 * or for a foreign replacement of our file (suffix ".changed"). */
static int make_backup(const JkPaths *p, const char *rel, const char *suffix, const char *target)
{
    char bk[1024], name[JK_PATH_MAX + 16];
    snprintf(name, sizeof name, "%s%s", rel, suffix);
    if (backup_of(p, name, bk, sizeof bk) < 0) return -1;
    return jk_fs_copy_checked(target, bk, NULL);
}

static int pending_path(const JkPaths *p, char *out, size_t cap)
{
    char reg[1024];
    if (jk_fs_join(reg, sizeof reg, p->data_root, "registry") < 0) return -1;
    return jk_fs_join(out, cap, reg, "pending");
}

static int write_pending(const JkPaths *p, const char *seed)
{
    char pend[1024];
    if (pending_path(p, pend, sizeof pend) < 0 || jk_fs_mkdir_parent(pend) < 0) return -1;
    return jk_fs_write_atomic(pend, seed, strlen(seed));
}

/* The game folder must be usable for any install. 0 ok. */
static int install_allowed(const JkPaths *p, char *reason, size_t cap)
{
    JkCheck c;
    jk_patch_check(p, &c);
    switch (c.state) {
    case JK_ST_NO_GAME: snprintf(reason, cap, "the game folder was not found"); return -1;
    case JK_ST_WRONG_EXE: snprintf(reason, cap, "this game version is not supported"); return -1;
    case JK_ST_MISSING_FILES: snprintf(reason, cap, "game files are missing (%s)", c.first_missing); return -1;
    case JK_ST_BAD_PACKAGE: snprintf(reason, cap, "%s", c.reason); return -1;
    default: return 0;
    }
}

/* Installs and verifies every entry of m, taking each file from src_dir. */
static int install_core(const JkPaths *p, const JkManifest *m, const char *src_dir, Record *rec,
                        JkProgress cb, void *ctx, char *reason, size_t cap)
{
    /* room for every file we will write plus the backups we will make (the
     * HD road alone is 532 MB, twice); checked before anything changes */
    long long need = FREE_MARGIN;
    const int first = !rec->exists;                    /* no install record yet: a first install */
    for (int i = 0; i < m->n; i++) {
        const JkEntry *e = &m->e[i];
        char tgt[1024], cur[65];
        if (!is_ours_kind(e->kind) && e->kind != JK_M_CONFIG && e->kind != JK_M_REMOVE) continue;
        if (target_of(p, e, tgt, sizeof tgt) < 0) continue;
        if (e->kind == JK_M_REMOVE) {
            if (!record_find(rec, e->kind, e->path) && jk_fs_size(tgt) > 0) need += jk_fs_size(tgt);
            continue;
        }
        if (e->kind == JK_M_CONFIG) { if (!jk_fs_exists(tgt)) need += e->size; continue; }
        sha_or_empty(tgt, cur);
        if (accepted(m, e->kind, e->path, tgt, cur) && !players_xidi(e, first, cur)) continue;
        need += e->size;
        if (cur[0] && e->kind != JK_M_SEED) need += jk_fs_size(tgt);   /* backup or .changed copy */
    }
    long long room = jk_fs_free_bytes(p->data_root), room_game = jk_fs_free_bytes(p->game_dir);
    if (room_game >= 0 && (room < 0 || room_game < room)) room = room_game;
    if (room >= 0 && room < need) {
        snprintf(reason, cap, "not enough free space: %lld MB needed, %lld MB free",
                 (need + (1 << 20) - 1) >> 20, room >> 20);
        return -1;
    }

    int total = 0, done = 0;
    for (int i = 0; i < m->n; i++)
        if (m->e[i].kind != JK_M_EXE && m->e[i].kind != JK_M_REQUIRE && m->e[i].kind != JK_M_ACCEPT) total++;

    for (int i = 0; i < m->n; i++) {
        const JkEntry *e = &m->e[i];
        if (e->kind == JK_M_EXE || e->kind == JK_M_REQUIRE || e->kind == JK_M_ACCEPT) continue;
        if (cb) cb(ctx, done, total, e->path);
        char tgt[1024], src[1024], cur[65], bkrel[JK_PATH_MAX + 16];
        backup_rel(e->kind, e->path, bkrel, sizeof bkrel);
        if (target_of(p, e, tgt, sizeof tgt) < 0 || jk_fs_join(src, sizeof src, src_dir, e->path) < 0) {
            snprintf(reason, cap, "path too long: %s", e->path);
            return -1;
        }
        if (e->kind != JK_M_SEED && jk_fs_safe_path(base_of(p, e->kind), e->path) < 0) {
            snprintf(reason, cap, "%s is a link or a folder: remove it and try again", e->path);
            return -1;
        }
        Rec *r = record_find(rec, e->kind, e->path);
        int save_fail = 0;
        switch (e->kind) {
        case JK_M_FILE:
        case JK_M_SEED:
        case JK_M_PROFILE:
        case JK_M_SERVER:
            sha_or_empty(tgt, cur);
            if (accepted(m, e->kind, e->path, tgt, cur) && !players_xidi(e, first, cur)) {
                if (!r) {
                    if (!record_add(rec, e->kind == JK_M_SEED ? "new" : "kept", e->kind, e->sha, e->path) || record_save(p, rec) < 0) {
                        snprintf(reason, cap, "could not write the install record");
                        return -1;
                    }
                    if (e->kind == JK_M_SEED && write_pending(p, e->path) < 0) {
                        snprintf(reason, cap, "could not mark the registry seed");
                        return -1;
                    }
                }
                break;
            }
            if (!r) {
                if (cur[0] && e->kind != JK_M_SEED) {
                    if (make_backup(p, bkrel, "", tgt) < 0) { snprintf(reason, cap, "could not back up %s", e->path); return -1; }
                    r = record_add(rec, "backup", e->kind, e->sha, e->path);
                } else {
                    r = record_add(rec, "new", e->kind, e->sha, e->path);
                }
                if (!r || record_save(p, rec) < 0) { snprintf(reason, cap, "could not write the install record"); return -1; }
            } else if (cur[0] && strcmp(cur, r->sha) && e->kind != JK_M_SEED) {
                /* someone replaced our file: keep theirs beside the original's backup */
                if (make_backup(p, bkrel, ".changed", tgt) < 0) { snprintf(reason, cap, "could not back up %s", e->path); return -1; }
            }
            /* the seed's marker is written before the seed: never a seed without it */
            if (e->kind == JK_M_SEED && write_pending(p, e->path) < 0) {
                snprintf(reason, cap, "could not mark the registry seed");
                return -1;
            }
            if (jk_fs_copy_checked(src, tgt, e->sha) < 0) { snprintf(reason, cap, "could not install %s", e->path); return -1; }
            if (strcmp(r->sha, e->sha)) { snprintf(r->sha, sizeof r->sha, "%s", e->sha); save_fail = record_save(p, rec) < 0; }
            break;
        case JK_M_CONFIG:
            if (jk_fs_exists(tgt)) {
                if (!r) { record_add(rec, "kept", e->kind, e->sha, e->path); save_fail = record_save(p, rec) < 0; }
                break;
            }
            if (!r) {
                if (!record_add(rec, "new", e->kind, e->sha, e->path) || record_save(p, rec) < 0) {
                    snprintf(reason, cap, "could not write the install record");
                    return -1;
                }
            }
            if (jk_fs_copy_checked(src, tgt, e->sha) < 0) { snprintf(reason, cap, "could not install %s", e->path); return -1; }
            break;
        case JK_M_REMOVE:
            if (!jk_fs_exists(tgt)) break;
            if (r) {
                /* it came back after an earlier install: keep this copy too */
                if (make_backup(p, e->path, ".changed", tgt) < 0) { snprintf(reason, cap, "could not back up %s", e->path); return -1; }
            } else {
                if (make_backup(p, e->path, "", tgt) < 0) { snprintf(reason, cap, "could not back up %s", e->path); return -1; }
                if (!record_add(rec, "removed", e->kind, "", e->path) || record_save(p, rec) < 0) {
                    snprintf(reason, cap, "could not write the install record");
                    return -1;
                }
            }
            if (jk_fs_remove(tgt) < 0) { snprintf(reason, cap, "could not move %s aside", e->path); return -1; }
            break;
        default:
            break;
        }
        if (save_fail) { snprintf(reason, cap, "could not write the install record"); return -1; }
        done++;
    }
    if (cb) cb(ctx, total, total, "");

    /* verify */
    for (int i = 0; i < m->n; i++) {
        const JkEntry *e = &m->e[i];
        if (!is_ours_kind(e->kind)) continue;
        char tgt[1024], cur[65];
        if (target_of(p, e, tgt, sizeof tgt) < 0) return -1;
        sha_or_empty(tgt, cur);
        if (!accepted(m, e->kind, e->path, tgt, cur)) { snprintf(reason, cap, "check failed for %s", e->path); return -1; }
    }
    return 0;
}

/* profiles.lst (console pre-check 2026-11-23): when the runtime keeps one,
 * it lists the profiles the host offers, one per line. Our profile is added
 * once: a line that is already there (any line ending, surrounding blanks)
 * is never added again. A line we added is remembered in
 * <data_root>/state/profiles-lst-added and taken out again by Restore. */
static int lst_has(const char *text, const char *name)
{
    size_t n = strlen(name);
    for (const char *s = text; s && *s;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, name, n)) {
            const char *e = s + n;
            while (*e == ' ' || *e == '\t' || *e == '\r') e++;
            if (*e == '\n' || !*e) return 1;
        }
        s = strchr(s, '\n');
        if (s) s++;
    }
    return 0;
}

static int lst_marker(const JkPaths *p, char *out, size_t cap)
{
    char st[1024];
    return jk_fs_join(st, sizeof st, p->data_root, "state") < 0 ? -1 : jk_fs_join(out, cap, st, "profiles-lst-added");
}

int jk_profiles_list_ensure(const JkPaths *p, const char *name)
{
    char lst[1024], mark[1024];
    if (!p || !p->profiles_dir || !name || jk_fs_join(lst, sizeof lst, p->profiles_dir, "profiles.lst") < 0) return -1;
    size_t n = 0;
    char *text = jk_fs_read(lst, 1 << 20, &n);
    if (!text) return jk_fs_exists(lst) ? -1 : 0;                 /* no list: the host offers every profile */
    if (lst_has(text, name)) { free(text); return 0; }
    size_t nl = strlen(name), need = n + nl + 3;
    char *out = malloc(need);
    int rc = -1;
    if (out) {
        int crlf = strstr(text, "\r\n") != NULL;
        size_t k = (size_t)snprintf(out, need, "%s%s%s%s", text, n && text[n - 1] != '\n' ? (crlf ? "\r\n" : "\n") : "", name, crlf ? "\r\n" : "\n");
        if (lst_marker(p, mark, sizeof mark) == 0 && jk_fs_mkdir_parent(mark) == 0 && jk_fs_write_atomic(mark, name, nl) == 0)
            rc = jk_fs_write_atomic(lst, out, k);
        free(out);
    }
    free(text);
    return rc;
}

/* Restore: our line out of profiles.lst, only when we added it. */
static int profiles_list_undo(const JkPaths *p)
{
    char lst[1024], mark[1024], name[160];
    if (!p->profiles_dir || lst_marker(p, mark, sizeof mark) < 0) return 0;
    size_t mn = 0;
    char *m = jk_fs_read(mark, sizeof name - 1, &mn);
    if (!m) return 0;
    snprintf(name, sizeof name, "%s", m);
    free(m);
    size_t n = 0;
    char *text = jk_fs_join(lst, sizeof lst, p->profiles_dir, "profiles.lst") == 0 ? jk_fs_read(lst, 1 << 20, &n) : NULL;
    int rc = 0;
    if (text) {
        char *out = malloc(n + 1);
        size_t k = 0;
        int dropped = 0;
        for (char *s = text; out && *s;) {
            char *e = strchr(s, '\n');
            size_t len = e ? (size_t)(e - s) + 1 : strlen(s);
            char line[200];
            snprintf(line, sizeof line, "%.*s", (int)(len < sizeof line ? len : sizeof line - 1), s);
            if (!dropped && lst_has(line, name)) dropped = 1;
            else { memcpy(out + k, s, len); k += len; }
            s += len;
        }
        if (out && dropped) rc = jk_fs_write_atomic(lst, out, k);
        free(out);
        free(text);
    }
    if (rc == 0) jk_fs_remove(mark);
    return rc;
}

int jk_patch_install(const JkPaths *p, JkProgress cb, void *ctx, char *reason, size_t cap)
{
    char dummy[8];
    if (!reason || !cap) { reason = dummy; cap = sizeof dummy; }
    reason[0] = 0;
    if (install_allowed(p, reason, cap) < 0) return -1;
    static JkManifest m;
    static Record rec;
    if (load_manifest(p, &m, reason, cap) < 0) return -1;
    record_load(p, &rec);
    if (foreign_gate(p, &rec, reason, cap) < 0) return -1;
    int r = install_core(p, &m, p->files_dir, &rec, cb, ctx, reason, cap);
    for (int i = 0; r == 0 && i < m.n; i++)
        if (m.e[i].kind == JK_M_PROFILE && jk_profiles_list_ensure(p, m.e[i].path) < 0) {
            snprintf(reason, cap, "could not add %s to profiles.lst", m.e[i].path);
            r = -1;
        }
    return r;
}

int jk_patch_install_entries(const JkPaths *p, const JkEntry *files, int n, const char *src_dir,
                             JkProgress cb, void *ctx, char *reason, size_t cap)
{
    char dummy[8];
    if (!reason || !cap) { reason = dummy; cap = sizeof dummy; }
    reason[0] = 0;
    if (!files || n < 0 || n >= JK_MANIFEST_MAX || !src_dir) { snprintf(reason, cap, "nothing to install"); return -1; }
    if (install_allowed(p, reason, cap) < 0) return -1;
    static JkManifest pkg, m;
    static Record rec;
    if (load_manifest(p, &pkg, reason, cap) < 0) return -1;
    memset(&m, 0, sizeof m);
    m.e[m.n++] = *jk_manifest_exe(&pkg);
    for (int i = 0; i < n; i++) {
        if (files[i].kind != JK_M_FILE || !jk_manifest_path_allowed(JK_M_FILE, files[i].path)) {
            snprintf(reason, cap, "not one of our files: %s", files[i].path);
            return -1;
        }
        m.e[m.n++] = files[i];
    }
    record_load(p, &rec);
    if (foreign_gate(p, &rec, reason, cap) < 0) return -1;
    return install_core(p, &m, src_dir, &rec, cb, ctx, reason, cap);
}

/* ------------------------------------------------------------------ */
/* Restore                                                            */
/* ------------------------------------------------------------------ */

/* Restores every allow-listed file found in backup/<prefix> into the game
 * folder, or, for prefix "profiles", into the profiles folder. */
static int restore_dir(const JkPaths *p, const char *prefix, char *reason, size_t cap)
{
    int prof = !strcmp(prefix, "profiles"), srv = !strcmp(prefix, "override");
    if ((prof && !p->profiles_dir) || (srv && !p->override_dir)) return 0;
    char bkroot[1024], dir[1024];
    if (jk_fs_join(bkroot, sizeof bkroot, p->data_root, "backup") < 0) return -1;
    if (*prefix) { if (jk_fs_join(dir, sizeof dir, bkroot, prefix) < 0) return -1; }
    else snprintf(dir, sizeof dir, "%s", bkroot);
    DIR *d = opendir(dir);
    if (!d) return 0;
    int rc = 0;
    struct dirent *de;
    while (rc == 0 && (de = readdir(d))) {
        char rel[JK_PATH_MAX], bk[1024], tgt[1024];
        if (de->d_name[0] == '.') continue;
        if (snprintf(rel, sizeof rel, "%s%s%s", prefix, *prefix ? "/" : "", de->d_name) >= (int)sizeof rel) continue;
        const char *name = prof || srv ? de->d_name : rel;
        if (!jk_manifest_path_allowed(prof ? JK_M_PROFILE : srv ? JK_M_SERVER : JK_M_FILE, name)) continue;  /* also skips *.changed */
        if (backup_of(p, rel, bk, sizeof bk) < 0 || jk_fs_size(bk) < 0) continue;
        const char *base = prof ? p->profiles_dir : srv ? p->override_dir : p->game_dir;
        if (jk_fs_join(tgt, sizeof tgt, base, name) < 0 || jk_fs_safe_path(base, name) < 0
            || jk_fs_copy_checked(bk, tgt, NULL) < 0) {
            snprintf(reason, cap, "could not put back %s", rel);
            rc = -1;
            break;
        }
        jk_fs_remove(bk);
    }
    closedir(d);
    return rc;
}

static int restore_from_backups(const JkPaths *p, const JkManifest *m, char *reason, size_t cap)
{
    static const char *const DIRS[] = { "", "scripts", "GLOBAL", "TRACKS", "FX/COMPILED", "profiles", "override" };
    /* our files first, so a backup put back below is never taken for ours */
    for (int i = 0; m && i < m->n; i++) {
        const JkEntry *e = &m->e[i];
        char tgt[1024], cur[65];
        if (!is_ours_kind(e->kind) || target_of(p, e, tgt, sizeof tgt) < 0) continue;
        sha_or_empty(tgt, cur);
        if (jk_manifest_accepts(m, e->path, cur) && jk_fs_remove(tgt) < 0) { snprintf(reason, cap, "could not remove %s", e->path); return -1; }
    }
    for (size_t i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++)
        if (restore_dir(p, DIRS[i], reason, cap) < 0) return -1;
    char pend[1024];
    if (pending_path(p, pend, sizeof pend) == 0) jk_fs_remove(pend);
    return 0;
}

/* Restore: the composed ini and profile go only while they still have the
 * hash the launcher last wrote (<data_root>/state/composed.txt); a file
 * changed since is not ours to delete. Then the player's own ini that the
 * first compose backed up (backup/jk4speed-dev.ini) comes back when the game
 * folder has none. */
static void remove_composed(const JkPaths *p)
{
    char path[1024], st[1024], sha[65], cur[65], bk[1024];
    if (jk_fs_join(st, sizeof st, p->data_root, "state") < 0) return;
    if (p->game_dir && jk_fs_safe_path(p->game_dir, COMPOSED_INI) == 0 && jk_fs_join(path, sizeof path, p->game_dir, COMPOSED_INI) == 0) {
        sha_or_empty(path, cur);
        if (cur[0] && jk_config_composed_sha(st, "ini", sha) == 0 && !strcmp(cur, sha)) jk_fs_remove(path);
        if (!jk_fs_exists(path) && backup_of(p, COMPOSED_INI, bk, sizeof bk) == 0 && jk_fs_exists(bk)
            && jk_fs_copy_checked(bk, path, NULL) == 0)
            jk_fs_remove(bk);
    }
    char cpn[80];
    composed_profile_name(p, cpn, sizeof cpn);
    if (p->profiles_dir && jk_fs_safe_path(p->profiles_dir, cpn) == 0
        && jk_fs_join(path, sizeof path, p->profiles_dir, cpn) == 0) {
        sha_or_empty(path, cur);
        if (cur[0] && jk_config_composed_sha(st, "profile", sha) == 0 && !strcmp(cur, sha)) jk_fs_remove(path);
    }
    jk_config_composed_forget(st);
}

int jk_patch_restore(const JkPaths *p, char *reason, size_t cap)
{
    char dummy[8];
    if (!reason || !cap) { reason = dummy; cap = sizeof dummy; }
    reason[0] = 0;
    static Record rec;
    static JkManifest m;
    if (!p || !p->game_dir || !p->data_root) { snprintf(reason, cap, "no folders"); return -1; }
    record_load(p, &rec);
    if (!rec.exists) { snprintf(reason, cap, "there is nothing to restore"); return -1; }
    if (rec_foreign(p, &rec)) {                     /* another game folder's backup: nothing here is touched */
        snprintf(reason, cap, "the backup belongs to another game folder (%.200s)", rec.game);
        return -1;
    }
    char ignored[160];
    int have_m = load_manifest(p, &m, ignored, sizeof ignored) == 0;

    char missing[JK_PATH_MAX] = "";
    if (rec.bad) {
        /* the record is damaged: put back every backup that is one of our file
         * names, remove our own files, and only then forget the record */
        if (restore_from_backups(p, have_m ? &m : NULL, reason, cap) < 0) return -1;
    } else {
        for (int i = rec.n - 1; i >= 0; i--) {
            Rec *r = &rec.r[i];
            JkEntry e;
            memset(&e, 0, sizeof e);
            e.kind = !strcmp(r->kind, "seed") ? JK_M_SEED : !strcmp(r->kind, "config") ? JK_M_CONFIG
                   : !strcmp(r->kind, "remove") ? JK_M_REMOVE : !strcmp(r->kind, "profile") ? JK_M_PROFILE : !strcmp(r->kind, "server") ? JK_M_SERVER : JK_M_FILE;
            if (!jk_manifest_path_allowed(e.kind, r->path)) continue;   /* a damaged line never steers a write */
            if (e.kind == JK_M_FILE && !strcmp(r->path, COMPOSED_INI)) continue;   /* by its composed hash, below */
            snprintf(e.path, sizeof e.path, "%s", r->path);
            char tgt[1024], bk[1024], cur[65], bkrel[JK_PATH_MAX + 16];
            backup_rel(e.kind, r->path, bkrel, sizeof bkrel);
            if (target_of(p, &e, tgt, sizeof tgt) < 0 || backup_of(p, bkrel, bk, sizeof bk) < 0
                || (e.kind != JK_M_SEED && jk_fs_safe_path(base_of(p, e.kind), r->path) < 0)) {
                snprintf(reason, cap, "could not put back %s", r->path);
                return -1;                                      /* the record stays for another try */
            }
            if (!strcmp(r->how, "backup") || !strcmp(r->how, "removed")) {
                if (!jk_fs_exists(bk)) {
                    if (!missing[0]) snprintf(missing, sizeof missing, "%s", r->path);
                    continue;
                }
                sha_or_empty(tgt, cur);
                int ours = !cur[0] || !strcmp(cur, r->sha) || (have_m && accepted(&m, e.kind, r->path, tgt, cur));
                if (!ours && make_backup(p, bkrel, ".changed", tgt) < 0) {   /* someone else's file: keep it */
                    snprintf(reason, cap, "could not keep %s", r->path);
                    return -1;
                }
                if (jk_fs_copy_checked(bk, tgt, NULL) < 0) { snprintf(reason, cap, "could not put back %s", r->path); return -1; }
                jk_fs_remove(bk);
            } else if (!strcmp(r->how, "new")) {
                sha_or_empty(tgt, cur);
                int ours = (cur[0] && !strcmp(cur, r->sha)) || (have_m && accepted(&m, e.kind, r->path, tgt, cur));
                if (e.kind == JK_M_CONFIG || e.kind == JK_M_SEED) ours = 1;  /* rewritten by the game / host */
                if (ours && jk_fs_remove(tgt) < 0) { snprintf(reason, cap, "could not remove %s", r->path); return -1; }
                if (e.kind == JK_M_SEED) {
                    char pend[1024];
                    if (pending_path(p, pend, sizeof pend) == 0) jk_fs_remove(pend);
                }
            }
            /* "kept": it was there before Jk4Speed; leave it */
        }
    }
    remove_composed(p);
    if (profiles_list_undo(p) < 0) {
        snprintf(reason, cap, "could not take our line out of profiles.lst");
        return -1;
    }
    char recp[1024];
    if (record_path(p, recp, sizeof recp) == 0 && jk_fs_remove(recp) < 0) {
        snprintf(reason, cap, "could not remove the install record");
        return -1;
    }
    if (missing[0]) {
        snprintf(reason, cap, "the backup of %s is missing; everything else was put back", missing);
        return -1;
    }
    return 0;
}
