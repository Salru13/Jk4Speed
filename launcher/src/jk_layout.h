/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The folder layout under /data/Jk4Speed (launcher r4), the same shape as the
 * JkFlow launcher's:
 *
 *   NFSMW/  NFS/  Saves/  Saves-NFS/  Mods/  Logs/ (+support)  devtools/
 *   System/{prefix,backup,modset,state,cache,profiles}
 *
 * The launcher's own data_root is System. r3 left logs/modset/state/backup/
 * registry/support next to the game; the first r4 open moves them aside.
 */
#ifndef JK_LAYOUT_H
#define JK_LAYOUT_H

#include <stddef.h>

#define JK_PRODUCT_ROOT "/data/Jk4Speed"

typedef struct JkLayout {
    char root[64], game[128], saves[96], saves_game[128], mods[96], logs[96], support[112], devtools[96],
         system[96], prefix[112], backup[112], pristine[128], modset[112], state[112], cache[112], profiles[112];
    /* 1.1: the second game (original NFS Most Wanted) */
    char game_nfs[128], saves_nfs[96], saves_nfs_game[128], system_nfs[112];
} JkLayout;

/* root NULL: JK_PRODUCT_ROOT. game = <root>/NFSMW until jk_game_dir_find
 * says otherwise. A path that does not fit leaves the struct all zero. */
void jk_layout_init(JkLayout *l, const char *root);

/* mkdir -p every folder except game and saves_game. 0 ok, -1 failed. */
int jk_layout_make(const JkLayout *l);

/* The game folder under root: a folder named NFSMW, letter case ignored.
 * 1: found (out). 0: none (out = <root>/NFSMW). -1: two or more spellings
 * (out and other name the first two, in strcmp order). */
int jk_game_dir_find(const char *root, char *out, size_t cap, char *other, size_t ocap);
/* The same for any folder name (leaf), e.g. "NFS" for the original game. */
int jk_game_dir_find_leaf(const char *root, const char *leaf, char *out, size_t cap, char *other, size_t ocap);

typedef void (*JkLayoutLog)(void *ctx, const char *line);

/* <root>/{logs,modset,state,backup,registry,support} -> <system>/legacy-r3/
 * <name>, by rename only. Where the target exists it is kept and the old
 * entry becomes <name>.old-r3 (then .old-r32 .. .old-r39). The game folders
 * and anything else are not touched. Returns the entries moved, -1 when a
 * move failed. */
int jk_layout_migrate_r3(const JkLayout *l, JkLayoutLog log, void *ctx);

#endif
