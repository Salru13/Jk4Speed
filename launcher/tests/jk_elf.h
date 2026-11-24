/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Test helpers: a tiny module in the three forms the console shows it in.
 * One code segment, the 0x61000010 segment, a PT_DYNAMIC (not hashed) and a
 * data segment. The ELF form carries section bytes outside every segment
 * (junk), which a SELF never keeps and FTP's unwrapped ELF has as zeros. */
#ifndef JK_ELF_H
#define JK_ELF_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static __attribute__((unused)) void elf_put(unsigned char *b, int n, uint64_t v)
{
    for (int i = 0; i < n; i++) b[i] = (unsigned char)(v >> (8 * i));
}

typedef struct ElfSeg { uint32_t type; const char *data; } ElfSeg;
#define ELF_NSEG 4
static __attribute__((unused)) const uint32_t ELF_TYPES[ELF_NSEG] = { 1, 0x61000010u, 2, 1 };

/* ELF64 with the four segments; junk fills the gap after them. Returns the size. */
static __attribute__((unused)) size_t elf_make(unsigned char *out, const char *const seg[ELF_NSEG], unsigned char junk)
{
    size_t off = 64 + 56 * ELF_NSEG;
    memset(out, 0, 4096);
    memcpy(out, "\x7f" "ELF", 4);
    out[4] = 2; out[5] = 1; out[6] = 1;
    elf_put(out + 32, 8, 64);
    elf_put(out + 54, 2, 56);
    elf_put(out + 56, 2, ELF_NSEG);
    for (int i = 0; i < ELF_NSEG; i++) {
        unsigned char *ph = out + 64 + 56 * i;
        size_t n = strlen(seg[i]);
        elf_put(ph, 4, ELF_TYPES[i]);
        elf_put(ph + 8, 8, off);
        elf_put(ph + 32, 8, n);
        memcpy(out + off, seg[i], n);
        off += n;
    }
    memset(out + off, junk, 64);                  /* section data no segment maps */
    return off + 64;
}

/* The signed form: segment table, then the ELF header and program headers,
 * then the hashed segments (flag 0x800 | index << 20). */
static __attribute__((unused)) size_t self_make(unsigned char *out, const char *const seg[ELF_NSEG])
{
    int hashed[ELF_NSEG], nh = 0;
    for (int i = 0; i < ELF_NSEG; i++) if (ELF_TYPES[i] != 2) hashed[nh++] = i;
    memset(out, 0, 4096);
    memcpy(out, "\x4f\x15\x3d\x1d", 4);
    elf_put(out + 24, 2, (uint64_t)nh);
    size_t elf = 0x20 + 32 * (size_t)nh;
    size_t off = elf + 64 + 56 * ELF_NSEG;
    memcpy(out + elf, "\x7f" "ELF", 4);
    out[elf + 4] = 2; out[elf + 5] = 1; out[elf + 6] = 1;
    elf_put(out + elf + 32, 8, 64);
    elf_put(out + elf + 54, 2, 56);
    elf_put(out + elf + 56, 2, ELF_NSEG);
    size_t at[ELF_NSEG];
    for (int i = 0; i < ELF_NSEG; i++) {
        size_t n = strlen(seg[i]);
        at[i] = off;
        memcpy(out + off, seg[i], n);
        off += n;
        unsigned char *ph = out + elf + 64 + 56 * i;
        elf_put(ph, 4, ELF_TYPES[i]);
        elf_put(ph + 8, 8, 0x1000 + (uint64_t)i * 0x100);   /* the ELF's own offsets: not where the SELF keeps them */
        elf_put(ph + 32, 8, n);
    }
    for (int k = 0; k < nh; k++) {
        unsigned char *e = out + 0x20 + 32 * k;
        size_t n = strlen(seg[hashed[k]]);
        elf_put(e, 8, 0x800u | ((uint64_t)hashed[k] << 20));
        elf_put(e + 8, 8, at[hashed[k]]);
        elf_put(e + 16, 8, n);
        elf_put(e + 24, 8, n);
    }
    return off;
}

static __attribute__((unused)) void elf_write(const char *path, const unsigned char *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
}

#endif
