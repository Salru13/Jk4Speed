/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Reading mod archives as their authors release them, on the console:
 * zip (stored, deflate: jk_inflate) and 7z (LZMA, LZMA2: jk_lzma; copy). RAR
 * is recognised and refused (no mod we use ships as RAR).
 *
 * jk_archive_extract walks every file entry in archive order. For each one it
 * asks want(); a wanted entry's bytes are streamed to sink() in pieces, and
 * done() is told at its end whether its CRC matched. Nothing is extracted to
 * disk here; the caller hashes and keeps what it needs. Memory: 32 KB for
 * deflate; for LZMA the dictionary, in 4 MB pieces (no single big buffer).
 */
#ifndef JK_ARCHIVE_H
#define JK_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

typedef enum JkArcType { JK_ARC_UNKNOWN = 0, JK_ARC_ZIP, JK_ARC_7Z, JK_ARC_RAR } JkArcType;

typedef struct JkArcEntry {
    char name[512];          /* path inside the archive, '/' separated, UTF-8 */
    long long size;          /* unpacked */
    uint32_t crc;
    int has_crc;
} JkArcEntry;

typedef struct JkArcOps {
    void *ctx;
    /* 1: stream this entry to sink, 0: skip it. */
    int (*want)(void *ctx, const JkArcEntry *e);
    /* bytes of the wanted entry; non-0 stops everything with an error */
    int (*sink)(void *ctx, const JkArcEntry *e, const unsigned char *p, size_t n);
    /* end of a wanted entry: ok 1 when its size and CRC matched */
    int (*done)(void *ctx, const JkArcEntry *e, int ok);
} JkArcOps;

JkArcType jk_archive_type(const char *path);
/* 0 ok; <0 with a short reason (damaged, unsupported method, a sink error). */
int jk_archive_extract(const char *path, const JkArcOps *ops, char *why, size_t cap);

/* Every file entry's name, size and CRC, without decoding anything (zip: the
 * central directory; 7z: the header). cb's return value is ignored. */
int jk_archive_list(const char *path, int (*cb)(void *ctx, const JkArcEntry *e), void *ctx, char *why, size_t cap);

/* The two formats on their own (jk_archive_extract picks by magic). */
int jk_zip_extract(const char *path, const JkArcOps *ops, char *why, size_t cap);
int jk_7z_extract(const char *path, const JkArcOps *ops, char *why, size_t cap);

#endif
