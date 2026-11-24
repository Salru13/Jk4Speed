/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Ed25519 signature check (TweetNaCl crypto_sign_open, public domain). */
#ifndef JK_ED25519_H
#define JK_ED25519_H
#include <stddef.h>
/* 0: sig is a valid Ed25519 signature of msg[0..n) by pk; -1 otherwise. */
int jk_ed25519_verify(const unsigned char sig[64], const unsigned char *msg, size_t n, const unsigned char pk[32]);
#endif
