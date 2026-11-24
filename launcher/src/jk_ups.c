/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* UPS applier. See jk_ups.h. */
#include "jk_ups.h"
#include "jk_fs.h"
#include "jk_inflate.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PATCH_MAX (64u << 20)

typedef struct P { const unsigned char *p; size_t n, i; int bad; } P;

static uint64_t vread(P *p)
{
    uint64_t data = 0, shift = 1;
    for (int k = 0; k < 10; k++) {
        if (p->i >= p->n) { p->bad = 1; return 0; }
        unsigned x = p->p[p->i++];
        data += (x & 0x7f) * shift;
        if (x & 0x80) return data;
        shift <<= 7;
        data += shift;
    }
    p->bad = 1;
    return 0;
}

static uint32_t le32(const unsigned char *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

typedef struct W { int fd; uint32_t crc; uint64_t n; unsigned char buf[65536]; size_t len; int bad; } W;
static void wput(W *w, unsigned char b)
{
    w->buf[w->len++] = b;
    if (w->len == sizeof w->buf) {
        w->crc = jk_crc32_update(w->crc, w->buf, w->len);
        for (size_t o = 0; o < w->len;) {
            ssize_t k = write(w->fd, w->buf + o, w->len - o);
            if (k <= 0) { w->bad = 1; break; }
            o += (size_t)k;
        }
        w->len = 0;
    }
    w->n++;
}
static void wflush(W *w)
{
    w->crc = jk_crc32_update(w->crc, w->buf, w->len);
    for (size_t o = 0; o < w->len;) {
        ssize_t k = write(w->fd, w->buf + o, w->len - o);
        if (k <= 0) { w->bad = 1; break; }
        o += (size_t)k;
    }
    w->len = 0;
}

typedef struct R { FILE *f; uint32_t crc; uint64_t n, size; unsigned char buf[65536]; size_t pos, len; } R;
static int rget(R *r)
{
    if (r->n >= r->size) return 0;                     /* past the input: zeros */
    if (r->pos == r->len) {
        r->len = fread(r->buf, 1, sizeof r->buf, r->f);
        r->pos = 0;
        if (!r->len) return -1;
        r->crc = jk_crc32_update(r->crc, r->buf, r->len);
    }
    r->n++;
    return r->buf[r->pos++];
}

static int fail(char *why, size_t cap, const char *t) { if (why && cap) snprintf(why, cap, "%s", t); return -1; }

int jk_ups_apply(const char *patch, const char *input, const char *out, char *why, size_t cap)
{
    size_t pn = 0;
    unsigned char *pd = (unsigned char *)jk_fs_read(patch, PATCH_MAX, &pn);
    if (!pd) return fail(why, cap, "the patch is missing");
    int rc = -1;
    static W w;
    static R r;
    memset(&w, 0, sizeof w);
    memset(&r, 0, sizeof r);
    w.fd = -1;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.jk4new", out ? out : "");
    if (pn < 4 + 12 || memcmp(pd, "UPS1", 4) || jk_crc32(pd, pn - 4) != le32(pd + pn - 4)) { fail(why, cap, "the patch is damaged"); goto done; }
    P p = { pd, pn - 12, 4, 0 };
    uint64_t in_size = vread(&p), out_size = vread(&p);
    long long isz = input ? jk_fs_size(input) : -1;
    if (p.bad || isz < 0 || (uint64_t)isz != in_size) { fail(why, cap, "the file to patch is not the expected one"); goto done; }
    r.f = fopen(input, "rb");
    r.size = in_size;
    if (!r.f || !out || jk_fs_mkdir_parent(out) < 0) { fail(why, cap, "cannot read or write"); goto done; }
    unlink(tmp);
    w.fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (w.fd < 0) { fail(why, cap, "cannot write the result"); goto done; }
    while (p.i < p.n) {
        uint64_t skip = vread(&p);
        if (p.bad) { fail(why, cap, "the patch is damaged"); goto done; }
        if (skip > out_size + r.size) { fail(why, cap, "the patch is damaged"); goto done; }
        while (skip--) {
            int b = rget(&r);
            if (b < 0) { fail(why, cap, "cannot read the file to patch"); goto done; }
            if (w.n < out_size) wput(&w, (unsigned char)b);   /* past the output: dropped, as byuu's applier does */
        }
        for (;;) {
            if (p.i >= p.n) { fail(why, cap, "the patch is damaged"); goto done; }
            unsigned x = p.p[p.i++];
            int b = rget(&r);
            if (b < 0) { fail(why, cap, "cannot read the file to patch"); goto done; }
            if (w.n < out_size) wput(&w, (unsigned char)(b ^ (int)x));
            if (x == 0) break;
        }
    }
    while (w.n < out_size) {
        int b = rget(&r);
        if (b < 0) { fail(why, cap, "cannot read the file to patch"); goto done; }
        wput(&w, (unsigned char)b);
    }
    while (r.n < r.size) if (rget(&r) < 0) break;                       /* the rest of the input, for its CRC */
    wflush(&w);
    if (w.bad) { fail(why, cap, "the result could not be written (disk full?)"); goto done; }
    if (r.crc != le32(pd + pn - 12)) { fail(why, cap, "the file to patch is not the expected one"); goto done; }
    if (w.crc != le32(pd + pn - 8)) { fail(why, cap, "the patched result does not match"); goto done; }
    if (fsync(w.fd) != 0 || close(w.fd) != 0) { w.fd = -1; fail(why, cap, "the result could not be written"); goto done; }
    w.fd = -1;
    if (jk_fs_rename(tmp, out) < 0) { fail(why, cap, "the result could not be put in place"); goto done; }
    rc = 0;
done:
    if (w.fd >= 0) close(w.fd);
    if (rc != 0 && out) unlink(tmp);
    if (r.f) fclose(r.f);
    free(pd);
    return rc;
}
