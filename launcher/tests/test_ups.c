/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* UPS patches: same size, grown and shrunk outputs; every CRC in the footer
 * checked; a wrong input, a damaged patch or a truncated patch leaves nothing
 * at the output path. The real X360 Stuff Pack road patch is checked
 * separately against the 532 MB road file (docs/launcher/MODS.md). */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_inflate.h"
#include "jk_ups.h"

static size_t vput(unsigned char *o, unsigned long long v)
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
static void put32(unsigned char *o, unsigned v) { for (int i = 0; i < 4; i++) o[i] = (unsigned char)(v >> (8 * i)); }

/* A UPS encoder in byuu's layout: relative skips, XOR runs ended by 0. */
static size_t ups_make(const unsigned char *a, size_t an, const unsigned char *b, size_t bn, unsigned char *o)
{
    size_t n = 0, last = 0, max = an > bn ? an : bn;
    memcpy(o, "UPS1", 4);
    n = 4;
    n += vput(o + n, an);
    n += vput(o + n, bn);
    for (size_t i = 0; i < max;) {
        unsigned x = (i < an ? a[i] : 0) ^ (i < bn ? b[i] : 0);
        if (!x) { i++; continue; }
        n += vput(o + n, i - last);
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
    put32(o + n, jk_crc32(a, an));
    put32(o + n + 4, jk_crc32(b, bn));
    n += 8;
    put32(o + n, jk_crc32(o, n));
    return n + 4;
}

static unsigned char A[200000], B[210000], PATCH[600000];

static void fill(void)
{
    unsigned s = 12345;
    for (size_t i = 0; i < sizeof A; i++) { s = s * 1103515245u + 12345u; A[i] = (unsigned char)(s >> 16); }
    memcpy(B, A, sizeof A);
    for (size_t i = 1000; i < 1100; i++) B[i] ^= 0x5a;
    for (size_t i = 70000; i < 140000; i += 7) B[i] = (unsigned char)i;
    for (size_t i = sizeof A; i < sizeof B; i++) B[i] = (unsigned char)(i * 3);
}

static int one(const char *d, const unsigned char *a, size_t an, const unsigned char *b, size_t bn)
{
    char pa[1100], pi[1100], po[1100], why[200] = "";
    size_t pn = ups_make(a, an, b, bn, PATCH);
    snprintf(pa, sizeof pa, "%s/p.ups", d);
    snprintf(pi, sizeof pi, "%s/in.bin", d);
    snprintf(po, sizeof po, "%s/out/x.bin", d);
    unlink(po);
    jk_fs_write_atomic(pa, PATCH, pn);
    jk_fs_write_atomic(pi, a, an);
    if (jk_ups_apply(pa, pi, po, why, sizeof why) != 0) { fprintf(stderr, "  %s\n", why); return 0; }
    size_t n = 0;
    char *got = jk_fs_read(po, 1 << 20, &n);
    int ok = got && n == bn && !memcmp(got, b, bn);
    free(got);
    return ok;
}

static void same_grow_shrink(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    fill();
    CHECK(one(d, A, sizeof A, B, sizeof A));          /* same size */
    CHECK(one(d, A, sizeof A, B, sizeof B));          /* grows */
    CHECK(one(d, B, sizeof B, A, sizeof A));          /* shrinks */
    CHECK(one(d, A, sizeof A, A, sizeof A));          /* no change */
    CHECK(one(d, A, 10, B + 5, 3));                   /* tiny */
    tmp_rm_rf(d);
}

static void refusals_leave_nothing(void)
{
    char d[256], pa[1100], pi[1100], po[1100], why[200];
    tmp_dir(d, sizeof d);
    fill();
    snprintf(pa, sizeof pa, "%s/p.ups", d);
    snprintf(pi, sizeof pi, "%s/in.bin", d);
    snprintf(po, sizeof po, "%s/x.bin", d);
    size_t pn = ups_make(A, sizeof A, B, sizeof B, PATCH);
    jk_fs_write_atomic(pa, PATCH, pn);

    /* the wrong input file (same size, one byte changed) */
    A[5000] ^= 1;
    jk_fs_write_atomic(pi, A, sizeof A);
    A[5000] ^= 1;
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    CHECK(strstr(why, "not the expected one") != NULL);
    CHECK(!tmp_exists(d, "x.bin") && !tmp_exists(d, "x.bin.jk4new"));

    /* the wrong size */
    jk_fs_write_atomic(pi, A, sizeof A - 1);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    CHECK(!tmp_exists(d, "x.bin"));

    /* a damaged patch */
    jk_fs_write_atomic(pi, A, sizeof A);
    PATCH[pn / 2] ^= 0x40;
    jk_fs_write_atomic(pa, PATCH, pn);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    CHECK(strstr(why, "damaged") != NULL);
    CHECK(!tmp_exists(d, "x.bin"));

    /* a truncated patch, not a patch, a missing patch */
    PATCH[pn / 2] ^= 0x40;
    jk_fs_write_atomic(pa, PATCH, pn - 20);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    jk_fs_write_atomic(pa, "PK\3\4 not a patch", 16);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    unlink(pa);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    CHECK(!tmp_exists(d, "x.bin"));

    /* an existing output is replaced only on success */
    tmp_put(d, "x.bin", "keep");
    jk_fs_write_atomic(pa, PATCH, pn);
    A[1] ^= 1;
    jk_fs_write_atomic(pi, A, sizeof A);
    A[1] ^= 1;
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) < 0);
    char buf[16];
    CHECK(tmp_get(d, "x.bin", buf, sizeof buf) == 4 && !strcmp(buf, "keep"));
    jk_fs_write_atomic(pi, A, sizeof A);
    CHECK(jk_ups_apply(pa, pi, po, why, sizeof why) == 0);
    CHECK(jk_fs_size(po) == (long long)sizeof B);
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(same_grow_shrink);
    RUN(refusals_leave_nothing);
    return jk_test_report("test_ups");
}
