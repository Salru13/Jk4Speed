/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Mod archives as released: zip (stored, deflate) and 7z (LZMA2 and LZMA,
 * solid, compressed headers). The fixtures were written by Python's zipfile
 * and 7-Zip 19.00 from tests/fixtures/arc/src; every entry must come out
 * byte-identical with its CRC checked. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_archive.h"
#include "jk_fs.h"
#include "jk_sha256.h"

#define ARC JK_TEST_DIR "/fixtures/arc"

typedef struct Got {
    int n, ok, crc_bad;
    char name[16][512];
    char sha[16][65];
    long long size[16];
    JkSha256 h;
    long long cur;
    const char *only;           /* want only this name (NULL: all) */
    int stop_after_bytes;       /* sink fails after this many bytes (0: never) */
} Got;

static int g_want(void *ctx, const JkArcEntry *e)
{
    Got *g = ctx;
    if (g->only && strcmp(e->name, g->only)) return 0;
    jk_sha256_init(&g->h);
    g->cur = 0;
    return 1;
}
static int g_sink(void *ctx, const JkArcEntry *e, const unsigned char *p, size_t n)
{
    Got *g = ctx;
    (void)e;
    jk_sha256_update(&g->h, p, n);
    g->cur += (long long)n;
    return g->stop_after_bytes && g->cur > g->stop_after_bytes;
}
static int g_done(void *ctx, const JkArcEntry *e, int ok)
{
    Got *g = ctx;
    if (g->n < 16) {
        snprintf(g->name[g->n], sizeof g->name[0], "%s", e->name);
        jk_sha256_final_hex(&g->h, g->sha[g->n]);
        g->size[g->n] = g->cur;
        g->n++;
    }
    g->ok += ok;
    g->crc_bad += !ok;
    return 0;
}

static const char *const NAMES[] = { "assets/HUD/A.dds", "assets/HUD/B.dds", "assets/empty.txt", "readme.txt",
                                     "deep/a/b/c/C.bin" };

static int found(const Got *g, const char *name)
{
    for (int i = 0; i < g->n; i++) {
        if (strcmp(g->name[i], name)) continue;
        char p[600], want[65];
        long long size = 0;
        snprintf(p, sizeof p, ARC "/src/%s", name);
        if (jk_fs_sha256(p, want, &size) < 0) return 0;
        return !strcmp(want, g->sha[i]) && size == g->size[i];
    }
    return 0;
}

static void reads_all(const char *file, JkArcType type)
{
    Got g;
    memset(&g, 0, sizeof g);
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    char why[160] = "";
    CHECK_EQ(jk_archive_type(file), type);
    CHECK_EQ(jk_archive_extract(file, &ops, why, sizeof why), 0);
    if (why[0]) fprintf(stderr, "    %s: %s\n", file, why);
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        int ok = found(&g, NAMES[i]);
        if (!ok) fprintf(stderr, "    %s: %s not right\n", file, NAMES[i]);
        CHECK(ok);
    }
    CHECK_EQ(g.crc_bad, 0);
}

static void zip_deflate_and_stored(void) { reads_all(ARC "/mod.zip", JK_ARC_ZIP); }
static void sevenz_lzma2_solid_encoded_header(void) { reads_all(ARC "/lzma2.7z", JK_ARC_7Z); }
static void sevenz_lzma(void) { reads_all(ARC "/lzma.7z", JK_ARC_7Z); }
static void sevenz_plain_header(void) { reads_all(ARC "/plainhdr.7z", JK_ARC_7Z); }

static void only_wanted_entries_streamed(void)
{
    Got g;
    memset(&g, 0, sizeof g);
    g.only = "deep/a/b/c/C.bin";
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    char why[160];
    CHECK_EQ(jk_archive_extract(ARC "/lzma2.7z", &ops, why, sizeof why), 0);
    CHECK_EQ(g.n, 1);
    CHECK(found(&g, "deep/a/b/c/C.bin"));
}

