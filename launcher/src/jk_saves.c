/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Save games: info, export to USB, import from USB. See jk_saves.h. */
#include "jk_saves.h"
#include "jk_archive.h"
#include "jk_fs.h"
#include "jk_md5.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DEPTH 3
#define EXPORT_MARK "jk4speed-export.txt"

typedef int (*Visit)(void *ctx, const char *full, const char *rel, long long size);

/* Calls visit for every regular file under root (links and dot names skipped).
 * Returns the number of files, or <0 when visit failed or a limit was hit. */
static int walk(const char *root, const char *rel, int depth, Visit visit, void *ctx, int *count)
{
    char dir[1024];
    if (*rel) { if (jk_fs_join(dir, sizeof dir, root, rel) < 0) return -1; }
    else snprintf(dir, sizeof dir, "%s", root);
    DIR *d = opendir(dir);
    if (!d) return -1;
    int rc = 0;
    struct dirent *e;
    while (rc == 0 && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[1024], r[512];
        struct stat st;
        if (jk_fs_join(full, sizeof full, dir, e->d_name) < 0
            || snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name) >= (int)sizeof r) { rc = -1; break; }
        if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth + 1 < MAX_DEPTH) rc = walk(root, r, depth + 1, visit, ctx, count);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (++*count > JK_SAVES_MAX_FILES || (long long)st.st_size > JK_SAVES_MAX_FILE) { rc = -2; break; }
        if (visit && visit(ctx, full, r, (long long)st.st_size) < 0) rc = -1;
    }
    closedir(d);
    return rc;
}

/* The saves folder, never a link. 1 a real folder, 0 missing, -1 a link or a file. */
static int real_dir(const char *path)
{
    struct stat st;
    if (!path || lstat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : -1;
}

static int sum(void *ctx, const char *full, const char *rel, long long size)
{
    (void)full; (void)rel;
    *(long long *)ctx += size;
    return 0;
}

int jk_saves_info(const char *saves_dir, JkSavesInfo *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (real_dir(saves_dir) != 1) return 0;
    out->exists = 1;
    DIR *d = opendir(saves_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char p[1024];
            struct stat st;
            if (e->d_name[0] == '.' || jk_fs_join(p, sizeof p, saves_dir, e->d_name) < 0) continue;
            if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) out->profiles++;
        }
        closedir(d);
    }
    walk(saves_dir, "", 0, sum, &out->bytes, &out->files);
    return 0;
}

/* Copy every file of a walk into dst/<rel>. */
typedef struct CopyCtx { const char *dst; } CopyCtx;
static int copy_one(void *ctx, const char *full, const char *rel, long long size)
{
    (void)size;
    const CopyCtx *c = ctx;
    char to[1024];
    if (jk_fs_join(to, sizeof to, c->dst, rel) < 0) return -1;
    if (jk_fs_mkdir_parent(to) < 0 || jk_fs_safe_path(c->dst, rel) < 0) return -1;
    return jk_fs_copy_checked(full, to, NULL);
}

static int copy_tree(const char *src, const char *dst)
{
    CopyCtx c = { dst };
    int n = 0;
    if (jk_fs_mkdir_p(dst) < 0) return -1;
    return walk(src, "", 0, copy_one, &c, &n);
}

/* A folder name made only of [A-Za-z0-9_-]. */
static void clean_stamp(char *out, size_t cap, const char *s)
{
    size_t n = 0;
    for (; s && *s && n + 1 < cap; s++) {
        char ch = *s;
        int ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        out[n++] = ok ? ch : '_';
    }
    out[n] = 0;
}

