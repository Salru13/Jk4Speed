/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#ifndef JK_SHA256_H
#define JK_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct JkSha256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t fill; } JkSha256;

void jk_sha256_init(JkSha256 *s);
void jk_sha256_update(JkSha256 *s, const void *data, size_t n);
void jk_sha256_final_hex(JkSha256 *s, char out[65]);
void jk_sha256_hex(const void *data, size_t n, char out[65]);

#endif
