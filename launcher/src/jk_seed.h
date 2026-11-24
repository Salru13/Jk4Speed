/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The Wine prefix seed (launcher r4). The package carries the game's Wine
 * prefix as a pinned index plus the files only the seed has
 * (jk4speed-files/seed/, made by launcher/tools/make_seed.py); every file
 * that is byte-identical to one in the package's win/ runtime is taken from
 * there instead. PATCH imports it into /data/Jk4Speed/System/prefix (the
 * title's library root + "/prefix", where the profile's "prefix = default"
 * points), the same way JkFlow's title seeds /data/JkFlow/System/prefix.
 *
 * seed/index.txt, one entry per line, paths relative to the prefix:
 *   JKSEED 1                                   first line
 *   d <relpath>                                a folder
 *   l <target> <relpath>                       a symbolic link (dosdevices/c: -> ../drive_c)
 *   f <sha256> <size> <relpath> obj            seed/objects/<sha256>
 *   f <sha256> <size> <relpath> app:<path>     <app root>/<path> (no spaces in <path>)
 * <relpath> may hold spaces (it runs to the last field). No entry may leave
 * the prefix, and no file is written through a link.
 */
#ifndef JK_SEED_H
#define JK_SEED_H

#include <stddef.h>

#include "jk_patcher.h"   /* JkProgress */

#define JK_SEED_INDEX "index.txt"
#define JK_SEED_MARKER ".jkseed-complete"   /* in the prefix: the index's sha256 and a newline */

/* 1 present (<prefix>/.jkseed-complete holds the index sha: nothing is read
 * or written); 0 imported (built in <prefix>.staging, resumed from whatever a
 * cut run left there, every file checked against its pinned hash, the marker
 * written last, then renamed to prefix_dir); <0 refused with a short reason
 * in why (no full paths): a prefix folder that exists without our marker is
 * left untouched, and so is everything when an object or runtime twin does
 * not match its pin or the volume reports too little free space.
 * A prefix holding a marker of ours for another index (an older seed) is
 * renamed to <prefix>.old-<first 8 hex of that index>[.N], never deleted, and
 * a fresh import follows; why then says where it went (for the log). */
int jk_seed_import(const char *seed_dir, const char *const *app_roots, int nroots, const char *prefix_dir,
                   JkProgress cb, void *ctx, char *why, size_t cap);

/* -1 the package has no seed (no seed_dir/index.txt); 1 prefix_dir holds this
 * seed's marker; 0 an import is needed (PATCH). */
int jk_seed_state(const char *seed_dir, const char *prefix_dir);

#endif
