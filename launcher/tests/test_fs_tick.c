/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* r5: long reads tick about every 8 MB (jk_fs_tick_fn), so PATCH can show MB. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ticks;
static long long last;
static void on_tick(void *ctx, long long done) { (void)ctx; ticks++; last = done; }

static void sha_and_copy_tick_every_8mb(void)
{
    char dir[256], src[400], dst[400], sha[65];
    tmp_dir(dir, sizeof dir);
    snprintf(src, sizeof src, "%s/big", dir);
    snprintf(dst, sizeof dst, "%s/copy", dir);
    FILE *f = fopen(src, "wb");
    static unsigned char buf[1 << 20];
    memset(buf, 7, sizeof buf);
    for (int i = 0; i < 20; i++) fwrite(buf, 1, sizeof buf, f);   /* 20 MB */
    fclose(f);
    jk_fs_tick_fn = on_tick;
    jk_fs_tick_ctx = NULL;
    ticks = 0;
    CHECK_EQ(jk_fs_sha256(src, sha, NULL), 0);
    CHECK_EQ(ticks, 2);                                  /* at 8 and 16 MB */
    CHECK_EQ(last >> 20, 16);
    ticks = 0;
    CHECK_EQ(jk_fs_copy_checked(src, dst, sha), 0);
    CHECK(ticks >= 2);
    jk_fs_tick_fn = NULL;
    ticks = 0;
    CHECK_EQ(jk_fs_sha256(src, sha, NULL), 0);
    CHECK_EQ(ticks, 0);                                  /* off: no calls */
    tmp_rm_rf(dir);
}

int main(void)
{
    RUN(sha_and_copy_tick_every_8mb);
    return jk_test_report("test_fs_tick");
}
