/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Zip reading (PKWARE APPNOTE): the central directory, stored and deflated
 * entries, CRC-32 checked. See jk_archive.h. */
#include "jk_archive.h"
#include "jk_inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static uint32_t u16(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return u16(p) | u16(p + 2) << 16; }
static uint64_t u64(const unsigned char *p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }

static int say(char *why, size_t cap, const char *t) { if (why && cap) snprintf(why, cap, "%s", t); return -1; }

typedef struct In { FILE *f; unsigned long long left; unsigned char buf[65536]; } In;
static size_t in_fn(void *ctx, const unsigned char **p)
{
    In *in = ctx;
    if (!in->left) return 0;
    size_t want = in->left > sizeof in->buf ? sizeof in->buf : (size_t)in->left;
    size_t got = fread(in->buf, 1, want, in->f);
    in->left -= got;
    *p = in->buf;
    return got;
}
typedef struct Out { const JkArcOps *ops; const JkArcEntry *e; uint32_t crc; long long n; int failed; } Out;
static int out_fn(void *ctx, const unsigned char *p, size_t n)
{
    Out *o = ctx;
    o->crc = jk_crc32_update(o->crc, p, n);
    o->n += (long long)n;
    if (o->ops->sink(o->ops->ctx, o->e, p, n)) { o->failed = 1; return 1; }
    return 0;
}

int jk_zip_extract(const char *path, const JkArcOps *ops, char *why, size_t cap)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f || !ops) { if (f) fclose(f); return say(why, cap, "cannot open the archive"); }
    int rc = -1;
    unsigned char *tail = NULL, *cd = NULL;
    if (fseeko(f, 0, SEEK_END) != 0) { say(why, cap, "cannot read the archive"); goto done; }
    off_t size = ftello(f);
    size_t tl = size > 65557 ? 65557 : (size_t)size;
    tail = malloc(tl);
    if (!tail || fseeko(f, size - (off_t)tl, SEEK_SET) != 0 || fread(tail, 1, tl, f) != tl) { say(why, cap, "cannot read the archive"); goto done; }
    long eocd = -1;
    for (long i = (long)tl - 22; i >= 0; i--)
        if (u32(tail + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { say(why, cap, "not a zip archive (no directory)"); goto done; }
    uint64_t count = u16(tail + eocd + 10), cd_size = u32(tail + eocd + 12), cd_off = u32(tail + eocd + 16);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) {
        /* zip64: the locator sits right before the end record */
        if (eocd < 20 || u32(tail + eocd - 20) != 0x07064b50) { say(why, cap, "damaged zip64 archive"); goto done; }
        uint64_t rec = u64(tail + eocd - 12);
        unsigned char z[56];
        if (fseeko(f, (off_t)rec, SEEK_SET) != 0 || fread(z, 1, 56, f) != 56 || u32(z) != 0x06064b50) { say(why, cap, "damaged zip64 archive"); goto done; }
        count = u64(z + 32); cd_size = u64(z + 40); cd_off = u64(z + 48);
    }
    if (cd_size > (64u << 20) || cd_off + cd_size > (uint64_t)size) { say(why, cap, "damaged zip directory"); goto done; }
    cd = malloc((size_t)cd_size + 1);
    if (!cd || fseeko(f, (off_t)cd_off, SEEK_SET) != 0 || fread(cd, 1, (size_t)cd_size, f) != cd_size) { say(why, cap, "cannot read the zip directory"); goto done; }
    size_t pos = 0;
    for (uint64_t k = 0; k < count; k++) {
        if (pos + 46 > cd_size || u32(cd + pos) != 0x02014b50) { say(why, cap, "damaged zip directory"); goto done; }
        const unsigned char *h = cd + pos;
        uint32_t flags = u16(h + 8), method = u16(h + 10), crc = u32(h + 16);
        uint64_t csize = u32(h + 20), usize = u32(h + 24), loff = u32(h + 42);
        size_t nlen = u16(h + 28), xlen = u16(h + 30), clen = u16(h + 32);
        if (pos + 46 + nlen + xlen + clen > cd_size) { say(why, cap, "damaged zip directory"); goto done; }
        /* zip64 extra field */
        for (size_t x = 0; x + 4 <= xlen;) {
            const unsigned char *e = h + 46 + nlen + x;
            size_t id = u16(e), sz = u16(e + 2), q = 4;
            if (id == 1) {
                if (usize == 0xFFFFFFFFu && q + 8 <= 4 + sz) { usize = u64(e + q); q += 8; }
                if (csize == 0xFFFFFFFFu && q + 8 <= 4 + sz) { csize = u64(e + q); q += 8; }
                if (loff == 0xFFFFFFFFu && q + 8 <= 4 + sz) { loff = u64(e + q); q += 8; }
            }
            x += 4 + sz;
        }
        JkArcEntry ent;
        memset(&ent, 0, sizeof ent);
        size_t nl = nlen < sizeof ent.name - 1 ? nlen : sizeof ent.name - 1;
        memcpy(ent.name, h + 46, nl);
        for (size_t i = 0; i < nl; i++) if (ent.name[i] == '\\') ent.name[i] = '/';
        ent.size = (long long)usize;
        ent.crc = crc;
        ent.has_crc = 1;
        pos += 46 + nlen + xlen + clen;
        if (nl && ent.name[nl - 1] == '/') continue;           /* a folder */
        if (!ops->want(ops->ctx, &ent)) continue;
        if (flags & 1) { say(why, cap, "encrypted zip entries are not supported"); goto done; }
        unsigned char lh[30];
        if (fseeko(f, (off_t)loff, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || u32(lh) != 0x04034b50
            || fseeko(f, (off_t)(loff + 30 + u16(lh + 26) + u16(lh + 28)), SEEK_SET) != 0) {
            say(why, cap, "damaged zip entry");
            goto done;
        }
        Out o = { ops, &ent, 0, 0, 0 };
        if (method == 0) {
            static unsigned char buf[65536];
            for (uint64_t left = csize; left;) {
                size_t part = left > sizeof buf ? sizeof buf : (size_t)left;
                if (fread(buf, 1, part, f) != part) { say(why, cap, "damaged zip entry"); goto done; }
                if (out_fn(&o, buf, part)) { say(why, cap, "the entry could not be stored"); goto done; }
                left -= part;
            }
        } else if (method == 8) {
            static In in;
            in.f = f;
            in.left = csize;
            unsigned long long got = 0;
            int r = jk_inflate_back(in_fn, &in, out_fn, &o, usize, &got);
            if (o.failed) { say(why, cap, "the entry could not be stored"); goto done; }
            if (r != JK_INFLATE_OK) {
                ops->done(ops->ctx, &ent, 0);
                say(why, cap, "damaged zip entry (deflate)");
                goto done;
            }
        } else {
            say(why, cap, "unsupported zip compression (only stored and deflate)");
            goto done;
        }
        if (ops->done(ops->ctx, &ent, o.n == ent.size && o.crc == ent.crc)) { say(why, cap, "stopped"); goto done; }
    }
    rc = 0;
done:
    free(tail);
    free(cd);
    fclose(f);
    return rc;
}
