/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Test helper: a synthetic save built from the layout of
 * docs/launcher/2026-11-23-save-format.md (no real save bytes): "20CM",
 * size, lenA 8, lenB, crcA/crcB/crcHdr (EA-CRC), block A {0x10D, lenB},
 * block B with the alias at 0x5A31 (32 bytes, NUL-padded), MD5 tail. */
#ifndef JK_SAVEFX_H
#define JK_SAVEFX_H

#include "jk_fs.h"
#include "jk_md5.h"
#include "jk_saves.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char SAVE[JK_SAVE_SIZE];

static __attribute__((unused)) void savefx_put32(unsigned char *b, unsigned v)
{
    for (int i = 0; i < 4; i++) b[i] = (unsigned char)(v >> (8 * i));
}

static __attribute__((unused)) void savefx_seal(unsigned char *d)
{
    size_t n = (size_t)JK_SAVE_SIZE;
    jk_md5(d + 0x34, n - 0x34 - 16, d + n - 16);
    savefx_put32(d + 0x14, jk_save_crc(d + 0x24, n - 0x24));
    savefx_put32(d + 0x10, jk_save_crc(d + 0x1C, 8));
    savefx_put32(d + 0x18, jk_save_crc(d, 0x18));
}

static __attribute__((unused)) const unsigned char *save_bytes(const char *alias, unsigned seed)
{
    size_t n = (size_t)JK_SAVE_SIZE;
    memset(SAVE, 0, sizeof SAVE);
    memcpy(SAVE, "20CM", 4);
    savefx_put32(SAVE + 4, (unsigned)n);
    savefx_put32(SAVE + 8, 8);
    savefx_put32(SAVE + 12, (unsigned)(n - 0x24));
    savefx_put32(SAVE + 0x1C, 0x10D);
    savefx_put32(SAVE + 0x20, (unsigned)(n - 0x24));
    memcpy(SAVE + 0x44, "Game", 4);
    for (size_t i = 0x48; i < 0x5A2D; i++) { seed = seed * 1103515245u + 12345u; SAVE[i] = (unsigned char)(seed >> 16); }
    memcpy(SAVE + JK_SAVE_NAME_AT, alias, strlen(alias));
    for (size_t i = JK_SAVE_NAME_AT + JK_SAVE_NAME_LEN; i < n - 16; i += 7) SAVE[i] = (unsigned char)(i ^ seed);
    savefx_seal(SAVE);
    return SAVE;
}

static __attribute__((unused)) void put_save(const char *dir, const char *rel, const char *alias, unsigned seed)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    jk_fs_mkdir_parent(p);
    jk_fs_write_atomic(p, save_bytes(alias, seed), (size_t)JK_SAVE_SIZE);
}

static __attribute__((unused)) int same_save(const char *dir, const char *rel, const char *alias, unsigned seed)
{
    char p[1024];
    size_t n = 0;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    char *d = jk_fs_read(p, (size_t)JK_SAVE_SIZE, &n);
    int ok = d && n == (size_t)JK_SAVE_SIZE && !memcmp(d, save_bytes(alias, seed), n);
    free(d);
    return ok;
}

#endif
