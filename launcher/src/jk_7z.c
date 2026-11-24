/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* 7z reading, written for the launcher from the format description in the
 * LZMA SDK's 7zFormat.txt (public domain): signature header, (encoded)
 * header, pack/unpack/substream info, file names; folders with one coder
 * (LZMA2, LZMA or copy), decoded as a stream and cut into their files.
 * See jk_archive.h. */
#include "jk_archive.h"
#include "jk_inflate.h"
#include "jk_lzma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define MAX_FILES 65536
#define MAX_FOLDERS 4096
#define HEADER_MAX (64u << 20)

enum { kEnd = 0, kHeader = 1, kArchiveProperties = 2, kAdditionalStreamsInfo = 3, kMainStreamsInfo = 4,
       kFilesInfo = 5, kPackInfo = 6, kUnPackInfo = 7, kSubStreamsInfo = 8, kSize = 9, kCRC = 10, kFolder = 11,
       kCodersUnPackSize = 12, kNumUnPackStream = 13, kEmptyStream = 14, kEmptyFile = 15, kName = 17,
       kEncodedHeader = 23, kDummy = 25 };

typedef struct Folder {
    unsigned char method[8]; size_t mlen;
    unsigned char props[8]; size_t plen;
    uint64_t unpack, pack_off, pack_size;
    uint32_t crc; int has_crc;
    uint64_t nsub;
} Folder;

typedef struct Arc {
    FILE *f;
    uint64_t base;                 /* 32: packed streams start here */
    Folder *fo; size_t nfo;
    uint64_t *packs; size_t npack; uint64_t pack_pos;
    /* substreams in folder order */
    uint64_t *sub_size; uint32_t *sub_crc; unsigned char *sub_has_crc; size_t nsub;
    /* files */
    size_t nfiles;
    unsigned char *empty;          /* per file: no stream */
    unsigned char *empty_file;     /* per file: an empty file (not a folder) */
    char (*names)[512];
    int bad;
} Arc;

typedef struct Buf { const unsigned char *p; size_t n, i; int bad; } Buf;

static unsigned rd8(Buf *b) { if (b->i >= b->n) { b->bad = 1; return 0; } return b->p[b->i++]; }
static uint64_t rdnum(Buf *b)
{
    unsigned first = rd8(b), mask = 0x80;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        if (!(first & mask)) {
            uint64_t high = first & (mask - 1u);
            v |= high << (8 * i);
            return v;
        }
        v |= (uint64_t)rd8(b) << (8 * i);
        mask >>= 1;
    }
    return v;
}
static uint32_t rd32(Buf *b) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)rd8(b) << (8 * i); return v; }
static void skip(Buf *b, uint64_t n) { if (n > b->n - b->i) { b->bad = 1; b->i = b->n; } else b->i += (size_t)n; }

/* bit vector of n bits (all defined when the "all" byte is set) */
static unsigned char *bits(Buf *b, size_t n, int has_all_byte)
{
    unsigned char *v = calloc(n ? n : 1, 1);
    if (!v) { b->bad = 1; return NULL; }
    if (has_all_byte && rd8(b)) { memset(v, 1, n); return v; }
    unsigned cur = 0, mask = 0;
    for (size_t i = 0; i < n; i++) {
        if (!mask) { cur = rd8(b); mask = 0x80; }
        v[i] = (cur & mask) != 0;
        mask >>= 1;
    }
    return v;
}

static void pack_info(Buf *b, Arc *a)
{
    a->pack_pos = rdnum(b);
    uint64_t n = rdnum(b);
    if (n > MAX_FOLDERS) { b->bad = 1; return; }
    a->npack = (size_t)n;
    a->packs = calloc(a->npack ? a->npack : 1, sizeof *a->packs);
    if (!a->packs) { b->bad = 1; return; }
    for (;;) {
        unsigned t = rd8(b);
        if (b->bad || t == kEnd) break;
        if (t == kSize) for (size_t i = 0; i < a->npack; i++) a->packs[i] = rdnum(b);
        else if (t == kCRC) { unsigned char *def = bits(b, a->npack, 1); for (size_t i = 0; def && i < a->npack; i++) if (def[i]) rd32(b); free(def); }
        else { b->bad = 1; return; }
    }
}

