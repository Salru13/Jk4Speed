/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Save games (owner decisions 2026-11-23). In r4 the prefix redirects "My
 * Documents" to Z:\data\Jk4Speed\Saves, so the game keeps its profiles in
 *
 *   /data/Jk4Speed/Saves/NFS Most Wanted/
 *
 * (speed.exe: SHGetFolderPathA(CSIDL_PERSONAL) + "NFS Most Wanted", research/
 * exe-analysis.md 5.3; console pre-check 2026-11-23: the folder is there and
 * empty until the first save.) One folder per profile ("alias") holding one
 * file of the same name: NFS Most Wanted/<alias>/<alias>, as on the PC.
 *
 * A save file (docs/launcher/2026-11-23-save-format.md, speed.exe RE):
 *   0x00 "20CM"   0x04 u32 size   0x08 lenA (8)   0x0C lenB   0x10 crcA
 *   0x14 crcB     0x18 crcHdr     0x1C block A    0x24 block B (to the end)
 *   0x5A31        the alias, 32 bytes, NUL-padded
 *   last 16 bytes MD5 of [0x34, size - 16)
 * The CRCs are the game's EA-CRC (jk_save_crc). jk_save_check accepts a
 * file only when every field and all four checksums hold, at the v1.3 size
 * JK_SAVE_SIZE. A rename writes the name at 0x5A31 and recomputes MD5,
 * crcB, crcA, crcHdr in that order (the game must not be running).
 *
 * Import looks on the USB drive in Jk4Speed/saves/ for: a "NFS Most Wanted"
 * folder; <alias> folders; zip or 7z archives of either (read with
 * jk_archive.h); JK4SPEED's own exports (<stamp>/NFS Most Wanted/). Every
 * candidate save is checked; foreign files are named and skipped. The player
 * sees the aliases, dates and origin first. An alias that is already on the
 * console is overwritten or kept beside (the copy gets a same-length name,
 * e.g. GOATS -> GOAT2, with the name inside the file and the MD5 updated).
 *
 * Atomic: the new folder is built beside the real one ("<dir>.jk4new"), every
 * save in it checked, the current saves backed up to
 * <data_root>/backup/saves/<stamp>/, then two renames swap it in. A cut
 * between them is put right by jk_saves_recover at the next start.
 *
 * Export copies the folder to <usb>/Jk4Speed/saves/<stamp>/NFS Most Wanted/
 * (never over an earlier export) with a jk4speed-export.txt marker.
 */
#ifndef JK_SAVES_H
#define JK_SAVES_H

#include <stddef.h>

#define JK_SAVES_FOLDER "NFS Most Wanted"
#define JK_SAVES_USB "Jk4Speed/saves"
#define JK_SAVES_MAX_FILES 512
#define JK_SAVES_MAX_FILE (64LL * 1024 * 1024)
#define JK_SAVE_SIZE 63596LL
#define JK_SAVES_CAND_MAX 12

typedef struct JkSavesInfo {
    int exists;        /* the folder is there */
    int profiles;      /* subfolders (one per alias) */
    int files;
    long long bytes;
} JkSavesInfo;

int jk_saves_info(const char *saves_dir, JkSavesInfo *out);

#define JK_SAVE_NAME_AT 0x5A31
#define JK_SAVE_NAME_LEN 0x20
#define JK_ALIAS_MAX 15              /* the save-format doc's safe subset: [A-Za-z0-9]{1,15} */

/* 0: path is a v1.3 Most Wanted save (header, sizes, 3 CRCs, MD5). <0 otherwise. */
int jk_save_check(const char *path);
/* The game's EA-CRC (speed.exe 0x7f73e1). */
unsigned jk_save_crc(const unsigned char *d, size_t n);
/* 1: a name the launcher allows (letters and digits, 1..JK_ALIAS_MAX). */
int jk_alias_valid(const char *s);
int jk_alias_char(int ch);
/* In memory: checks the save and that old is at 0x5A31, writes nw there and
 * reseals. 0 ok; <0 and nothing usable changed otherwise. */
int jk_save_rename_bytes(unsigned char *d, size_t n, const char *old, const char *nw);
/* On disk: <old>/<old> becomes <nw>/<nw>, after a backup of the whole
 * folder, through the same atomic swap as an import. Names are compared
 * without case for collisions. 0 ok; <0 with a reason and nothing changed. */
int jk_saves_rename(const char *saves_dir, const char *old, const char *nw, const char *data_root,
                    const char *stamp, char *backup, size_t bcap, char *why, size_t wcap);

typedef struct JkSaveCand {
    char alias[64];
    char dir[1024];          /* the folder holding <alias> */
    long long mtime;         /* the save file's time (seconds since 1970) */
    int console;             /* from a JK4SPEED export (else: a PC save) */
    int exists;              /* the console already has this alias */
    char keep_as[64];        /* the name it gets with "keep both" */
} JkSaveCand;

typedef struct JkSavesFound {
    int n;
    JkSaveCand c[JK_SAVES_CAND_MAX];
    int collisions;          /* candidates whose alias is on the console */
    int rejected;            /* <alias>/<alias> files that are not valid saves */
    char rejected_name[96];
} JkSavesFound;

/* Looks for saves on the USB drive (archives are unpacked under
 * <data_root>/saves-in/). 0 ok (n may be 0); <0 with a reason. */
int jk_saves_find(const char *saves_dir, const char *usb_dir, const char *data_root, JkSavesFound *out,
                  char *why, size_t wcap);

/* Imports what jk_saves_find found. keep_both: colliding aliases are kept
 * beside under keep_as; else they are overwritten (after the backup).
 * 0 ok: backup gets the backup folder ("" when there were no saves yet).
 * <0 with a reason; the saves are then exactly as before. */
int jk_saves_import_found(const char *saves_dir, const JkSavesFound *f, int keep_both, const char *data_root,
                          const char *stamp, char *backup, size_t bcap, char *why, size_t wcap);

/* The profiles on the console (folders holding <alias>/<alias>), sorted. */
int jk_saves_list(const char *saves_dir, char names[][64], int max);

/* At start: completes or undoes an import cut off between its renames.
 * 1 when something was put right, 0 nothing to do. */
int jk_saves_recover(const char *saves_dir);

/* 0 ok: out gets the USB folder written. <0 with a short reason in why. */
int jk_saves_export(const char *saves_dir, const char *usb_dir, const char *stamp,
                    char *out, size_t cap, char *why, size_t wcap);

/* r4: the game now writes /data/Jk4Speed/Saves/NFS Most Wanted. Copies (never
 * moves) every profile folder of old_dir (the r3 prefix folder, read only)
 * into new_dir when new_dir has no profile yet and <state_dir>/saves-migrated
 * is absent; then writes that marker. The marker is also written when new_dir
 * already has profiles (decided once). Nothing to copy (old_dir NULL or
 * without profiles): 0, no marker. note gets a short line for the log.
 * 1 copied, 0 nothing, <0 error (no marker; a half-copied profile is removed). */
int jk_saves_migrate_once(const char *old_dir, const char *new_dir, const char *state_dir, char *note, size_t cap);

/* "2013-01-03" from seconds since 1970 (UTC). */
void jk_saves_date(long long t, char out[16]);

#endif