/* parent/<stamp>[-k] (or parent/<fallback>-k) that does not exist yet. */
static int fresh_dir(char *out, size_t cap, const char *parent, const char *stamp, const char *fallback)
{
    char s[96], name[128];
    clean_stamp(s, sizeof s, stamp);
    for (int k = 1; k < 1000; k++) {
        if (s[0]) { if (k == 1) snprintf(name, sizeof name, "%s", s); else snprintf(name, sizeof name, "%s-%d", s, k); }
        else snprintf(name, sizeof name, "%s-%d", fallback, k);
        if (jk_fs_join(out, cap, parent, name) < 0) return -1;
        if (!jk_fs_exists(out)) return 0;
    }
    return -1;
}

static void say(char *why, size_t cap, const char *text)
{
    if (why && cap) snprintf(why, cap, "%s", text);
}

int jk_saves_export(const char *saves_dir, const char *usb_dir, const char *stamp,
                    char *out, size_t cap, char *why, size_t wcap)
{
    if (out && cap) out[0] = 0;
    JkSavesInfo in;
    jk_saves_info(saves_dir, &in);
    if (!in.files) { say(why, wcap, "There are no saves yet. Play once, then export."); return -1; }
    if (!usb_dir || !jk_fs_is_dir(usb_dir)) { say(why, wcap, "No USB drive found. Plug one in and try again."); return -1; }
    char parent[1024], dir[1024], dst[1100];
    if (jk_fs_join(parent, sizeof parent, usb_dir, "Jk4Speed/saves") < 0 || jk_fs_mkdir_p(parent) < 0
        || fresh_dir(dir, sizeof dir, parent, stamp, "export") < 0 || jk_fs_join(dst, sizeof dst, dir, JK_SAVES_FOLDER) < 0) {
        say(why, wcap, "The USB drive could not be written.");
        return -1;
    }
    int r = copy_tree(saves_dir, dst);
    if (r < 0) {
        say(why, wcap, r == -2 ? "The save folder is larger than expected; nothing more was copied."
                               : "The USB drive could not be written. Check its free space.");
        return -1;
    }
    char mark[1100];
    static const char note[] = "Saves exported by JK4SPEED from the console.\n";
    if (jk_fs_join(mark, sizeof mark, dir, EXPORT_MARK) == 0) jk_fs_write_atomic(mark, note, sizeof note - 1);
    if (out && cap) snprintf(out, cap, "%s", dir);
    return 0;
}

/* ---- the save file ---- */