static void unpack_info(Buf *b, Arc *a)
{
    if (rd8(b) != kFolder) { b->bad = 1; return; }
    uint64_t n = rdnum(b);
    if (n > MAX_FOLDERS || rd8(b) != 0) { b->bad = 1; return; }     /* no external folders */
    a->nfo = (size_t)n;
    a->fo = calloc(a->nfo ? a->nfo : 1, sizeof *a->fo);
    if (!a->fo) { b->bad = 1; return; }
    for (size_t k = 0; k < a->nfo && !b->bad; k++) {
        Folder *fo = &a->fo[k];
        uint64_t ncoders = rdnum(b);
        if (ncoders != 1) { b->bad = 2; return; }                /* filters (BCJ...) are not needed for our mods */
        unsigned flag = rd8(b);
        fo->mlen = flag & 0x0F;
        if (fo->mlen > sizeof fo->method || (flag & 0x10)) { b->bad = 2; return; }
        for (size_t i = 0; i < fo->mlen; i++) fo->method[i] = (unsigned char)rd8(b);
        if (flag & 0x20) {
            uint64_t pl = rdnum(b);
            if (pl > sizeof fo->props) { b->bad = 1; return; }
            fo->plen = (size_t)pl;
            for (size_t i = 0; i < fo->plen; i++) fo->props[i] = (unsigned char)rd8(b);
        }
        fo->nsub = 1;
    }
    if (rd8(b) != kCodersUnPackSize) { b->bad = 1; return; }
    for (size_t k = 0; k < a->nfo; k++) a->fo[k].unpack = rdnum(b);
    for (;;) {
        unsigned t = rd8(b);
        if (b->bad || t == kEnd) break;
        if (t == kCRC) {
            unsigned char *def = bits(b, a->nfo, 1);
            for (size_t i = 0; def && i < a->nfo; i++) if (def[i]) { a->fo[i].crc = rd32(b); a->fo[i].has_crc = 1; }
            free(def);
        } else { b->bad = 1; return; }
    }
}

static void substreams(Buf *b, Arc *a)
{
    unsigned t = rd8(b);
    if (t == kNumUnPackStream) {
        for (size_t k = 0; k < a->nfo; k++) a->fo[k].nsub = rdnum(b);
        t = rd8(b);
    }
    size_t total = 0;
    for (size_t k = 0; k < a->nfo; k++) {
        if (a->fo[k].nsub > MAX_FILES || total + a->fo[k].nsub > MAX_FILES) { b->bad = 1; return; }
        total += (size_t)a->fo[k].nsub;
    }
    a->nsub = total;
    a->sub_size = calloc(total ? total : 1, sizeof *a->sub_size);
    a->sub_crc = calloc(total ? total : 1, sizeof *a->sub_crc);
    a->sub_has_crc = calloc(total ? total : 1, 1);
    if (!a->sub_size || !a->sub_crc || !a->sub_has_crc) { b->bad = 1; return; }
    size_t s = 0;
    for (size_t k = 0; k < a->nfo; k++) {
        uint64_t left = a->fo[k].unpack;
        for (uint64_t j = 0; j < a->fo[k].nsub; j++, s++) {
            if (j + 1 == a->fo[k].nsub) a->sub_size[s] = left;
            else if (t == kSize) { a->sub_size[s] = rdnum(b); if (a->sub_size[s] > left) { b->bad = 1; return; } left -= a->sub_size[s]; }
            else { b->bad = 1; return; }
        }
        if (a->fo[k].nsub == 1 && a->fo[k].has_crc) { a->sub_crc[s - 1] = a->fo[k].crc; a->sub_has_crc[s - 1] = 1; }
    }
    if (t == kSize) t = rd8(b);
    for (;;) {
        if (b->bad || t == kEnd) break;
        if (t == kCRC) {
            /* digests for the substreams whose CRC is not known from the folder */
            size_t need = 0;
            for (size_t k = 0; k < a->nfo; k++) if (!(a->fo[k].nsub == 1 && a->fo[k].has_crc)) need += (size_t)a->fo[k].nsub;
            unsigned char *def = bits(b, need, 1);
            size_t q = 0, s2 = 0;
            for (size_t k = 0; def && k < a->nfo; k++) {
                if (a->fo[k].nsub == 1 && a->fo[k].has_crc) { s2++; continue; }
                for (uint64_t j = 0; j < a->fo[k].nsub; j++, s2++, q++)
                    if (def[q]) { a->sub_crc[s2] = rd32(b); a->sub_has_crc[s2] = 1; }
            }
            free(def);
        } else { b->bad = 1; return; }
        t = rd8(b);
    }
}

