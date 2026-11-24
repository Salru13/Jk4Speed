/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * LZMA and LZMA2 decoding, written for the launcher from the format as Igor
 * Pavlov describes it (LZMA SDK, "lzma-specification.txt" / LzmaSpec.cpp,
 * public domain); no SDK code is copied. Streaming: input is pulled through
 * read(), output pushed through out(). The dictionary lives in 4 MB pieces,
 * allocated as the output grows (never more than the dictionary size or the
 * total output, whichever is smaller), so no single large buffer is needed.
 */
#ifndef JK_LZMA_H
#define JK_LZMA_H

#include <stddef.h>
#include <stdint.h>

/* Bytes read into buf (up to cap); 0 at the end or on a read error. */
typedef size_t (*JkLzmaRead)(void *ctx, unsigned char *buf, size_t cap);
/* 0 ok; anything else stops decoding. */
typedef int (*JkLzmaOut)(void *ctx, const unsigned char *p, size_t n);

enum { JK_LZMA_OK = 0, JK_LZMA_DAMAGED = -1, JK_LZMA_NOMEM = -2, JK_LZMA_OUT_FAILED = -3, JK_LZMA_UNSUPPORTED = -4 };

/* LZMA as a 7z coder: props = lc/lp/pb byte + dictionary size (LE u32);
 * exactly unpack_size bytes are produced (an end marker is accepted). */
int jk_lzma_decode(const unsigned char props[5], uint64_t unpack_size, JkLzmaRead rd, void *rctx,
                   JkLzmaOut out, void *octx);
/* LZMA2: prop = the dictionary-size byte (0..40). */
int jk_lzma2_decode(unsigned char prop, uint64_t unpack_size, JkLzmaRead rd, void *rctx, JkLzmaOut out, void *octx);

#endif
