/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* MD5 (RFC 1321), written from the RFC for the save check: a Most Wanted
 * save ends with the MD5 of its body (jk_saves.h). Not used for security. */
#ifndef JK_MD5_H
#define JK_MD5_H

#include <stddef.h>

void jk_md5(const void *data, size_t n, unsigned char out[16]);

#endif