static uint32_t le32(const unsigned char *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void put_le32(unsigned char *b, uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (unsigned char)(v >> (8 * i)); }

/* The game's own CRC (speed.exe 0x7f73e1): the MSB-first table of
 * 0x04C11DB7, seeded with the first four bytes big-endian. */
unsigned jk_save_crc(const unsigned char *d, size_t n)
{
    static uint32_t T[256];
    if (!T[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : c << 1;
            T[i] = c;
        }
    if (n < 4) return 0;
    uint32_t c = ~((uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]);
    for (size_t i = 4; i < n; i++) c = ((c << 8) | d[i]) ^ T[c >> 24];
    return ~c;
}

/* docs/launcher/2026-11-23-save-format.md: magic, size, blocks, crcHdr,
 * crcA, crcB (EA-CRC) and the MD5 tail. */
static int check_bytes(const unsigned char *d, size_t n)
{
    unsigned char md[16];
    if ((long long)n != JK_SAVE_SIZE || memcmp(d, "20CM", 4) || le32(d + 4) != n) return -1;
    uint32_t la = le32(d + 8), lb = le32(d + 12);
    if (la != 8 || (uint64_t)0x1C + la + lb != n) return -1;
    if (jk_save_crc(d, 0x18) != le32(d + 0x18) || jk_save_crc(d + 0x1C, la) != le32(d + 0x10)
        || jk_save_crc(d + 0x24, lb) != le32(d + 0x14)) return -1;
    jk_md5(d + 0x34, n - 0x34 - 16, md);
    return memcmp(md, d + n - 16, 16) ? -1 : 0;
}

/* The checksums again after a change, in the game's order: MD5, crcB, crcA, crcHdr. */
static void reseal(unsigned char *d, size_t n)
{
    jk_md5(d + 0x34, n - 0x34 - 16, d + n - 16);
    put_le32(d + 0x14, jk_save_crc(d + 0x24, le32(d + 12)));
    put_le32(d + 0x10, jk_save_crc(d + 0x1C, le32(d + 8)));
    put_le32(d + 0x18, jk_save_crc(d, 0x18));
}

int jk_alias_valid(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > JK_ALIAS_MAX) return 0;
    for (; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9'))) return 0;
    return 1;
}

int jk_alias_char(int ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); }

int jk_save_rename_bytes(unsigned char *d, size_t n, const char *old, const char *nw)
{
    size_t ol = old ? strlen(old) : 0;
    if (check_bytes(d, n) < 0 || !ol || ol >= JK_SAVE_NAME_LEN || !jk_alias_valid(nw)) return -1;
    if (memcmp(d + JK_SAVE_NAME_AT, old, ol) || d[JK_SAVE_NAME_AT + ol]) return -1;     /* the old name must be there */
    memset(d + JK_SAVE_NAME_AT, 0, JK_SAVE_NAME_LEN);
    memcpy(d + JK_SAVE_NAME_AT, nw, strlen(nw));
    reseal(d, n);
    return 0;
}

int jk_save_check(const char *path)
{
    size_t n = 0;
    if (jk_fs_size(path) != JK_SAVE_SIZE) return -1;
    unsigned char *d = (unsigned char *)jk_fs_read(path, (size_t)JK_SAVE_SIZE, &n);
    int r = d ? check_bytes(d, n) : -1;
    free(d);
    return r;
}

void jk_saves_date(long long t, char out[16])
{
    long long z = (t >= 0 ? t : 0) / 86400 + 719468, era = z / 146097;
    long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    int yy = (int)(y + (m <= 2)), mm = (int)m, dd = (int)d;
    if (yy < 0 || yy > 9999 || mm < 1 || mm > 12 || dd < 1 || dd > 31) { yy = 1970; mm = 1; dd = 1; }
    snprintf(out, 16, "%04u-%02u-%02u", (unsigned)yy % 10000u, (unsigned)mm % 100u, (unsigned)dd % 100u);
}

/* ---- finding saves on the USB drive ---- */

static int alias_ok(const char *s)
{
    size_t n = strlen(s);
    if (!n || n >= 32 || s[0] == '.') return 0;
    for (; *s; s++) if ((unsigned char)*s < 0x20 || *s == '/' || *s == '\\') return 0;
    return 1;
}

static int has_alias(const JkSavesFound *f, const char *a, int skip)
{
    for (int i = 0; i < f->n; i++) if (i != skip && !strcasecmp(f->c[i].alias, a)) return 1;
    return 0;
}

static void scan(const char *dir, int depth, int console, JkSavesFound *f)
{
    DIR *d = depth <= MAX_DEPTH ? opendir(dir) : NULL;
    if (!d) return;
    char mark[1100];
    if (jk_fs_join(mark, sizeof mark, dir, EXPORT_MARK) == 0 && jk_fs_exists(mark)) console = 1;
    struct dirent *e;
    while ((e = readdir(d))) {
        char sub[1024], file[1100];
        struct stat st;
        if (e->d_name[0] == '.' || jk_fs_join(sub, sizeof sub, dir, e->d_name) < 0 || lstat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (jk_fs_join(file, sizeof file, sub, e->d_name) == 0 && lstat(file, &st) == 0 && S_ISREG(st.st_mode)) {
            if (!alias_ok(e->d_name) || jk_save_check(file) < 0) {
                if (!f->rejected++) snprintf(f->rejected_name, sizeof f->rejected_name, "%.40s/%.40s", e->d_name, e->d_name);
                continue;
            }
            int at = -1;
            for (int i = 0; i < f->n; i++) if (!strcasecmp(f->c[i].alias, e->d_name)) at = i;
            if (at >= 0 && f->c[at].mtime >= (long long)st.st_mtime) continue;      /* the newest copy wins */
            if (at < 0) { if (f->n >= JK_SAVES_CAND_MAX) continue; at = f->n++; }
            JkSaveCand *c = &f->c[at];
            memset(c, 0, sizeof *c);
            snprintf(c->alias, sizeof c->alias, "%.63s", e->d_name);
            snprintf(c->dir, sizeof c->dir, "%s", sub);
            c->mtime = (long long)st.st_mtime;
            c->console = console;
            continue;
        }
        scan(sub, depth + 1, console, f);
    }
    closedir(d);
}

/* archives in Jk4Speed/saves/ unpacked to <data_root>/saves-in/<k>/ */
typedef struct Unz { const char *root; int fd; int files; long long bytes; int bad; char tmp[1200]; } Unz;
static int unz_want(void *ctx, const JkArcEntry *e)
{
    Unz *u = ctx;
    u->fd = -1;
    if (strstr(e->name, "..") || e->name[0] == '/' || strchr(e->name, '\\') || e->size > (4LL << 20)
        || ++u->files > JK_SAVES_MAX_FILES || (u->bytes += e->size) > (64LL << 20)) return 0;
    char to[1200];
    if (jk_fs_join(to, sizeof to, u->root, e->name) < 0 || jk_fs_mkdir_parent(to) < 0 || jk_fs_safe_path(u->root, e->name) < 0) return 0;
    snprintf(u->tmp, sizeof u->tmp, "%s", to);
    u->fd = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    return u->fd >= 0;
}
static int unz_sink(void *ctx, const JkArcEntry *e, const unsigned char *p, size_t n)
{
    Unz *u = ctx;
    (void)e;
    while (n) {
        ssize_t w = write(u->fd, p, n);
        if (w <= 0) return 1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}
static int unz_done(void *ctx, const JkArcEntry *e, int ok)
{
    Unz *u = ctx;
    (void)e;
    if (u->fd >= 0) close(u->fd);
    if (!ok) { unlink(u->tmp); u->bad++; }
    u->fd = -1;
    return 0;
}

static void rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1100];
        struct stat st;
        if (jk_fs_join(p, sizeof p, dir, e->d_name) < 0 || lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) rm_tree(p);
        else unlink(p);
    }
    if (d) closedir(d);
    rmdir(dir);
}

