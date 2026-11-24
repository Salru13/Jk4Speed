/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Load-segment digest of an ELF or SELF. See jk_elfdigest.h. */
#include "jk_elfdigest.h"
#include "jk_sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define PHDR_MAX 64
#define SEG_MAX 128

static uint64_t le(const unsigned char *b, int n)
{
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

static int hash_range(FILE *f, JkSha256 *h, uint64_t off, uint64_t len)
{
    static unsigned char buf[65536];
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return -1;
    while (len) {
        size_t want = len > sizeof buf ? sizeof buf : (size_t)len;
        if (fread(buf, 1, want, f) != want) return -1;
        jk_sha256_update(h, buf, want);
        len -= want;
    }
    return 0;
}

typedef struct Ph { uint32_t type; uint64_t off, filesz; } Ph;

/* ELF64 header at file offset base: reads its program headers (at base + e_phoff). */
static int read_phdrs(FILE *f, uint64_t base, Ph *ph, int *n)
{
    unsigned char e[64], p[56];
    if (fseeko(f, (off_t)base, SEEK_SET) != 0 || fread(e, 1, 64, f) != 64) return -1;
    if (memcmp(e, "\x7f" "ELF", 4) || e[4] != 2 || e[5] != 1) return -1;          /* 64-bit little-endian */
    uint64_t phoff = le(e + 32, 8);
    unsigned phentsize = (unsigned)le(e + 54, 2), phnum = (unsigned)le(e + 56, 2);
    if (phentsize != 56 || phnum == 0 || phnum > PHDR_MAX) return -1;
    for (unsigned i = 0; i < phnum; i++) {
        if (fseeko(f, (off_t)(base + phoff + (uint64_t)i * 56), SEEK_SET) != 0 || fread(p, 1, 56, f) != 56) return -1;
        ph[i].type = (uint32_t)le(p, 4);
        ph[i].off = le(p + 8, 8);
        ph[i].filesz = le(p + 32, 8);
    }
    *n = (int)phnum;
    return 0;
}

static int hashed(uint32_t type) { return type == 1 || type == 0x61000010u; }

int jk_load_digest(const char *path, char out[65])
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return -1;
    unsigned char m[32];
    Ph ph[PHDR_MAX];
    int nph = 0, rc = -1;
    JkSha256 h;
    jk_sha256_init(&h);
    if (fread(m, 1, 32, f) != 32) goto done;
    if (!memcmp(m, "\x7f" "ELF", 4)) {
        if (read_phdrs(f, 0, ph, &nph) < 0) goto done;
        for (int i = 0; i < nph; i++)
            if (hashed(ph[i].type) && ph[i].filesz && hash_range(f, &h, ph[i].off, ph[i].filesz) < 0) goto done;
    } else if (!memcmp(m, "\x4f\x15\x3d\x1d", 4)) {
        unsigned nseg = (unsigned)le(m + 24, 2);
        if (nseg == 0 || nseg > SEG_MAX) goto done;
        static unsigned char seg[SEG_MAX][32];
        if (fseeko(f, 0x20, SEEK_SET) != 0 || fread(seg, 32, nseg, f) != nseg) goto done;
        if (read_phdrs(f, 0x20 + (uint64_t)nseg * 32, ph, &nph) < 0) goto done;
        for (int i = 0; i < nph; i++) {
            if (!hashed(ph[i].type) || !ph[i].filesz) continue;
            int found = 0;
            for (unsigned s = 0; s < nseg; s++) {
                uint64_t fl = le(seg[s], 8);
                if (!(fl & 0x800) || (int)((fl >> 20) & 0xFFF) != i) continue;
                if ((fl & 0x2) || (fl & 0x8) || le(seg[s] + 16, 8) != ph[i].filesz) goto done;  /* encrypted or packed */
                if (hash_range(f, &h, le(seg[s] + 8, 8), ph[i].filesz) < 0) goto done;
                found = 1;
                break;
            }
            if (!found) goto done;
        }
    } else {
        goto done;
    }
    jk_sha256_final_hex(&h, out);
    rc = 0;
done:
    fclose(f);
    return rc;
}
