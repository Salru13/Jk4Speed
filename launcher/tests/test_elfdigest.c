/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The load-segment digest: one value for the signed module, the ELF before
 * signing and the ELF GoldHEN's FTP serves (zeros where no segment maps).
 * The real enc17 server is checked too when its copies are in
 * launcher/build/server/ (copied from ~/nfsmw-bench/native/server-enc17/link,
 * never moved; the build folder is not in git). */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_elf.h"
#include "jk_elfdigest.h"
#include "jk_sha256.h"

static const char *const SEG[ELF_NSEG] = { "code code code", "the 0x61000010 segment", "dyn", "data!" };

static void want_of(const char *const seg[ELF_NSEG], char out[65])
{
    JkSha256 h;
    jk_sha256_init(&h);
    for (int i = 0; i < ELF_NSEG; i++) if (ELF_TYPES[i] != 2) jk_sha256_update(&h, seg[i], strlen(seg[i]));
    jk_sha256_final_hex(&h, out);
}

static unsigned char buf[4096];

static void three_forms_one_digest(void)
{
    char d[256], p[512], got[65], want[65];
    tmp_dir(d, sizeof d);
    want_of(SEG, want);
    snprintf(p, sizeof p, "%s/m.oelf", d);
    elf_write(p, buf, elf_make(buf, SEG, 0xAB));            /* the linker's ELF: section bytes */
    CHECK(jk_load_digest(p, got) == 0 && !strcmp(got, want));
    snprintf(p, sizeof p, "%s/ftp.prx", d);
    elf_write(p, buf, elf_make(buf, SEG, 0x00));            /* FTP readback: zeros there */
    CHECK(jk_load_digest(p, got) == 0 && !strcmp(got, want));
    snprintf(p, sizeof p, "%s/m.prx", d);
    elf_write(p, buf, self_make(buf, SEG));                 /* the signed file */
    CHECK(jk_load_digest(p, got) == 0 && !strcmp(got, want));
    tmp_rm_rf(d);
}

static void other_code_other_digest(void)
{
    char d[256], p[512], a[65], b[65];
    tmp_dir(d, sizeof d);
    const char *other[ELF_NSEG] = { "code code codE", "the 0x61000010 segment", "dyn", "data!" };
    snprintf(p, sizeof p, "%s/a.prx", d);
    elf_write(p, buf, self_make(buf, SEG));
    CHECK_EQ(jk_load_digest(p, a), 0);
    elf_write(p, buf, self_make(buf, other));
    CHECK_EQ(jk_load_digest(p, b), 0);
    CHECK(strcmp(a, b) != 0);
    /* the PT_DYNAMIC segment is not part of it */
    const char *dyn[ELF_NSEG] = { "code code code", "the 0x61000010 segment", "DYN", "data!" };
    elf_write(p, buf, elf_make(buf, dyn, 0));
    CHECK(jk_load_digest(p, b) == 0 && !strcmp(a, b));
    tmp_rm_rf(d);
}

static void refusals(void)
{
    char d[256], p[512], got[65];
    tmp_dir(d, sizeof d);
    tmp_put(d, "text.prx", "not a module at all, just some text that is long enough");
    snprintf(p, sizeof p, "%s/text.prx", d);
    CHECK(jk_load_digest(p, got) < 0);
    snprintf(p, sizeof p, "%s/missing.prx", d);
    CHECK(jk_load_digest(p, got) < 0);
    /* an encrypted SELF segment cannot be hashed here */
    size_t n = self_make(buf, SEG);
    buf[0x20] |= 0x2;
    snprintf(p, sizeof p, "%s/enc.prx", d);
    elf_write(p, buf, n);
    CHECK(jk_load_digest(p, got) < 0);
    /* a truncated ELF */
    n = elf_make(buf, SEG, 0);
    elf_write(p, buf, 80);
    CHECK(jk_load_digest(p, got) < 0);
    tmp_rm_rf(d);
}

static void real_enc17_server(void)
{
    const char *want = "807b2334acbeed1129600e541749ace44ec18651b41f1de6be16f3c94626feac";
    const char *forms[2] = { JK_ROOT_DIR "/build/server/dxvknative.prx", JK_ROOT_DIR "/build/server/dxvknative.oelf" };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(forms[i], "rb");
        if (!f) { fprintf(stderr, "  (skipped: no copy at %s)\n", forms[i]); continue; }
        fclose(f);
        char got[65];
        CHECK(jk_load_digest(forms[i], got) == 0 && !strcmp(got, want));
    }
}

int main(void)
{
    RUN(three_forms_one_digest);
    RUN(other_code_other_digest);
    RUN(refusals);
    RUN(real_enc17_server);
    return jk_test_report("test_elfdigest");
}