int jk_saves_find(const char *saves_dir, const char *usb_dir, const char *data_root, JkSavesFound *f,
                  char *why, size_t wcap)
{
    if (!f) return -1;
    memset(f, 0, sizeof *f);
    if (!usb_dir || !jk_fs_is_dir(usb_dir)) { say(why, wcap, "No USB drive found. Plug one in and try again."); return -1; }
    char top[1024], legacy[1024], in[1024];
    if (jk_fs_join(top, sizeof top, usb_dir, JK_SAVES_USB) < 0) { say(why, wcap, "The USB path is too long."); return -1; }
    scan(top, 0, 0, f);
    if (jk_fs_join(legacy, sizeof legacy, usb_dir, JK_SAVES_FOLDER) == 0 && real_dir(legacy) == 1) scan(legacy, 1, 0, f);
    /* zips and 7z of either layout */
    if (data_root && jk_fs_join(in, sizeof in, data_root, "saves-in") == 0) {
        rm_tree(in);
        DIR *d = opendir(top);
        struct dirent *e;
        int k = 0;
        while (d && (e = readdir(d)) && k < 16) {
            char p[1100], dst[1100], name[16];
            if (e->d_name[0] == '.' || jk_fs_join(p, sizeof p, top, e->d_name) < 0 || jk_fs_is_dir(p)) continue;
            JkArcType t = jk_archive_type(p);
            if (t != JK_ARC_ZIP && t != JK_ARC_7Z) continue;
            snprintf(name, sizeof name, "%d", k++);
            if (jk_fs_join(dst, sizeof dst, in, name) < 0 || jk_fs_mkdir_p(dst) < 0) continue;
            static Unz u;
            memset(&u, 0, sizeof u);
            u.root = dst;
            u.fd = -1;
            JkArcOps ops = { &u, unz_want, unz_sink, unz_done };
            char w[160];
            if (jk_archive_extract(p, &ops, w, sizeof w) < 0 && !f->rejected++)
                snprintf(f->rejected_name, sizeof f->rejected_name, "%.90s", e->d_name);
            if (u.fd >= 0) close(u.fd);
            scan(dst, 0, 0, f);
        }
        if (d) closedir(d);
    }
    /* collisions and the "keep both" names */
    for (int i = 0; i < f->n; i++) {
        JkSaveCand *c = &f->c[i];
        char on[1100];
        c->exists = saves_dir && jk_fs_join(on, sizeof on, saves_dir, c->alias) == 0 && jk_fs_exists(on);
        f->collisions += c->exists;
        memcpy(c->keep_as, c->alias, sizeof c->keep_as);
        size_t n = strlen(c->keep_as);
        static const char digits[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
        for (const char *dg = digits; *dg; dg++) {
            c->keep_as[n - 1] = *dg;
            if (!strcasecmp(c->keep_as, c->alias) || has_alias(f, c->keep_as, -1)) continue;
            if (saves_dir && jk_fs_join(on, sizeof on, saves_dir, c->keep_as) == 0 && jk_fs_exists(on)) continue;
            break;
        }
    }
    return 0;
}

/* ---- import ---- */

/* the alias folder's files into dst/<as>/ (the save renamed and its name
 * fields and MD5 rewritten when as differs) */
static int put_alias(const JkSaveCand *c, const char *dst_root, const char *as)
{
    char dst[1100];
    if (jk_fs_join(dst, sizeof dst, dst_root, as) < 0) return -1;
    rm_tree(dst);
    if (jk_fs_mkdir_p(dst) < 0) return -1;
    DIR *d = opendir(c->dir);
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && d && (e = readdir(d))) {
        char from[1100], to[1200];
        struct stat st;
        if (e->d_name[0] == '.' || jk_fs_join(from, sizeof from, c->dir, e->d_name) < 0 || lstat(from, &st) != 0
            || !S_ISREG(st.st_mode) || st.st_size > (4LL << 20)) continue;
        int is_save = !strcmp(e->d_name, c->alias);
        if (jk_fs_join(to, sizeof to, dst, is_save ? as : e->d_name) < 0) { rc = -1; break; }
        if (!is_save || !strcmp(as, c->alias)) { rc = jk_fs_copy_checked(from, to, NULL); continue; }
        size_t n = 0;
        unsigned char *b = (unsigned char *)jk_fs_read(from, (size_t)JK_SAVE_SIZE, &n);
        if (!b || jk_save_rename_bytes(b, n, c->alias, as) < 0) { free(b); rc = -1; break; }
        rc = jk_fs_write_atomic(to, b, n);
        free(b);
    }
    if (d) closedir(d);
    return rc;
}