static void streams_info(Buf *b, Arc *a)
{
    for (;;) {
        unsigned t = rd8(b);
        if (b->bad || t == kEnd) break;
        if (t == kPackInfo) pack_info(b, a);
        else if (t == kUnPackInfo) unpack_info(b, a);
        else if (t == kSubStreamsInfo) substreams(b, a);
        else { b->bad = 1; return; }
    }
    if (!a->sub_size && a->fo) {              /* no substream info: one file per folder */
        a->nsub = a->nfo;
        a->sub_size = calloc(a->nfo ? a->nfo : 1, sizeof *a->sub_size);
        a->sub_crc = calloc(a->nfo ? a->nfo : 1, sizeof *a->sub_crc);
        a->sub_has_crc = calloc(a->nfo ? a->nfo : 1, 1);
        if (!a->sub_size || !a->sub_crc || !a->sub_has_crc) { b->bad = 1; return; }
        for (size_t k = 0; k < a->nfo; k++) {
            a->sub_size[k] = a->fo[k].unpack;
            a->sub_crc[k] = a->fo[k].crc;
            a->sub_has_crc[k] = (unsigned char)a->fo[k].has_crc;
        }
    }
    /* where each folder's packed stream is */
    uint64_t off = a->base + a->pack_pos;
    for (size_t k = 0; k < a->nfo; k++) {
        if (k >= a->npack) { b->bad = 1; return; }
        a->fo[k].pack_off = off;
        a->fo[k].pack_size = a->packs[k];
        off += a->packs[k];
    }
}

static void utf16_to_utf8(const unsigned char *p, size_t units, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < units && o + 4 < cap; i++) {
        uint32_t c = (uint32_t)p[2 * i] | (uint32_t)p[2 * i + 1] << 8;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
            uint32_t c2 = (uint32_t)p[2 * i + 2] | (uint32_t)p[2 * i + 3] << 8;
            c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
            i++;
        }
        if (c == '\\') c = '/';
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
        else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 63)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
    }
    out[o] = 0;
}

static void files_info(Buf *b, Arc *a)
{
    uint64_t n = rdnum(b);
    if (n > MAX_FILES) { b->bad = 1; return; }
    a->nfiles = (size_t)n;
    a->empty = calloc(a->nfiles ? a->nfiles : 1, 1);
    a->names = calloc(a->nfiles ? a->nfiles : 1, sizeof *a->names);
    a->empty_file = calloc(a->nfiles ? a->nfiles : 1, 1);
    if (!a->empty || !a->names || !a->empty_file) { b->bad = 1; return; }
    for (;;) {
        unsigned t = rd8(b);
        if (b->bad || t == kEnd) break;
        uint64_t size = rdnum(b);
        if (size > b->n - b->i) { b->bad = 1; return; }
        Buf sub = { b->p + b->i, (size_t)size, 0, 0 };
        if (t == kEmptyStream) {
            unsigned char *v = bits(&sub, a->nfiles, 0);
            if (v) { memcpy(a->empty, v, a->nfiles); free(v); }
        } else if (t == kEmptyFile) {
            size_t ne = 0;
            for (size_t k = 0; k < a->nfiles; k++) ne += a->empty[k];
            unsigned char *v = bits(&sub, ne, 0);
            for (size_t k = 0, q = 0; v && k < a->nfiles; k++) if (a->empty[k]) a->empty_file[k] = v[q++];
            free(v);
        } else if (t == kName) {
            if (rd8(&sub) != 0) { b->bad = 1; return; }          /* external names */
            for (size_t k = 0; k < a->nfiles; k++) {
                size_t start = sub.i, units = 0;
                while (sub.i + 1 < sub.n && (sub.p[sub.i] || sub.p[sub.i + 1])) { sub.i += 2; units++; }
                if (sub.i + 1 >= sub.n) { b->bad = 1; return; }
                utf16_to_utf8(sub.p + start, units, a->names[k], sizeof a->names[k]);
                sub.i += 2;
            }
        }
        if (sub.bad) { b->bad = 1; return; }
        b->i += (size_t)size;                                     /* every other property is skipped */
    }
}

/* ---- decoding a folder ---- */

