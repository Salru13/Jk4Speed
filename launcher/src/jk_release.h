/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The "Official Build" badge (r5). The package carries
 * <app_root>/jk4speed-files/release.txt:
 *
 *   JK4SPEED-RELEASE 1
 *   build <build id>
 *   <sha256>  <path relative to app_root>      (one line per file under
 *                                              jk4speed-files/; the PS4 rewrites
 *                                              eboot.bin at install)
 *
 * and release.sig, the raw 64-byte Ed25519 signature of release.txt by the
 * JK4SPEED release key (made offline by tools/sign_release.sh). OFFICIAL: the
 * signature checks with pk and every listed file is there at its hash;
 * CHANGED: signed, but files differ (out->changed of them); UNOFFICIAL: no
 * signature, or it does not check. Nothing is ever turned off by the result.
 */
#ifndef JK_RELEASE_H
#define JK_RELEASE_H
#include "jk_screens.h"
/* Fills out (its strings point to static storage). 0 always (the state says it). */
int jk_release_verify(const char *app_root, const unsigned char pk[32], JkRelease *out);
#endif