static int side(char *out, size_t cap, const char *saves_dir, const char *suffix)
{
    return snprintf(out, cap, "%s%s", saves_dir, suffix) < (int)cap ? 0 : -1;
}

int jk_saves_recover(const char *saves_dir)
{
    char nw[1100], old[1100];
    if (!saves_dir || side(nw, sizeof nw, saves_dir, ".jk4new") < 0 || side(old, sizeof old, saves_dir, ".jk4old") < 0) return 0;
    int fixed = 0;
    if (!jk_fs_exists(saves_dir) && jk_fs_is_dir(old)) { if (rename(old, saves_dir) == 0) fixed = 1; }
    if (jk_fs_is_dir(old)) { rm_tree(old); fixed = 1; }               /* the swap was done: the backup has them */
    if (jk_fs_is_dir(nw)) { rm_tree(nw); fixed = 1; }                 /* never swapped in: dropped */
    return fixed;
}

static int swap_in(const char *saves_dir, int have, const char *nw, const char *old, const char *data_root,
                   const char *stamp, char *backup, size_t bcap, char *why, size_t wcap);

int jk_saves_import_found(const char *saves_dir, const JkSavesFound *f, int keep_both, const char *data_root,
                          const char *stamp, char *backup, size_t bcap, char *why, size_t wcap)
{
    if (backup && bcap) backup[0] = 0;
    if (!saves_dir || !data_root) { say(why, wcap, "The save folder is not known."); return -1; }
    if (!f || !f->n) { say(why, wcap, "No saves to import."); return -1; }
    jk_saves_recover(saves_dir);
    int have = real_dir(saves_dir);
    if (have < 0) { say(why, wcap, "The save folder is a link. Nothing was changed."); return -1; }
    char nw[1100], old[1100];
    if (side(nw, sizeof nw, saves_dir, ".jk4new") < 0 || side(old, sizeof old, saves_dir, ".jk4old") < 0) {
        say(why, wcap, "The save folder path is too long.");
        return -1;
    }
    /* 1. the new folder: the current saves + the imported ones */
    if (jk_fs_mkdir_p(nw) < 0 || (have == 1 && copy_tree(saves_dir, nw) < 0)) {
        rm_tree(nw);
        say(why, wcap, "Not enough free space to prepare the import. Nothing was changed.");
        return -1;
    }
    for (int i = 0; i < f->n; i++) {
        const JkSaveCand *c = &f->c[i];
        if (put_alias(c, nw, c->exists && keep_both ? c->keep_as : c->alias) < 0) {
            rm_tree(nw);
            say(why, wcap, "A save could not be copied. Nothing was changed.");
            return -1;
        }
    }
    /* 2. every save in it checked */
    DIR *d = opendir(nw);
    struct dirent *e;
    int bad = 0;
    while (d && (e = readdir(d))) {
        char p[1100], s[1200];
        if (e->d_name[0] == '.' || jk_fs_join(p, sizeof p, nw, e->d_name) < 0 || !jk_fs_is_dir(p)) continue;
        if (jk_fs_join(s, sizeof s, p, e->d_name) == 0 && jk_fs_exists(s) && jk_save_check(s) < 0) {
            /* a save that was on the console already is kept as it is; an imported one must pass */
            for (int i = 0; i < f->n; i++)
                if (!strcmp(e->d_name, f->c[i].exists && keep_both ? f->c[i].keep_as : f->c[i].alias)) bad = 1;
        }
    }
    if (d) closedir(d);
    if (bad) { rm_tree(nw); say(why, wcap, "An imported save did not check out. Nothing was changed."); return -1; }
    return swap_in(saves_dir, have, nw, old, data_root, stamp, backup, bcap, why, wcap);
}

