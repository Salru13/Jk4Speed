/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Test helpers for the mod set: a stored zip writer, a UPS encoder and a
 * JKD1 writer (copies of whole sources plus literal bytes). */
#ifndef JK_FX_H
#define JK_FX_H

#include "jk_inflate.h"
#include "jk_sha256.h"

#include <stdio.h>
#include <string.h>

static __attribute__((unused)) void fx_put32(unsigned char *o, unsigned v) { for (int i = 0; i < 4; i++) o[i] = (unsigned char)(v >> (8 * i)); }
static __attribute__((unused)) void fx_put64(unsigned char *o, unsigned long long v) { for (int i = 0; i < 8; i++) o[i] = (unsigned char)(v >> (8 * i)); }

typedef struct FxEnt { const char *name; const unsigned char *data; size_t size; } FxEnt;

/* A stored zip, as a PC tool or a folder download makes it. */
static __attribute__((unused)) void fx_zip(const char *path, const FxEnt *e, int n)
{
    FILE *f = fopen(path, "wb");
    static unsigned char cd[65536];
    size_t cn = 0, off = 0;
    for (int i = 0; i < n; i++) {
        unsigned char h[30] = { 'P', 'K', 3, 4, 20 };
        unsigned crc = jk_crc32(e[i].data, e[i].size);
        size_t nl = strlen(e[i].name);
        fx_put32(h + 14, crc); fx_put32(h + 18, (unsigned)e[i].size); fx_put32(h + 22, (unsigned)e[i].size);
        h[26] = (unsigned char)nl;
        fwrite(h, 1, 30, f); fwrite(e[i].name, 1, nl, f); fwrite(e[i].data, 1, e[i].size, f);
        unsigned char c[46] = { 'P', 'K', 1, 2, 20, 0, 20 };
        fx_put32(c + 16, crc); fx_put32(c + 20, (unsigned)e[i].size); fx_put32(c + 24, (unsigned)e[i].size);
        c[28] = (unsigned char)nl;
        fx_put32(c + 42, (unsigned)off);
        memcpy(cd + cn, c, 46); memcpy(cd + cn + 46, e[i].name, nl);
        cn += 46 + nl;
        off += 30 + nl + e[i].size;
    }
    unsigned char end[22] = { 'P', 'K', 5, 6 };
    end[8] = end[10] = (unsigned char)n;
    fx_put32(end + 12, (unsigned)cn); fx_put32(end + 16, (unsigned)off);
    fwrite(cd, 1, cn, f); fwrite(end, 1, 22, f);
    fclose(f);
}

static __attribute__((unused)) size_t fx_vput(unsigned char *o, unsigned long long v)
{
    size_t n = 0;
    for (;;) {
        unsigned x = v & 0x7f;
        v >>= 7;
        if (!v) { o[n++] = (unsigned char)(0x80 | x); return n; }
        o[n++] = (unsigned char)x;
        v--;
    }
}

/* UPS in byuu's layout. */
static __attribute__((unused)) size_t fx_ups(const unsigned char *a, size_t an, const unsigned char *b, size_t bn, unsigned char *o)
{
    size_t n = 4, last = 0, max = an > bn ? an : bn;
    memcpy(o, "UPS1", 4);
    n += fx_vput(o + n, an);
    n += fx_vput(o + n, bn);
    for (size_t i = 0; i < max;) {
        unsigned x = (i < an ? a[i] : 0) ^ (i < bn ? b[i] : 0);
        if (!x) { i++; continue; }
        n += fx_vput(o + n, i - last);
        while (i < max) {
            x = (i < an ? a[i] : 0) ^ (i < bn ? b[i] : 0);
            if (!x) break;
            o[n++] = (unsigned char)x;
            i++;
        }
        o[n++] = 0;
        i++;
        last = i;
    }
    fx_put32(o + n, jk_crc32(a, an));
    fx_put32(o + n + 4, jk_crc32(b, bn));
    n += 8;
    fx_put32(o + n, jk_crc32(o, n));
    return n + 4;
}

static __attribute__((unused)) void fx_rawsha(const unsigned char *d, size_t n, unsigned char out[32])
{
    char h[65];
    jk_sha256_hex(d, n, h);
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (unsigned char)v; }
}

/* JKD1: target = src[0] || src[1] || ... || lit. Writes the delta; out_target
 * (may be NULL) gets the target bytes. Returns the target size. */
static __attribute__((unused)) size_t fx_delta(const char *path, const FxEnt *src, int n, const char *lit, unsigned char *out_target)
{
    static unsigned char t[1 << 20];
    size_t tn = 0;
    for (int i = 0; i < n; i++) { memcpy(t + tn, src[i].data, src[i].size); tn += src[i].size; }
    memcpy(t + tn, lit, strlen(lit));
    tn += strlen(lit);
    if (out_target) memcpy(out_target, t, tn);
    FILE *f = fopen(path, "wb");
    unsigned char h[40];
    fwrite("JKD1", 1, 4, f);
    fx_put32(h, (unsigned)n); fwrite(h, 1, 4, f);
    fx_rawsha(t, tn, h); fx_put64(h + 32, tn); fwrite(h, 1, 40, f);
    for (int i = 0; i < n; i++) { fx_rawsha(src[i].data, src[i].size, h); fx_put64(h + 32, src[i].size); fwrite(h, 1, 40, f); }
    for (int i = 0; i < n; i++) {
        unsigned char op[14] = { 1, (unsigned char)i };
        fx_put64(op + 2, 0); fx_put32(op + 10, (unsigned)src[i].size);
        fwrite(op, 1, 14, f);
    }
    unsigned char add[5] = { 2 };
    fx_put32(add + 1, (unsigned)strlen(lit));
    fwrite(add, 1, 5, f); fwrite(lit, 1, strlen(lit), f);
    fputc(0, f);
    fclose(f);
    return tn;
}

#endif
