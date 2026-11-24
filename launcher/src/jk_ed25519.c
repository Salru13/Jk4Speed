/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Ed25519 verify for the release badge: TweetNaCl (public domain,
 * third_party/tweetnacl, see its README for the two local changes). */
#include "jk_ed25519.h"

#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wparentheses"
#include "../third_party/tweetnacl/tweetnacl.c"
#pragma GCC diagnostic pop

/* Only the key generation needs random bytes, and the launcher never makes keys. */
void randombytes(u8 *x, u64 n)
{
    (void)x;
    (void)n;
    abort();
}

int jk_ed25519_verify(const unsigned char sig[64], const unsigned char *msg, size_t n, const unsigned char pk[32])
{
    if (!sig || !pk || (n && !msg) || n > (1u << 24)) return -1;
    unsigned char *sm = malloc(n + 64), *m = malloc(n + 64);
    if (!sm || !m) { free(sm); free(m); return -1; }
    memcpy(sm, sig, 64);
    if (n) memcpy(sm + 64, msg, n);
    unsigned long long mlen = 0;
    int r = crypto_sign_open(m, &mlen, sm, n + 64, pk);
    free(sm);
    free(m);
    return r == 0 && mlen == n ? 0 : -1;
}
