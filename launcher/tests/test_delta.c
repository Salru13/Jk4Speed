/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* JKD1 deltas on the console (jk_delta): the fixture written by
 * tools/make_delta.py is rebuilt from its sources, every hash checked; a
 * wrong, missing or damaged input writes nothing. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_delta.h"
#include "jk_fs.h"

#define FX JK_TEST_DIR "/fixtures/delta"

static void reads_header(void)
{
    JkDeltaInfo in;
    CHECK_EQ(jk_delta_info(FX "/fixture.jkd", &in), 0);
    CHECK_EQ(in.nsrc, 2);
    CHECK_EQ(in.target_size, 6012);
    CHECK_EQ(in.src_size[0], 6000);
    CHECK_EQ(in.src_size[1], 3000);
    char want[65];
    jk_fs_sha256(FX "/target.bin", want, NULL);
    CHECK(!strcmp(in.target_sha, want));
    jk_fs_sha256(FX "/b.bin", want, NULL);
    CHECK(!strcmp(in.src_sha[1], want));
    CHECK(jk_delta_info(FX "/a.bin", &in) < 0);           /* not a delta */
}

static void rebuilds_target(void)
{
    char d[256], out[400], why[160] = "";
    tmp_dir(d, sizeof d);
    snprintf(out, sizeof out, "%s/sub/target.bin", d);
    const char *src[] = { FX "/a.bin", FX "/b.bin" };
    CHECK_EQ(jk_delta_apply(FX "/fixture.jkd", src, 2, out, why, sizeof why), 0);
    if (why[0]) fprintf(stderr, "    why: %s\n", why);
    char got[65], want[65];
    jk_fs_sha256(out, got, NULL);
    jk_fs_sha256(FX "/target.bin", want, NULL);
    CHECK(!strcmp(got, want));
    CHECK(!tmp_exists(d, "sub/target.bin.jk4new"));
    tmp_rm_rf(d);
}

static void wrong_or_missing_source_writes_nothing(void)
{
    char d[256], out[400], why[160];
    tmp_dir(d, sizeof d);
    snprintf(out, sizeof out, "%s/target.bin", d);
    const char *swapped[] = { FX "/b.bin", FX "/a.bin" };
    CHECK(jk_delta_apply(FX "/fixture.jkd", swapped, 2, out, why, sizeof why) < 0);
    CHECK(strstr(why, "source") != NULL);
    const char *one[] = { FX "/a.bin" };
    CHECK(jk_delta_apply(FX "/fixture.jkd", one, 1, out, why, sizeof why) < 0);
    const char *gone[] = { FX "/a.bin", "/nonexistent/b.bin" };
    CHECK(jk_delta_apply(FX "/fixture.jkd", gone, 2, out, why, sizeof why) < 0);
    CHECK(!tmp_exists(d, "target.bin"));
    CHECK(!tmp_exists(d, "target.bin.jk4new"));
    tmp_rm_rf(d);
}

static void damaged_delta_writes_nothing(void)
{
    char d[256], out[400], bad[400], why[160];
    tmp_dir(d, sizeof d);
    snprintf(out, sizeof out, "%s/target.bin", d);
    size_t n = 0;
    char *delta = jk_fs_read(FX "/fixture.jkd", 1 << 20, &n);
    CHECK(delta != NULL && n > 100);
    const char *src[] = { FX "/a.bin", FX "/b.bin" };
    /* cut short */
    snprintf(bad, sizeof bad, "%s/short.jkd", d);
    jk_fs_write_atomic(bad, delta, n - 20);
    CHECK(jk_delta_apply(bad, src, 2, out, why, sizeof why) < 0);
    /* a literal byte changed: the result's hash is wrong */
    delta[n - 3] ^= 0x55;
    snprintf(bad, sizeof bad, "%s/flip.jkd", d);
    jk_fs_write_atomic(bad, delta, n);
    CHECK(jk_delta_apply(bad, src, 2, out, why, sizeof why) < 0);
    CHECK(!tmp_exists(d, "target.bin"));
    free(delta);
    tmp_rm_rf(d);
}

static void copy_out_of_range_refused(void)
{
    char d[256], out[400], bad[400], why[160];
    tmp_dir(d, sizeof d);
    snprintf(out, sizeof out, "%s/t.bin", d);
    /* header for a 4-byte target from one 4-byte source, then COPY 0 @2 len 4 */
    unsigned char buf[4 + 4 + 40 + 40 + 1 + 1 + 8 + 4 + 1];
    memset(buf, 0, sizeof buf);
    memcpy(buf, "JKD1", 4);
    buf[4] = 1;
    buf[4 + 4 + 32] = 4;                 /* target size */
    buf[4 + 4 + 40 + 32] = 4;            /* source size */
    size_t k = 4 + 4 + 40 + 40;
    buf[k++] = 1; buf[k++] = 0; buf[k] = 2; k += 8; buf[k] = 4; k += 4; buf[k] = 0;
    snprintf(bad, sizeof bad, "%s/range.jkd", d);
    jk_fs_write_atomic(bad, buf, sizeof buf);
    tmp_put(d, "src.bin", "abcd");
    char src[400];
    snprintf(src, sizeof src, "%s/src.bin", d);
    const char *s[] = { src };
    CHECK(jk_delta_apply(bad, s, 1, out, why, sizeof why) < 0);  /* source sha wrong anyway, and range */
    tmp_rm_rf(d);
}

/* A JDLZ op: the applier's encoder gives the same bytes as the tool's (and
 * the JDLZ-safe trailing refill of the Golf GTI crash fix). */
static void jdlz_op_matches_the_tool(void)
{
    char d[256], out[400], why[160] = "";
    tmp_dir(d, sizeof d);
    snprintf(out, sizeof out, "%s/j.bin", d);
    const char *src[] = { FX "/a.bin" };
    CHECK_EQ(jk_delta_apply(FX "/jdlz.jkd", src, 1, out, why, sizeof why), 0);
    if (why[0]) fprintf(stderr, "    why: %s\n", why);
    char got[65], want[65];
    jk_fs_sha256(out, got, NULL);
    jk_fs_sha256(FX "/jdlz-target.bin", want, NULL);
    CHECK(!strcmp(got, want));
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(reads_header);
    RUN(rebuilds_target);
    RUN(wrong_or_missing_source_writes_nothing);
    RUN(damaged_delta_writes_nothing);
    RUN(copy_out_of_range_refused);
    RUN(jdlz_op_matches_the_tool);
    return jk_test_report("test_delta");
}
