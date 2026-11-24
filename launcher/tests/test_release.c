/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* r5: the "Official Build" badge (jk_release_verify): Ed25519 over
 * jk4speed-files/release.txt, then every listed file by SHA-256. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_release.h"
#include "jk_ed25519.h"
#include "jk_fs.h"
#include "jk_sha256.h"
#include "../third_party/tweetnacl/tweetnacl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void unhex(const char *h, unsigned char *out, size_t n)
{
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (unsigned char)v; }
}

/* RFC 8032 section 7.1, TEST 1 (empty message) */
#define T1_SEED "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
#define T1_PK   "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
#define T1_SIG  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"

static unsigned char pk[32], sk[64];

static void ed25519_rfc8032_vector1(void)
{
    unsigned char sig[64];
    unhex(T1_PK, pk, 32);
    unhex(T1_SIG, sig, 64);
    CHECK_EQ(jk_ed25519_verify(sig, (const unsigned char *)"", 0, pk), 0);
    sig[10] ^= 1;
    CHECK(jk_ed25519_verify(sig, (const unsigned char *)"", 0, pk) != 0);
    sig[10] ^= 1;
    CHECK(jk_ed25519_verify(sig, (const unsigned char *)"x", 1, pk) != 0);
}

/* An /app0 tree: eboot.bin, sce_sys/param.sfo, jk4speed-files/{a.txt,sub/b.bin},
 * with release.txt listing them; signed with the RFC test key when sign. */
static void make_tree(char *root, size_t cap, int sign)
{
    tmp_dir(root, cap);
    tmp_put(root, "eboot.bin", "ELF eboot");
    tmp_put(root, "sce_sys/param.sfo", "PSF");
    tmp_put(root, "jk4speed-files/a.txt", "alpha");
    tmp_put(root, "jk4speed-files/sub/b.bin", "beta");
    const char *rel[] = { "eboot.bin", "sce_sys/param.sfo", "jk4speed-files/a.txt", "jk4speed-files/sub/b.bin" };
    char text[2048];
    size_t n = (size_t)snprintf(text, sizeof text, "JK4SPEED-RELEASE 1\nbuild JK4SPEED 1.0\n");
    for (size_t i = 0; i < 4; i++) {
        char path[600], sha[65];
        snprintf(path, sizeof path, "%s/%s", root, rel[i]);
        jk_fs_sha256(path, sha, NULL);
        n += (size_t)snprintf(text + n, sizeof text - n, "%s  %s\n", sha, rel[i]);
    }
    tmp_put(root, "jk4speed-files/release.txt", text);
    if (!sign) return;
    unsigned char *sm = malloc(n + 64);
    unsigned long long smlen = 0;
    crypto_sign(sm, &smlen, (const unsigned char *)text, n, sk);
    char path[600];
    snprintf(path, sizeof path, "%s/jk4speed-files/release.sig", root);
    FILE *f = fopen(path, "wb");
    fwrite(sm, 1, 64, f);
    fclose(f);
    free(sm);
}

static void release_official(void)
{
    char root[256];
    make_tree(root, sizeof root, 1);
    JkRelease r;
    CHECK_EQ(jk_release_verify(root, pk, &r), 0);
    CHECK_EQ(r.state, JK_REL_OFFICIAL);
    CHECK(r.key_id && strlen(r.key_id) == 8);
    CHECK(r.version && !strcmp(r.version, "JK4SPEED 1.0"));
    tmp_rm_rf(root);
}

static void release_file_changed(void)
{
    char root[256];
    make_tree(root, sizeof root, 1);
    tmp_put(root, "jk4speed-files/a.txt", "ALPHA");
    JkRelease r;
    jk_release_verify(root, pk, &r);
    CHECK_EQ(r.state, JK_REL_CHANGED);
    CHECK_EQ(r.changed, 1);
    tmp_rm_rf(root);
}

static void release_no_sig(void)
{
    char root[256];
    make_tree(root, sizeof root, 0);
    JkRelease r;
    jk_release_verify(root, pk, &r);
    CHECK_EQ(r.state, JK_REL_UNOFFICIAL);
    CHECK(r.reason == NULL);                              /* "No signature was found..." */
    tmp_rm_rf(root);
}

static void release_bad_sig(void)
{
    char root[256];
    make_tree(root, sizeof root, 1);
    char path[600], buf[2048];
    tmp_get(root, "jk4speed-files/release.txt", buf, sizeof buf);
    buf[20] = buf[20] == '1' ? '2' : '1';                 /* the list edited after signing */
    tmp_put(root, "jk4speed-files/release.txt", buf);
    JkRelease r;
    jk_release_verify(root, pk, &r);
    CHECK_EQ(r.state, JK_REL_UNOFFICIAL);
    CHECK(r.reason != NULL);
    snprintf(path, sizeof path, "%s", root);
    tmp_rm_rf(path);
}

static void release_path_outside_refused(void)
{
    char root[256];
    make_tree(root, sizeof root, 0);
    char text[600], sha[65];
    jk_sha256_hex("x", 1, sha);
    size_t n = (size_t)snprintf(text, sizeof text, "JK4SPEED-RELEASE 1\nbuild X\n%s  ../outside\n", sha);
    tmp_put(root, "jk4speed-files/release.txt", text);
    unsigned char sm[700];
    unsigned long long smlen = 0;
    crypto_sign(sm, &smlen, (const unsigned char *)text, n, sk);
    char path[600];
    snprintf(path, sizeof path, "%s/jk4speed-files/release.sig", root);
    FILE *f = fopen(path, "wb");
    fwrite(sm, 1, 64, f);
    fclose(f);
    JkRelease r;
    jk_release_verify(root, pk, &r);
    CHECK(r.state != JK_REL_OFFICIAL);
    tmp_rm_rf(root);
}

int main(void)
{
    unhex(T1_SEED, sk, 32);
    unhex(T1_PK, sk + 32, 32);
    unhex(T1_PK, pk, 32);
    RUN(ed25519_rfc8032_vector1);
    RUN(release_official);
    RUN(release_file_changed);
    RUN(release_no_sig);
    RUN(release_bad_sig);
    RUN(release_path_outside_refused);
    return jk_test_report("test_release");
}
