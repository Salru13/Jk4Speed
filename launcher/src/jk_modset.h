/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The mod set (declarative, launcher r4). The player turns each of the three
 * mods ON or OFF (SETTINGS > MODS, <data_root>/modset/choice.txt; default all
 * ON); any of the 8 subsets is a choice. Every PATCH brings every managed
 * path to the catalog's file for the target subset (JkMsPath.at[set]):
 *
 *   "="   the untouched file, from the launcher's pristine store
 *         <data_root>/backup/pristine/<path> (+ pristine.txt), captured once
 *         (jk_modset_pristine_capture) and accepted only at the catalog's
 *         untouched hash; a path the game does not have is removed
 *   sha   taken from the cache <data_root>/modset/built/<sha>, or built from
 *         the pristine store, our package deltas and the mods' downloads
 *
 * Target rule: target = the choice, except that a mod which is ON, not
 * installed, and whose download is missing is left out (row ON_NO_ARCHIVE).
 * A missing download never means OFF: an installed layer stays as it is, and
 * OFF needs no download (the pristine store and the cache suffice). When a
 * file the target needs cannot be made, PATCH refuses with every problem
 * listed and changes nothing. Files swapped out that are catalog outputs move
 * into built/ (rename, no copy). Every output is checked (SHA-256, and the
 * game's own JDLZ counter) in staging, then swapped in under a journal: a
 * failure or a power cut leaves the game exactly as before
 * (jk_modset_recover at the next start). PATCH is idempotent.
 */

/* jk_modset_apply: the swap stopped half way and could not be undone now; the
 * journal stays and the next start's jk_modset_recover puts the game back. */
#define JK_MS_PENDING (-3)
#ifndef JK_MODSET_H
#define JK_MODSET_H

#include "jk_patcher.h"

#include <stddef.h>

/* catalog kinds: the three mods, then the player's game and our package */
enum { JK_MOD_X = 0, JK_MOD_H = 1, JK_MOD_R = 2, JK_MOD_N = 3, JK_MS_GAME = 3, JK_MS_PKG = 4 };
enum { JK_MS_COPY = 0, JK_MS_DELTA, JK_MS_UPS };
#define JK_MS_BIT(m) (1 << (m))

typedef struct JkMsArchive { int mod; const char *sha; long long size; } JkMsArchive;
/* A file by hash: kind JK_MS_GAME (name = game path), JK_MOD_* (name = path
 * inside the release; the pack's by suffix), JK_MS_PKG (name under files_dir). */
typedef struct JkMsFile { const char *sha; long long size; int kind; const char *name; } JkMsFile;
/* How a target is made: COPY (arg = the sha of a catalog file), DELTA (arg =
 * delta under files_dir/deltas/), UPS (arg = the patch's sha, in = the input's). */
typedef struct JkMsRecipe { const char *sha; long long size; int how; const char *arg; const char *in; } JkMsRecipe;
/* A managed path and its file for each subset of the mods: at[set], set =
 * the JK_MS_BIT set (X=1, H=2, R=4), so at[0] is none and at[7] X+H+R ("=":
 * the player's original, or no file). vanilla: the game has its own file
 * there (it can only be restored, never deleted). */
#define JK_MS_SETS 8
typedef struct JkMsPath { const char *path; int vanilla; const char *at[JK_MS_SETS]; } JkMsPath;

typedef struct JkMsCatalog {
    const JkMsArchive *arcs; int narcs;
    const JkMsFile *files; int nfiles;
    const JkMsRecipe *recipes; int nrecipes;
    const JkMsPath *paths; int npaths;
    const char *const *bad; int nbad;     /* never installed, never kept as an original */
} JkMsCatalog;

const JkMsCatalog *jk_modset_catalog(void);
/* "Xbox 360 Stuff Pack", "Upscaled HUD", "HD Road Textures" */
const char *jk_modset_name(int mod);

/* The player's ON/OFF choice (JK_MS_BIT set), <data_root>/modset/choice.txt. */
#define JK_MS_DEFAULT_ON (JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_H) | JK_MS_BIT(JK_MOD_R))
int jk_modset_choice(const JkPaths *p);                 /* the default when nothing was saved */
int jk_modset_set_choice(const JkPaths *p, int on);     /* 0 ok (atomic write) */

/* What the drop folders hold, per mod (r4: any subset works). */
enum { JK_MS_ABSENT = 0, JK_MS_READY, JK_MS_BAD };
typedef struct JkMsMod {
    int state;
    char archive[128];      /* the archive's file name (READY, BAD) */
    char note[160];         /* BAD: why, for the screen */
} JkMsMod;

/* Per mod, for the MODS rows. */
enum { JK_MS_ROW_OFF = 0, JK_MS_ROW_ON_INSTALLED, JK_MS_ROW_ON_PENDING, JK_MS_ROW_ON_NO_ARCHIVE,
       JK_MS_ROW_OFF_PENDING, JK_MS_ROW_BAD };

#define JK_MS_PROBLEMS 4
typedef struct JkMsScan {
    JkMsMod mod[JK_MOD_N];
    int on;                 /* the player's choice (JK_MS_BIT set) */
    int want;               /* = target */
    int installed;          /* the set in place: from the record, else from the game folder hashes; -1 mixed/unknown */
    int target;             /* the set the next PATCH builds (see the target rule) */
    int row[JK_MOD_N];      /* JK_MS_ROW_* */
    int cached[JK_MOD_N];   /* in the target without its download (installed files or the cache serve it) */
    int problems;           /* why the next PATCH would refuse (0: it can run) */
    char problem[JK_MS_PROBLEMS][160];
    char missing_pristine[JK_MS_PROBLEMS][64];   /* managed paths with no untouched copy */
    int nmissing_pristine;
    /* the paths the target needs an untouched copy of and none exists (the
     * PATCH is refused for them: "MODS CAN'T BE CHANGED"); the first ones named */
    char pristine_needed[JK_MS_PROBLEMS][64];
    /* Restore only: managed paths with no unchanged copy, left as they are (the first ones named) */
    char left[JK_MS_PROBLEMS][64];
    int nleft;
    int npristine_needed;
    int unknown;            /* other archives (not one of the three) */
    char unknown_name[128];
} JkMsScan;

#define JK_MS_DIRS_MAX 4
/* Scans the drop folders (missing ones are skipped), the game folder and the
 * pristine store, and works out the target and the rows. Cheap: archive
 * hashes are cached by size and time in <data_root>/modset/archives.txt, the
 * game folder's by the install record. 0 ok. */
int jk_modset_scan(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, JkMsScan *out);

/* PATCH: brings the game to the scanned target (none_only: Restore, target 0,
 * then the cache is emptied). *changed (may be NULL) gets the number of files
 * swapped (0: already in place). out (may be NULL): the scan after the PATCH,
 * or with the problems when refused. 0 ok; <0 with every problem in why, the
 * game folder unchanged; JK_MS_PENDING when a swap (this one's or a journal
 * left before) could not be undone.
 *
 * Restore (none_only) never dead-ends: a managed path with no unchanged copy
 * stays as it is (out->left / nleft name them), every other path goes to the
 * untouched file or away, and then the first file kept aside at each path
 * (backup/kept/<path>, never .2+) comes back where the path ended absent or
 * at the untouched file.
 *
 * A journal left by an earlier PATCH (the journal names its game folder and
 * whether a PATCH or a Restore wrote it) is dealt with first: its own
 * folder's is rolled back or finished; a damaged one is salvaged (what is
 * provably ours goes, the earlier file comes back, anything else is kept
 * aside; the journal moves to modset/journal.damaged.txt); another existing
 * folder's refuses the PATCH; a folder that is gone has its journal moved to
 * modset/journal.other.txt (its files kept aside). */
int jk_modset_apply(const JkMsCatalog *c, const JkPaths *p, const char *const *dirs, int ndirs, int none_only,
                    JkProgress cb, void *ctx, JkMsScan *out, int *changed, char *why, size_t cap);

/* At start: finishes or rolls back a PATCH that was cut off (journal), only
 * when the journal was written for p->game_dir (a journal without a folder
 * line, JK4MS 2, counts as this folder's). 1 rolled back, 2 finished, 0
 * nothing to do, JK_MS_OTHER_FOLDER the journal is another folder's (left
 * as it is, why says whose), <0 could not (reason; the journal stays). */
#define JK_MS_OTHER_FOLDER 3
int jk_modset_recover(const JkPaths *p, char *why, size_t cap);

/* A journal of this game folder is waiting: JK_MS_PEND_PATCH / _RESTORE
 * (written by a PATCH / a Restore), JK_MS_PEND_DAMAGED (unreadable), or
 * JK_MS_PEND_NONE (none, or another folder's). */
enum { JK_MS_PEND_NONE = 0, JK_MS_PEND_PATCH, JK_MS_PEND_RESTORE, JK_MS_PEND_DAMAGED };
int jk_modset_pending(const JkPaths *p);

/* Once per managed path the game has (vanilla): its untouched file into
 * <data_root>/backup/pristine/<path> (+ pristine.txt "sha size path"), from
 * (1) the game file at its untouched hash, (2) a sidecar <name>.* in the same
 * folder at the untouched size and hash (copied; the sidecar is listed in
 * <data_root>/state/cleanup.txt), (3) <data_root>/legacy-r3/modset/original
 * or legacy-r3/backup. Never writes the game folder. Free space is checked
 * first (jk_fs_free_bytes on data_root): too little refuses before any write
 * ("N MB needed, M MB free"); unknown goes ahead and says so in why.
 * 0 ok, -2 some files have no untouched copy (named in why), <0 other. */
int jk_modset_pristine_capture(const JkMsCatalog *c, const JkPaths *p, JkProgress cb, void *ctx, char *why, size_t cap);

/* Deletes the game-folder files listed in <data_root>/state/cleanup.txt
 * ("sha path", under a first line "# game <dir>") that still have that hash;
 * others are left alone. A list written for another game folder is not
 * acted on (0). Returns the number deleted, <0 on error. */
int jk_modset_cleanup(const JkPaths *p, char *why, size_t cap);

/* Tests only: the next PATCH stops as a power cut would after N renames in
 * the game folder (journal left, nothing cleaned). 0 off. */
void jk_modset_test_fault(int after_renames);

#endif