static void corrupt_zip_entry_reported(void)
{
    Got g;
    memset(&g, 0, sizeof g);
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    char why[160] = "";
    int r = jk_archive_extract(ARC "/corrupt.zip", &ops, why, sizeof why);
    /* either the deflate stream breaks or the CRC does not match: never a silent success */
    CHECK(r < 0 || g.crc_bad > 0);
    CHECK(!found(&g, "assets/HUD/A.dds"));
}

static void rar_refused(void)
{
    Got g;
    memset(&g, 0, sizeof g);
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    char why[160] = "";
    CHECK_EQ(jk_archive_type(ARC "/fake.rar"), JK_ARC_RAR);
    CHECK(jk_archive_extract(ARC "/fake.rar", &ops, why, sizeof why) < 0);
    CHECK(strstr(why, "RAR") != NULL);
    CHECK(jk_archive_extract(ARC "/src/readme.txt", &ops, why, sizeof why) < 0);
}

static void sink_error_stops(void)
{
    Got g;
    memset(&g, 0, sizeof g);
    g.stop_after_bytes = 1000;
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    char why[160] = "";
    CHECK(jk_archive_extract(ARC "/lzma2.7z", &ops, why, sizeof why) < 0);
    memset(&g, 0, sizeof g);
    g.stop_after_bytes = 1000;
    CHECK(jk_archive_extract(ARC "/mod.zip", &ops, why, sizeof why) < 0);
}

static void truncated_7z_refused(void)
{
    char d[256], p[400], why[160];
    tmp_dir(d, sizeof d);
    size_t n = 0;
    char *data = jk_fs_read(ARC "/lzma2.7z", 1 << 20, &n);
    snprintf(p, sizeof p, "%s/cut.7z", d);
    jk_fs_write_atomic(p, data, n / 2);
    Got g;
    memset(&g, 0, sizeof g);
    JkArcOps ops = { &g, g_want, g_sink, g_done };
    CHECK(jk_archive_extract(p, &ops, why, sizeof why) < 0);
    data[n - 100] ^= 0x10;                            /* inside the encoded header */
    jk_fs_write_atomic(p, data, n);
    int r = jk_archive_extract(p, &ops, why, sizeof why);
    CHECK(r < 0 || g.crc_bad > 0);
    free(data);
    tmp_rm_rf(d);
}

typedef struct Listed { int n; long long bytes; int has_c; } Listed;
static int on_list(void *ctx, const JkArcEntry *e)
{
    Listed *l = ctx;
    l->n++;
    l->bytes += e->size;
    if (!strcmp(e->name, "deep/a/b/c/C.bin") && e->has_crc) l->has_c = 1;
    return 0;
}

static void list_without_decoding(void)
{
    const char *arcs[] = { ARC "/mod.zip", ARC "/lzma2.7z", ARC "/lzma.7z", ARC "/plainhdr.7z" };
    for (int i = 0; i < 4; i++) {
        Listed l = { 0, 0, 0 };
        char why[200] = "";
        CHECK_EQ(jk_archive_list(arcs[i], on_list, &l, why, sizeof why), 0);
        CHECK_EQ(l.n, 5);
        CHECK(l.has_c);
        /* the same total as a full extraction */
        Got g;
        memset(&g, 0, sizeof g);
        JkArcOps ops = { &g, g_want, g_sink, g_done };
        CHECK_EQ(jk_archive_extract(arcs[i], &ops, why, sizeof why), 0);
        long long total = 0;
        for (int k = 0; k < g.n; k++) total += g.size[k];
        CHECK_EQ(l.bytes, total);
    }
    Listed l = { 0, 0, 0 };
    char why[200] = "";
    CHECK(jk_archive_list(ARC "/fake.rar", on_list, &l, why, sizeof why) < 0);
}

int main(void)
{
    RUN(list_without_decoding);
    RUN(zip_deflate_and_stored);
    RUN(sevenz_lzma2_solid_encoded_header);
    RUN(sevenz_lzma);
    RUN(sevenz_plain_header);
    RUN(only_wanted_entries_streamed);
    RUN(corrupt_zip_entry_reported);
    RUN(rar_refused);
    RUN(sink_error_stops);
    RUN(truncated_7z_refused);
    return jk_test_report("test_archive");
}
