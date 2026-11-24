/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* JKD1 delta applier. See jk_delta.h and tools/make_delta.py (the format). */
#include "jk_delta.h"
#include "jk_fs.h"
#include "jk_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHUNK (1 << 20)

static void hex(const unsigned char *b, char out[65])
{
    static const char d[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 15]; }
    out[64] = 0;
}

static uint64_t le(const unsigned char *p, int n)
{
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int read_exact(FILE *f, void *buf, size_t n) { return fread(buf, 1, n, f) == n ? 0 : -1; }

static int read_header(FILE *f, JkDeltaInfo *in)
{
    unsigned char h[40];
    memset(in, 0, sizeof *in);
    if (read_exact(f, h, 8) < 0 || memcmp(h, "JKD1", 4)) return -1;
    uint64_t n = le(h + 4, 4);
    if (n < 1 || n > JK_DELTA_MAX_SRC) return -1;
    in->nsrc = (int)n;
    if (read_exact(f, h, 40) < 0) return -1;
    hex(h, in->target_sha);
    in->target_size = (long long)le(h + 32, 8);
    for (int i = 0; i < in->nsrc; i++) {
        if (read_exact(f, h, 40) < 0) return -1;
        hex(h, in->src_sha[i]);
        in->src_size[i] = (long long)le(h + 32, 8);
    }
    return in->target_size >= 0 ? 0 : -1;
}

int jk_delta_info(const char *delta_path, JkDeltaInfo *out)
{
    FILE *f = delta_path ? fopen(delta_path, "rb") : NULL;
    if (!f || !out) { if (f) fclose(f); return -1; }
    int r = read_header(f, out);
    fclose(f);
    return r;
}

static int fail(char *why, size_t cap, const char *text)
{
    if (why && cap) snprintf(why, cap, "%s", text);
    return -1;
}

/* ---- JDLZ, exactly as tools/make_delta.py jdlz() (the JDLZ-safe encoder of
 * evidence/codex-upscaled-hud/gauges/indexed/build.py, with the trailing
 * refill bytes that fixed the Golf GTI race-load crash). ---- */

typedef struct Hist { uint32_t *key; int32_t *pos; size_t mask; } Hist;

static int hist_init(Hist *h, size_t n)
{
    size_t cap = 16;
    while (cap < 2 * n + 16) cap <<= 1;
    h->key = calloc(cap, sizeof *h->key);
    h->pos = malloc(cap * sizeof *h->pos);
    h->mask = cap - 1;
    return h->key && h->pos ? 0 : -1;
}
static void hist_free(Hist *h) { free(h->key); free(h->pos); }
static size_t hist_slot(const Hist *h, uint32_t k)
{
    size_t i = (k * 2654435761u) & h->mask;
    while (h->key[i] && h->key[i] != k) i = (i + 1) & h->mask;
    return i;
}
/* key = 3-byte prefix + 1 (0 marks an empty slot) */
static void hist_put(Hist *h, const unsigned char *d, size_t i, size_t n)
{
    if (i + 3 > n) return;                       /* short prefixes never match */
    uint32_t k = ((uint32_t)d[i] | (uint32_t)d[i + 1] << 8 | (uint32_t)d[i + 2] << 16) + 1;
    size_t s = hist_slot(h, k);
    h->key[s] = k;
    h->pos[s] = (int32_t)i;
}
static long hist_get(const Hist *h, const unsigned char *d, size_t i, size_t n)
{
    if (i + 3 > n) return -4096;
    uint32_t k = ((uint32_t)d[i] | (uint32_t)d[i + 1] << 8 | (uint32_t)d[i + 2] << 16) + 1;
    size_t s = hist_slot(h, k);
    return h->key[s] ? h->pos[s] : -4096;
}

/* *out (malloc'd) gets the JDLZ stream of d[0..n). 0 ok. */
static int jdlz_encode(const unsigned char *d, size_t n, unsigned char **out, size_t *out_n)
{
    size_t cap = 16 + n + n / 4 + 64, len = 16;
    unsigned char *b = malloc(cap);
    Hist h;
    if (!b || n > 0x7fffffff || hist_init(&h, n) < 0) { free(b); return -1; }
    size_t pos = 0, first = 0, second = 0;
    unsigned long literals = 0, matches = 0;
    int refill = 0;
    while (pos < n) {
        if (len + 8 > cap) {
            unsigned char *q = realloc(b, cap *= 2);
            if (!q) { free(b); hist_free(&h); return -1; }
            b = q;
        }
        if (literals % 8 == 0) { first = len; b[len++] = 0; }
        if (matches % 8 == 0 && (pos == 0 || refill)) { second = len; b[len++] = 0; }
        refill = 0;
        long prev = hist_get(&h, d, pos, n);
        long distance = (long)pos - prev;
        size_t length = 1;
        if (distance <= 2064 && pos + 3 <= n) {
            size_t limit = n - pos;
            size_t most = distance <= 16 ? 4098 : 34;
            if (limit > most) limit = most;
            length = 3;
            while (length < limit && d[pos + length] == d[pos + length - (size_t)distance]) length++;
            b[first] |= (unsigned char)(1u << (literals % 8));
            size_t count = length - 3;
            if (distance <= 16) {
                b[second] |= (unsigned char)(1u << (matches % 8));
                b[len++] = (unsigned char)(((count >> 4) & 0xf0) | (size_t)(distance - 1));
                b[len++] = (unsigned char)(count & 255);
            } else {
                size_t span = (size_t)distance - 17;
                b[len++] = (unsigned char)(((span >> 3) & 0xe0) | count);
                b[len++] = (unsigned char)(span & 255);
            }
            matches++;
            refill = matches % 8 == 0;
        } else {
            b[len++] = d[pos];
        }
        for (size_t i = pos; i < pos + length; i++) hist_put(&h, d, i, n);
        pos += length;
        literals++;
    }
    hist_free(&h);
    if (len + 2 > cap) {
        unsigned char *q = realloc(b, cap + 2);
        if (!q) { free(b); return -1; }
        b = q;
    }
    if (literals % 8 == 0) b[len++] = 0;
    if (refill) b[len++] = 0;
    memcpy(b, "JDLZ\x02\x10\0\0", 8);
    uint32_t raw = (uint32_t)n, total = (uint32_t)len;
    for (int i = 0; i < 4; i++) { b[8 + i] = (unsigned char)(raw >> (8 * i)); b[12 + i] = (unsigned char)(total >> (8 * i)); }
    *out = b;
    *out_n = len;
    return 0;
}

/* ---- applying ---- */

typedef struct Out { int fd; JkSha256 h; long long written, limit; } Out;

static int emit(Out *o, const unsigned char *p, size_t n)
{
    if (o->written + (long long)n > o->limit) return -1;
    jk_sha256_update(&o->h, p, n);
    for (size_t w = 0; w < n;) {
        ssize_t k = write(o->fd, p + w, n - w);
        if (k <= 0) return -2;
        w += (size_t)k;
    }
    o->written += (long long)n;
    return 0;
}

/* Reads one COPY (1) or ADD (2) op body and gives its bytes to sink: either
 * streamed to the output (acc == NULL) or appended to acc. 0 ok, -1 damaged,
 * -2 write failed. */
typedef struct Acc { unsigned char *p; size_t n, cap, max; } Acc;
static int one_op(int kind, FILE *df, FILE *const *sf, const JkDeltaInfo *in, int n, unsigned char *buf, Out *o, Acc *acc)
{
    unsigned char op[13];
    uint64_t len;
    FILE *from;
    if (kind == 1) {
        if (read_exact(df, op, 13) < 0) return -1;
        int si = op[0];
        uint64_t off = le(op + 1, 8);
        len = le(op + 9, 4);
        if (si >= n || off > (uint64_t)in->src_size[si] || len > (uint64_t)in->src_size[si] - off
            || fseeko(sf[si], (off_t)off, SEEK_SET) != 0)
            return -1;
        from = sf[si];
    } else if (kind == 2) {
        if (read_exact(df, op, 4) < 0) return -1;
        len = le(op, 4);
        from = df;
    } else {
        return -1;
    }
    while (len) {
        size_t part = len > CHUNK ? CHUNK : (size_t)len;
        if (read_exact(from, buf, part) < 0) return -1;
        if (acc) {
            if (acc->n + part > acc->max) return -1;
            if (acc->n + part > acc->cap) {
                size_t cap = acc->cap ? acc->cap : 1 << 16;
                while (cap < acc->n + part) cap *= 2;
                unsigned char *q = realloc(acc->p, cap);
                if (!q) return -2;
                acc->p = q;
                acc->cap = cap;
            }
            memcpy(acc->p + acc->n, buf, part);
            acc->n += part;
        } else {
            int r = emit(o, buf, part);
            if (r < 0) return r;
        }
        len -= part;
    }
    return 0;
}

int jk_delta_apply(const char *delta_path, const char *const *src, int n, const char *out, char *why, size_t cap)
{
    char dummy[8];
    if (!why || !cap) { why = dummy; cap = sizeof dummy; }
    why[0] = 0;
    static JkDeltaInfo in;
    FILE *df = delta_path ? fopen(delta_path, "rb") : NULL;
    if (!df) return fail(why, cap, "the delta is missing");
    if (read_header(df, &in) < 0) { fclose(df); return fail(why, cap, "the delta is damaged"); }
    if (n != in.nsrc || !src || !out) { fclose(df); return fail(why, cap, "wrong number of source files"); }
    static FILE *sf[JK_DELTA_MAX_SRC];
    memset(sf, 0, sizeof sf);
    for (int i = 0; i < n; i++) {
        char sha[65];
        long long size = -1;
        if (!src[i] || jk_fs_sha256(src[i], sha, &size) < 0 || size != in.src_size[i] || strcmp(sha, in.src_sha[i])) {
            fclose(df);
            for (int k = 0; k < i; k++) if (sf[k]) fclose(sf[k]);
            return fail(why, cap, "a source file is not the expected version");
        }
        sf[i] = fopen(src[i], "rb");
        if (!sf[i]) {
            fclose(df);
            for (int k = 0; k < i; k++) fclose(sf[k]);
            return fail(why, cap, "a source file could not be read");
        }
    }
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.jk4new", out);
    unsigned char *buf = malloc(CHUNK);
    int rc = -1;
    Out o;
    memset(&o, 0, sizeof o);
    o.fd = -1;
    o.limit = in.target_size;
    Acc acc = { NULL, 0, 0, 64u << 20 };          /* one JDLZ record: well under 64 MB */
    if (!buf || jk_fs_mkdir_parent(out) < 0) { fail(why, cap, "out of memory or no folder"); goto done; }
    unlink(tmp);
    o.fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (o.fd < 0) { fail(why, cap, "the result could not be written"); goto done; }
    jk_sha256_init(&o.h);
    for (;;) {
        int kind = fgetc(df), r;
        if (kind == 0) break;
        if (kind == 3) {
            unsigned char nb[4];
            if (read_exact(df, nb, 4) < 0) { fail(why, cap, "the delta is damaged"); goto done; }
            uint64_t parts = le(nb, 4);
            acc.n = 0;
            for (uint64_t k = 0; k < parts; k++) {
                int pk = fgetc(df);
                if ((r = one_op(pk, df, sf, &in, n, buf, &o, &acc)) < 0) {
                    fail(why, cap, r == -2 ? "out of memory" : "the delta is damaged");
                    goto done;
                }
            }
            unsigned char *z = NULL;
            size_t zn = 0;
            if (jdlz_encode(acc.p ? acc.p : (const unsigned char *)"", acc.n, &z, &zn) < 0) { fail(why, cap, "out of memory"); goto done; }
            r = emit(&o, z, zn);
            free(z);
        } else {
            r = one_op(kind, df, sf, &in, n, buf, &o, NULL);
        }
        if (r < 0) {
            fail(why, cap, r == -2 ? "the result could not be written (disk full?)" : "the delta is damaged");
            goto done;
        }
    }
    if (fgetc(df) != EOF) { fail(why, cap, "the delta is damaged"); goto done; }
    char got[65];
    jk_sha256_final_hex(&o.h, got);
    if (o.written != in.target_size || strcmp(got, in.target_sha)) { fail(why, cap, "the result does not match"); goto done; }
    if (fsync(o.fd) != 0 || close(o.fd) != 0) { o.fd = -1; fail(why, cap, "the result could not be written"); goto done; }
    o.fd = -1;
    if (jk_fs_rename(tmp, out) < 0) { fail(why, cap, "the result could not be put in place"); goto done; }
    rc = 0;
done:
    if (o.fd >= 0) close(o.fd);
    if (rc != 0) unlink(tmp);
    free(buf);
    free(acc.p);
    fclose(df);
    for (int i = 0; i < n; i++) if (sf[i]) fclose(sf[i]);
    return rc;
}