/* Steps 3 and 4 of an import or a rename: back up the current saves, then
 * two renames put the prepared folder nw in place (jk_saves_recover finishes
 * or undoes a cut between them). */
static int swap_in(const char *saves_dir, int have, const char *nw, const char *old, const char *data_root,
                   const char *stamp, char *backup, size_t bcap, char *why, size_t wcap)
{
    JkSavesInfo in;
    jk_saves_info(saves_dir, &in);
    if (in.files) {
        char parent[1024], dir[1024], dst[1100];
        if (jk_fs_join(parent, sizeof parent, data_root, "backup/saves") < 0 || jk_fs_mkdir_p(parent) < 0
            || fresh_dir(dir, sizeof dir, parent, stamp, "saves") < 0 || jk_fs_join(dst, sizeof dst, dir, JK_SAVES_FOLDER) < 0
            || copy_tree(saves_dir, dst) < 0) {
            rm_tree(nw);
            say(why, wcap, "Your current saves could not be backed up, so nothing was changed. Check the free space.");
            return -1;
        }
        if (backup && bcap) snprintf(backup, bcap, "%s", dir);
    }
    /* 4. swap (creates the folder when the game has never saved) */
    if (have == 1 && jk_fs_rename(saves_dir, old) < 0) { rm_tree(nw); say(why, wcap, "The save folder could not be replaced. Nothing was changed."); return -1; }
    if (jk_fs_rename(nw, saves_dir) < 0) {
        if (have == 1) rename(old, saves_dir);
        rm_tree(nw);
        say(why, wcap, "The save folder could not be replaced. Nothing was changed.");
        return -1;
    }
    rm_tree(old);
    return 0;
}