typedef struct Src { FILE *f; uint64_t left; } Src;
static size_t src_read(void *ctx, unsigned char *buf, size_t cap)
{
    Src *s = ctx;
    if (!s->left) return 0;
    if (cap > s->left) cap = (size_t)s->left;
    size_t n = fread(buf, 1, cap, s->f);
    s->left -= n;
    return n;
}

static int decode_folder(FILE *f, const Folder *fo, JkLzmaOut out, void *octx)
{
    if (fseeko(f, (off_t)fo->pack_off, SEEK_SET) != 0) return JK_LZMA_DAMAGED;
    Src s = { f, fo->pack_size };
    if (fo->mlen == 1 && fo->method[0] == 0x21 && fo->plen == 1)
        return jk_lzma2_decode(fo->props[0], fo->unpack, src_read, &s, out, octx);
    if (fo->mlen == 3 && fo->method[0] == 3 && fo->method[1] == 1 && fo->method[2] == 1 && fo->plen == 5)
        return jk_lzma_decode(fo->props, fo->unpack, src_read, &s, out, octx);
    if (fo->mlen == 1 && fo->method[0] == 0) {
        static unsigned char buf[65536];
        for (uint64_t left = fo->unpack; left;) {
            size_t n = src_read(&s, buf, left > sizeof buf ? sizeof buf : (size_t)left);
            if (!n) return JK_LZMA_DAMAGED;
            if (out(octx, buf, n)) return JK_LZMA_OUT_FAILED;
            left -= n;
        }
        return 0;
    }
    return JK_LZMA_UNSUPPORTED;
}

typedef struct Mem { unsigned char *p; size_t n, cap; } Mem;
static int mem_out(void *ctx, const unsigned char *p, size_t n)
{
    Mem *m = ctx;
    if (m->n + n > m->cap) return 1;
    memcpy(m->p + m->n, p, n);
    m->n += n;
    return 0;
}

/* Streams a folder's output into its files. */
typedef struct Cut {
    const Arc *a; const JkArcOps *ops;
    size_t file;              /* current file index (next with a stream) */
    size_t sub;               /* current substream */
    uint64_t left;            /* bytes left of the current substream */
    int want; JkArcEntry e; uint32_t crc; int failed;
} Cut;

static size_t next_stream_file(const Arc *a, size_t i)
{
    while (i < a->nfiles && a->empty[i]) i++;
    return i;
}

static int cut_begin(Cut *c)
{
    while (c->left == 0 && c->sub < c->a->nsub) {
        /* a zero-size substream still is a file */
        c->file = next_stream_file(c->a, c->file);
        if (c->file >= c->a->nfiles) return -1;
        memset(&c->e, 0, sizeof c->e);
        snprintf(c->e.name, sizeof c->e.name, "%s", c->a->names[c->file]);
        c->e.size = (long long)c->a->sub_size[c->sub];
        c->e.crc = c->a->sub_crc[c->sub];
        c->e.has_crc = c->a->sub_has_crc[c->sub];
        c->want = c->ops->want(c->ops->ctx, &c->e);
        c->crc = 0;
        c->left = c->a->sub_size[c->sub];
        if (c->left == 0) {
            if (c->want && c->ops->done(c->ops->ctx, &c->e, !c->e.has_crc || c->e.crc == 0)) return -1;
            c->sub++;
            c->file++;
            continue;
        }
        break;
    }
    return 0;
}

static int cut_out(void *ctx, const unsigned char *p, size_t n)
{
    Cut *c = ctx;
    while (n) {
        if (c->left == 0 && cut_begin(c) < 0) return 1;
        if (c->left == 0) return 1;                          /* more output than substreams */
        size_t part = n < c->left ? n : (size_t)c->left;
        if (c->want) {
            c->crc = jk_crc32_update(c->crc, p, part);
            if (c->ops->sink(c->ops->ctx, &c->e, p, part)) { c->failed = 1; return 1; }
        }
        c->left -= part;
        p += part;
        n -= part;
        if (c->left == 0) {
            if (c->want && c->ops->done(c->ops->ctx, &c->e, !c->e.has_crc || c->crc == c->e.crc)) { c->failed = 1; return 1; }
            c->sub++;
            c->file++;
        }
    }
    return 0;
}

