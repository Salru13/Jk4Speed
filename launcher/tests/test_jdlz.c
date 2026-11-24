/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The game-exact JDLZ counter check: streams from our encoder pass; a stream
 * whose counter skips 0 (the 08b9d1ef gauge crash) or whose header size is
 * off by one fails, wherever it sits in the file. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_jdlz.h"

#define FX JK_TEST_DIR "/fixtures/delta"

static unsigned char *load(size_t *n) { return (unsigned char *)jk_fs_read(FX "/jdlz-target.bin", 1 << 20, n); }

static void put32(unsigned char *o, unsigned v) { for (int i = 0; i < 4; i++) o[i] = (unsigned char)(v >> (8 * i)); }

static long long find(const unsigned char *d, size_t n)
{
    for (size_t i = 0; i + 5 <= n; i++) if (!memcmp(d + i, "JDLZ\2", 5)) return (long long)i;
    return -1;
}

static void encoder_streams_pass(void)
{
    size_t n = 0;
    unsigned char *d = load(&n);
    long long at = find(d, n);
    CHECK(at >= 0);
    CHECK_EQ(jk_jdlz_check(d + at, n - (size_t)at), JK_JDLZ_OK);
    long long count = 0, bad = 0;
    char why[200] = "";
    CHECK_EQ(jk_jdlz_check_file(FX "/jdlz-target.bin", &count, &bad, why, sizeof why), 0);
    CHECK_EQ(count, 1);
    free(d);
}

static void off_by_one_fails(void)
{
    size_t n = 0;
    unsigned char *d = load(&n);
    long long at = find(d, n);
    unsigned comp = (unsigned)d[at + 12] | (unsigned)d[at + 13] << 8 | (unsigned)d[at + 14] << 16 | (unsigned)d[at + 15] << 24;
    put32(d + at + 12, comp - 1);
    CHECK(jk_jdlz_check(d + at, n - (size_t)at) < 0);
    put32(d + at + 12, comp);
    CHECK_EQ(jk_jdlz_check(d + at, n - (size_t)at), JK_JDLZ_OK);
    free(d);
}

static void counter_skipping_zero_is_underflow(void)
{
    /* one back-reference with the counter at 1: 1 - 2 wraps, as entry 6dfe27a1 did */
    unsigned char s[0x20] = { 'J', 'D', 'L', 'Z', 2, 0x10 };
    put32(s + 8, 100);
    put32(s + 12, 0x12 + 1);
    s[0x10] = 0x01;
    s[0x11] = 0x00;
    CHECK_EQ(jk_jdlz_check(s, sizeof s), JK_JDLZ_UNDERFLOW);
    CHECK_EQ(jk_jdlz_check((const unsigned char *)"PK\3\4", 4), JK_JDLZ_NOT);
}

static void bad_stream_found_across_the_read_window(void)
{
    char d[256], p[1100], why[200] = "";
    tmp_dir(d, sizeof d);
    size_t n = 0;
    unsigned char *src = load(&n);
    long long at = find(src, n);
    size_t pad = (1u << 20) - 3;                   /* header straddles the 1 MiB window edge */
    size_t total = pad + 2 * (n - (size_t)at) + 64;
    unsigned char *f = calloc(1, total);
    memcpy(f + pad, src + at, n - (size_t)at);
    memcpy(f + pad + (n - (size_t)at), src + at, n - (size_t)at);
    snprintf(p, sizeof p, "%s/x.bin", d);
    jk_fs_write_atomic(p, f, total);
    long long count = 0, bad = 0;
    CHECK_EQ(jk_jdlz_check_file(p, &count, &bad, why, sizeof why), 0);
    CHECK_EQ(count, 2);
    /* the second stream one byte short */
    unsigned char *h = f + pad + (n - (size_t)at);
    unsigned comp = (unsigned)h[12] | (unsigned)h[13] << 8 | (unsigned)h[14] << 16 | (unsigned)h[15] << 24;
    put32(h + 12, comp - 1);
    jk_fs_write_atomic(p, f, total);
    CHECK(jk_jdlz_check_file(p, &count, &bad, why, sizeof why) < 0);
    CHECK_EQ(bad, (long long)(pad + (n - (size_t)at)));
    CHECK(strstr(why, "would crash the game") != NULL);
    /* no JDLZ at all: nothing to check */
    tmp_put(d, "plain.txt", "no compressed data here");
    snprintf(p, sizeof p, "%s/plain.txt", d);
    CHECK_EQ(jk_jdlz_check_file(p, &count, &bad, why, sizeof why), 0);
    CHECK_EQ(count, 0);
    free(f);
    free(src);
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(encoder_streams_pass);
    RUN(off_by_one_fails);
    RUN(counter_skipping_zero_is_underflow);
    RUN(bad_stream_found_across_the_read_window);
    return jk_test_report("test_jdlz");
}