int jk_saves_list(const char *saves_dir, char names[][64], int max)
{
    int n = 0;
    DIR *d = real_dir(saves_dir) == 1 ? opendir(saves_dir) : NULL;
    struct dirent *e;
    while (d && n < max && (e = readdir(d))) {
        char p[1100], f[1200];
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 64 || jk_fs_join(p, sizeof p, saves_dir, e->d_name) < 0
            || real_dir(p) != 1 || jk_fs_join(f, sizeof f, p, e->d_name) < 0 || !jk_fs_exists(f)) continue;
        memcpy(names[n++], e->d_name, strlen(e->d_name) + 1);
    }
    if (d) closedir(d);
    for (int i = 1; i < n; i++)                       /* sorted: the screen does not depend on the folder's order */
        for (int k = i; k > 0 && strcasecmp(names[k - 1], names[k]) > 0; k--) {
            char t[64];
            memcpy(t, names[k], 64); memcpy(names[k], names[k - 1], 64); memcpy(names[k - 1], t, 64);
        }
    return n;
}

int jk_saves_rename(const char *saves_dir, const char *old, const char *nw_name, const char *data_root,
                    const char *stamp, char *backup, size_t bcap, char *why, size_t wcap)
{
    if (backup && bcap) backup[0] = 0;
    if (!saves_dir || !data_root || !old || !nw_name) { say(why, wcap, "The save folder is not known."); return -1; }
    if (!jk_alias_valid(nw_name)) { say(why, wcap, "Use 1 to 15 letters and digits for the name."); return -1; }
    jk_saves_recover(saves_dir);
    if (real_dir(saves_dir) != 1) { say(why, wcap, "There are no saves yet."); return -1; }
    char src[1100], file[1200];
    if (jk_fs_join(src, sizeof src, saves_dir, old) < 0 || jk_fs_join(file, sizeof file, src, old) < 0 || real_dir(src) != 1) {
        say(why, wcap, "That profile was not found.");
        return -1;
    }
    /* another folder with the same name in any case is a collision (a case
     * change of the profile itself is fine) */
    DIR *d = opendir(saves_dir);
    struct dirent *e;
    int clash = 0;
    while (d && (e = readdir(d)))
        if (e->d_name[0] != '.' && strcmp(e->d_name, old) && !strcasecmp(e->d_name, nw_name)) clash = 1;
    if (d) closedir(d);
    if (clash) { say(why, wcap, "A profile with that name is already here."); return -1; }
    size_t n = 0;
    unsigned char *b = (unsigned char *)jk_fs_read(file, (size_t)JK_SAVE_SIZE, &n);
    if (!b || jk_save_rename_bytes(b, n, old, nw_name) < 0) {
        free(b);
        say(why, wcap, "This save did not check out (damaged or not a v1.3 save). Nothing was changed.");
        return -1;
    }
    char nw[1100], oldside[1100], gone[1100], dst[1200], dfile[1300];
    if (side(nw, sizeof nw, saves_dir, ".jk4new") < 0 || side(oldside, sizeof oldside, saves_dir, ".jk4old") < 0) {
        free(b);
        say(why, wcap, "The save folder path is too long.");
        return -1;
    }
    /* the new folder: every other profile as it is, <new>/<new> instead of <old>/ */
    int ok = jk_fs_mkdir_p(nw) == 0 && copy_tree(saves_dir, nw) == 0 && jk_fs_join(gone, sizeof gone, nw, old) == 0;
    if (ok) rm_tree(gone);
    ok = ok && jk_fs_join(dst, sizeof dst, nw, nw_name) == 0 && jk_fs_join(dfile, sizeof dfile, dst, nw_name) == 0
         && jk_fs_mkdir_p(dst) == 0 && jk_fs_write_atomic(dfile, b, n) == 0 && jk_save_check(dfile) == 0;
    free(b);
    if (!ok) { rm_tree(nw); say(why, wcap, "Not enough free space to rename. Nothing was changed."); return -1; }
    return swap_in(saves_dir, 1, nw, oldside, data_root, stamp, backup, bcap, why, wcap);
}