static void arc_free(Arc *a)
{
    free(a->fo); free(a->packs); free(a->sub_size); free(a->sub_crc); free(a->sub_has_crc);
    free(a->empty); free(a->empty_file); free(a->names);
    memset(a, 0, sizeof *a);
}

static int say(char *why, size_t cap, const char *t) { if (why && cap) snprintf(why, cap, "%s", t); return -1; }

static int run7z(const char *path, const JkArcOps *ops, int list, char *why, size_t cap)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f || !ops) { if (f) fclose(f); return say(why, cap, "cannot open the archive"); }
    static Arc a;
    memset(&a, 0, sizeof a);
    a.f = f;
    a.base = 32;
    int rc = -1;
    unsigned char sig[32];
    unsigned char *hdr = NULL;
    if (fread(sig, 1, 32, f) != 32 || memcmp(sig, "7z\xBC\xAF\x27\x1C", 6)) { say(why, cap, "not a 7z archive"); goto done; }
    if (jk_crc32(sig + 12, 20) != (uint32_t)(sig[8] | sig[9] << 8 | sig[10] << 16 | (uint32_t)sig[11] << 24)) { say(why, cap, "damaged 7z archive (start header)"); goto done; }
    uint64_t noff = 0, nsize = 0;
    for (int i = 7; i >= 0; i--) { noff = (noff << 8) | sig[12 + i]; nsize = (nsize << 8) | sig[20 + i]; }
    uint32_t ncrc = (uint32_t)(sig[28] | sig[29] << 8 | sig[30] << 16 | (uint32_t)sig[31] << 24);
    if (nsize == 0 || nsize > HEADER_MAX) { say(why, cap, "damaged 7z archive (header size)"); goto done; }
    hdr = malloc((size_t)nsize);
    if (!hdr || fseeko(f, (off_t)(32 + noff), SEEK_SET) != 0 || fread(hdr, 1, (size_t)nsize, f) != nsize
        || jk_crc32(hdr, (size_t)nsize) != ncrc) { say(why, cap, "damaged 7z archive (header)"); goto done; }
    size_t hlen = (size_t)nsize;
    /* an encoded header is itself a packed stream: decode it first */
    while (hlen && hdr[0] == kEncodedHeader) {
        Buf b = { hdr, hlen, 1, 0 };
        streams_info(&b, &a);
        if (b.bad || a.nfo != 1 || a.fo[0].unpack > HEADER_MAX) { say(why, cap, b.bad == 2 ? "unsupported 7z coder" : "damaged 7z header"); goto done; }
        Mem m = { malloc((size_t)a.fo[0].unpack + 1), 0, (size_t)a.fo[0].unpack };
        if (!m.p) { say(why, cap, "out of memory"); goto done; }
        int r = decode_folder(f, &a.fo[0], mem_out, &m);
        if (r || m.n != a.fo[0].unpack || (a.fo[0].has_crc && jk_crc32(m.p, m.n) != a.fo[0].crc)) {
            free(m.p);
            say(why, cap, r == JK_LZMA_UNSUPPORTED ? "unsupported 7z coder" : "damaged 7z header");
            goto done;
        }
        free(hdr);
        hdr = m.p;
        hlen = m.n;
        arc_free(&a);
        a.f = f;
        a.base = 32;
    }
    Buf b = { hdr, hlen, 0, 0 };
    if (rd8(&b) != kHeader) { say(why, cap, "damaged 7z header"); goto done; }
    for (;;) {
        unsigned t = rd8(&b);
        if (b.bad || t == kEnd) break;
        if (t == kArchiveProperties) { for (;;) { unsigned p = rd8(&b); if (b.bad || p == 0) break; skip(&b, rdnum(&b)); } }
        else if (t == kAdditionalStreamsInfo) { b.bad = 1; }
        else if (t == kMainStreamsInfo) streams_info(&b, &a);
        else if (t == kFilesInfo) files_info(&b, &a);
        else b.bad = 1;
    }
    if (b.bad) { say(why, cap, b.bad == 2 ? "unsupported 7z coder (only LZMA, LZMA2, copy)" : "damaged 7z header"); goto done; }
    /* files without a stream (empty files, folders) first, as entries of size 0 */
    for (size_t i = 0; i < a.nfiles; i++) {
        if (!a.empty[i] || !a.empty_file[i]) continue;
        JkArcEntry e;
        memset(&e, 0, sizeof e);
        snprintf(e.name, sizeof e.name, "%s", a.names[i]);
        if (ops->want(ops->ctx, &e) && !list && ops->done(ops->ctx, &e, 1)) { say(why, cap, "stopped"); goto done; }
    }
    if (list) {                                   /* names and sizes only: nothing is decoded */
        size_t file = 0;
        for (size_t s = 0; s < a.nsub; s++, file++) {
            file = next_stream_file(&a, file);
            if (file >= a.nfiles) { say(why, cap, "damaged 7z archive (files)"); goto done; }
            JkArcEntry e;
            memset(&e, 0, sizeof e);
            snprintf(e.name, sizeof e.name, "%s", a.names[file]);
            e.size = (long long)a.sub_size[s];
            e.crc = a.sub_crc[s];
            e.has_crc = a.sub_has_crc[s];
            ops->want(ops->ctx, &e);
        }
        rc = 0;
        goto done;
    }
    Cut c;
    memset(&c, 0, sizeof c);
    c.a = &a;
    c.ops = ops;
    for (size_t k = 0; k < a.nfo; k++) {
        if (c.left == 0 && cut_begin(&c) < 0) { say(why, cap, "damaged 7z archive (files)"); goto done; }
        int r = decode_folder(f, &a.fo[k], cut_out, &c);
        if (c.failed) { say(why, cap, "stopped"); goto done; }
        if (r) { say(why, cap, r == JK_LZMA_UNSUPPORTED ? "unsupported 7z coder" : r == JK_LZMA_NOMEM ? "out of memory" : "damaged 7z data"); goto done; }
    }
    rc = 0;
