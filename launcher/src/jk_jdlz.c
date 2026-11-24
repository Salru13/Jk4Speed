/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Game-exact JDLZ counter check. See jk_jdlz.h. */
#include "jk_jdlz.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STREAM_MAX (64u << 20)
#define STEPS_MAX 200000000ll

static uint32_t le32(const unsigned char *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

int jk_jdlz_check(const unsigned char *d, size_t n)
{
    if (n < 0x12 || memcmp(d, "JDLZ", 4) || d[4] != 2) return JK_JDLZ_NOT;
#define B(i) ((size_t)(i) < n ? d[(size_t)(i)] : 0u)
    uint32_t out_len = le32(d + 8), comp = le32(d + 12);
    uint64_t p = 0x12, op = 0;
    unsigned f1 = d[0x10] | 0x100u, f2 = d[0x11] | 0x100u;
    uint32_t ebx = comp - 0x12u;
    if (ebx == 0) return JK_JDLZ_EMPTY;
    for (long long steps = 1;; steps++) {
        if (steps > STEPS_MAX) return JK_JDLZ_RUNAWAY;
        if (f1 & 1) {
            unsigned b0 = B(p), b1 = B(p + 1);
            uint64_t ln = (f2 & 1) ? ((((b0 & 0xf0u) << 4) | b1) + 3) : ((b0 & 0x1fu) + 3);
            f2 >>= 1;
            p += 2;
            ebx -= 2;
            op += ln;
        } else {
            if (op < out_len) { op++; p++; }
            ebx -= 1;
        }
        f1 >>= 1;
        if (f1 == 1) { f1 = B(p) | 0x100u; p++; ebx -= 1; }
        if (f2 == 1) { f2 = B(p) | 0x100u; p++; ebx -= 1; }
        if (ebx == 0) break;
        if (ebx >= 0x80000000u) return JK_JDLZ_UNDERFLOW;
    }
#undef B
    /* The counter ended exactly on 0. EA's own streams (InGameB.lzc, the GTI
     * TEXTURES.BIN blobs) stop with the cursor one byte before hdr.comp; the
     * game accepts that. What it does not survive is a counter that skips 0
     * or a wrong unpacked size. */
    (void)p;
    return op == out_len ? JK_JDLZ_OK : JK_JDLZ_MISMATCH;
}

int jk_jdlz_check_file(const char *path, long long *count, long long *bad_off, char *why, size_t cap)
{
    if (count) *count = 0;
    if (bad_off) *bad_off = -1;
    FILE *f = fopen(path, "rb");
    if (!f) { if (why && cap) snprintf(why, cap, "cannot read the file"); return -1; }
    static unsigned char win[1 << 20];
    unsigned char *s = NULL;
    long long base = 0;                       /* file offset of win[0] */
    size_t have = 0;
    int rc = 0;
    for (;;) {
        size_t got = fread(win + have, 1, sizeof win - have, f);
        have += got;
        if (have < 5) break;
        size_t limit = got ? have - 4 : have;   /* keep 4 bytes for a header across the edge */
        long long next = -1;                    /* set when a stream was checked: scan on after it */
        for (size_t i = 0; i + 5 <= have && i < limit; i++) {
            if (win[i] != 'J' || memcmp(win + i, "JDLZ\2", 5)) continue;
            long long at = base + (long long)i;
            unsigned char hdr[0x12];
            memset(hdr, 0, sizeof hdr);
            if (fseeko(f, at, SEEK_SET) != 0 || fread(hdr, 1, sizeof hdr, f) == 0) { rc = -1; goto done; }
            uint32_t comp = le32(hdr + 12);
            size_t want = (comp < 0x12 ? 0x12 : comp) + 16u;  /* a bad stream reads on: keep the next bytes */
            if (want > STREAM_MAX) want = STREAM_MAX;
            free(s);
            s = malloc(want);
            if (!s) { if (why && cap) snprintf(why, cap, "out of memory"); rc = -1; goto done; }
            fseeko(f, at, SEEK_SET);
            size_t sn = fread(s, 1, want, f);
            int r = jk_jdlz_check(s, sn);
            if (count) (*count)++;
            if (r < 0) {
                if (bad_off) *bad_off = at;
                if (why && cap)
                    snprintf(why, cap, "compressed data at 0x%llx would crash the game (%s)", at,
                             r == JK_JDLZ_UNDERFLOW ? "counter skips 0" : r == JK_JDLZ_MISMATCH ? "size mismatch" : "runaway");
                rc = -1;
                goto done;
            }
            next = at + (comp < 0x12 ? 0x12 : (long long)comp);   /* the stream's own bytes are data, not headers */
            break;
        }
        if (next >= 0) {
            if (fseeko(f, next, SEEK_SET) != 0) { rc = -1; goto done; }
            base = next;
            have = 0;
            continue;
        }
        if (!got) break;
        size_t keep = have - limit;
        memmove(win, win + limit, keep);
        base += (long long)limit;
        have = keep;
    }
done:
    free(s);
    fclose(f);
    return rc;
}