#define MIGRATED_MARK "saves-migrated"

int jk_saves_migrate_once(const char *old_dir, const char *new_dir, const char *state_dir, char *note, size_t cap)
{
    char mark[1024];
    if (note && cap) note[0] = 0;
    if (!new_dir || !state_dir || jk_fs_join(mark, sizeof mark, state_dir, MIGRATED_MARK) < 0) return -1;
    if (jk_fs_exists(mark)) { say(note, cap, "already decided"); return 0; }
    static char names[JK_SAVES_CAND_MAX][64];
    int n = old_dir && real_dir(old_dir) == 1 ? jk_saves_list(old_dir, names, JK_SAVES_CAND_MAX) : 0;
    if (n <= 0) { say(note, cap, "no r3 saves"); return 0; }
    char none[JK_SAVES_CAND_MAX][64];
    int rc = 0;
    if (jk_saves_list(new_dir, none, 1) > 0) say(note, cap, "r4 saves exist, r3 saves left alone");
    else {
        /* Everything is copied into a staging folder beside new_dir first and
         * moved in only when every profile made it: a failure or a power cut
         * leaves new_dir as it was, and the next open copies again. */
        char stage[1100], slash[1100];
        snprintf(slash, sizeof slash, "%s", new_dir);
        char *cut = strrchr(slash, '/');
        if (!cut || cut == slash) return -1;
        *cut = 0;
        if (jk_fs_join(stage, sizeof stage, slash, ".saves-migrate-staging") < 0) return -1;
        rm_tree(stage);                                  /* a leftover of a cut-off try */
        for (int i = 0; i < n && rc == 0; i++) {
            char src[1100], dst[1100];
            if (jk_fs_join(src, sizeof src, old_dir, names[i]) < 0 || jk_fs_join(dst, sizeof dst, stage, names[i]) < 0
                || copy_tree(src, dst) < 0) rc = -1;
        }
        if (rc == 0) {
            rmdir(new_dir);                              /* only succeeds when empty */
            if (!jk_fs_exists(new_dir)) { if (jk_fs_rename(stage, new_dir) < 0) rc = -1; }
            else {                                       /* new_dir holds other files: move profile by profile */
                if (jk_fs_mkdir_p(new_dir) < 0) rc = -1;
                for (int i = 0; i < n && rc == 0; i++) {
                    char from[1100], to[1100];
                    if (jk_fs_join(from, sizeof from, stage, names[i]) < 0 || jk_fs_join(to, sizeof to, new_dir, names[i]) < 0
                        || jk_fs_rename(from, to) < 0) rc = -1;
                }
                if (rc < 0) for (int i = 0; i < n; i++) {   /* undo what was moved in */
                    char to[1100];
                    if (jk_fs_join(to, sizeof to, new_dir, names[i]) == 0 && jk_fs_exists(to)) rm_tree(to);
                }
            }
        }
        rm_tree(stage);
        if (rc < 0) { say(note, cap, "copy failed, will try again"); return -1; }
        rc = 1;
        if (note && cap) snprintf(note, cap, "copied %d r3 profile%s", n, n == 1 ? "" : "s");
    }
    if (jk_fs_mkdir_p(state_dir) < 0 || jk_fs_write_atomic(mark, "1\n", 2) < 0) return -1;
    return rc;
}
