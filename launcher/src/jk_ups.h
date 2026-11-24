/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * UPS patches (byuu's format, as the X360 Stuff Pack ships its road change,
 * Install-Part-3.ups): written for the launcher from the format; the input,
 * output and patch CRC-32s in the footer are all checked. Streaming: the
 * input file is read once, the output written through <out>.jk4new + rename.
 */
#ifndef JK_UPS_H
#define JK_UPS_H

#include <stddef.h>

/* 0 ok; <0 with a short reason and nothing written at out. */
int jk_ups_apply(const char *patch, const char *input, const char *out, char *why, size_t cap);

#endif
