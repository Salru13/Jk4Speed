/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The JDLZ safety check (docs/status/2026-11-23-jdlz-crash.md): the game's
 * decompressor (speed.exe 0x64DB40) counts its input down from
 * hdr.comp - 0x12 and stops only when the counter is exactly 0. A stream that
 * skips 0 runs over the heap and crashes the race load (the old HD gauge file
 * 08b9d1ef). This is a port of evidence/jdlz-crash/jdlzemu.py: a stream
 * passes only when the counter ends exactly on 0 with the exact unpacked
 * size (EA's own streams end one byte before hdr.comp; the game accepts that).
 */
#ifndef JK_JDLZ_H
#define JK_JDLZ_H

#include <stddef.h>

enum { JK_JDLZ_OK = 0, JK_JDLZ_EMPTY = 1, JK_JDLZ_NOT = -1, JK_JDLZ_UNDERFLOW = -2, JK_JDLZ_MISMATCH = -3,
       JK_JDLZ_RUNAWAY = -4 };

/* One stream at buf[0..n): the header "JDLZ" 02 .. out_len comp. Bytes past n
 * read as 0, as the emulator does. */
int jk_jdlz_check(const unsigned char *buf, size_t n);

/* Every JDLZ stream in a file: 0 when all pass (or there are none), <0 with
 * the offset of the first bad one in *bad_off and a short reason. *count gets
 * the number of streams checked. */
int jk_jdlz_check_file(const char *path, long long *count, long long *bad_off, char *why, size_t cap);

#endif