done:
    free(hdr);
    arc_free(&a);
    fclose(f);
    return rc;
}

JkArcType jk_archive_type(const char *path)
{
    unsigned char m[8] = { 0 };
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return JK_ARC_UNKNOWN;
    size_t n = fread(m, 1, sizeof m, f);
    fclose(f);
    if (n >= 4 && m[0] == 'P' && m[1] == 'K' && (m[2] == 3 || m[2] == 5) && (m[3] == 4 || m[3] == 6)) return JK_ARC_ZIP;
    if (n >= 6 && !memcmp(m, "7z\xBC\xAF\x27\x1C", 6)) return JK_ARC_7Z;
    if (n >= 4 && !memcmp(m, "Rar!", 4)) return JK_ARC_RAR;
    return JK_ARC_UNKNOWN;
}

int jk_7z_extract(const char *path, const JkArcOps *ops, char *why, size_t cap) { return run7z(path, ops, 0, why, cap); }

typedef struct ListCtx { int (*cb)(void *, const JkArcEntry *); void *ctx; } ListCtx;
static int list_want(void *ctx, const JkArcEntry *e) { ListCtx *l = ctx; l->cb(l->ctx, e); return 0; }
static int list_sink(void *ctx, const JkArcEntry *e, const unsigned char *p, size_t n) { (void)ctx; (void)e; (void)p; (void)n; return 1; }
static int list_done(void *ctx, const JkArcEntry *e, int ok) { (void)ctx; (void)e; (void)ok; return 0; }

int jk_archive_list(const char *path, int (*cb)(void *ctx, const JkArcEntry *e), void *ctx, char *why, size_t cap)
{
    ListCtx l = { cb, ctx };
    JkArcOps ops = { &l, list_want, list_sink, list_done };
    switch (jk_archive_type(path)) {
    case JK_ARC_ZIP: return jk_zip_extract(path, &ops, why, cap);     /* want() says no to all: only the directory is read */
    case JK_ARC_7Z: return run7z(path, &ops, 1, why, cap);
    case JK_ARC_RAR: return say(why, cap, "RAR archives are not supported: use the author's zip or 7z");
    default: return say(why, cap, "not an archive JK4SPEED can read (zip or 7z)");
    }
}

int jk_archive_extract(const char *path, const JkArcOps *ops, char *why, size_t cap)
{
    switch (jk_archive_type(path)) {
    case JK_ARC_ZIP: return jk_zip_extract(path, ops, why, cap);
    case JK_ARC_7Z: return jk_7z_extract(path, ops, why, cap);
    case JK_ARC_RAR: return say(why, cap, "RAR archives are not supported: use the author's zip or 7z");
    default: return say(why, cap, "not an archive JK4SPEED can read (zip or 7z)");
    }
}
