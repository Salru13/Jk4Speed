/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The release config (launcher r4). The player's choices live in
 * <state>/launcher.ini (frame rate, Show FPS / ms, Enable logs). Before the
 * game starts the launcher composes, from those and the package's two base
 * files (jk4speed.ini.base, nfsmw.profile.base in files_dir):
 *
 *   <game_dir>/jk4speed-dev.ini       the DXVK client's keys, CRLF
 *   <profiles_dir>/nfsmw.profile      the runtime profile
 *
 * A release has every bench key written as 0 (never left out, never set by
 * the base), no DXVK_PS4_DIAG and winedebug -all. Logs on turns on the frame
 * log, the log folder and the Wine channels. DXVK_PS4_AUTOTEST=1 only when
 * devtools are on.
 */
#ifndef JK_CONFIG_H
#define JK_CONFIG_H

#include <stddef.h>

#include "jk_pace.h"
#include "jk_controls.h"

typedef struct JkSettings { JkPace pace; int fps_hud; int logs; } JkSettings;   /* defaults: JK_PACE_30, 0, 0 */

/* <state_dir>/launcher.ini. A missing file or key = the default; a value that
 * makes no sense = the default. 0 ok, -1 bad arguments. */
int jk_settings_load(const char *state_dir, JkSettings *out);
/* Atomic write (the folder is made). pace is stored as 30 or 60. 0 ok. */
int jk_settings_save(const char *state_dir, const JkSettings *s);
/* 1.1: the game side the launcher last showed (0 NFSMW, 1 NFS), kept as
 * "active=" in the same launcher.ini (jk_settings_save leaves it alone).
 * Load: -1 when there is none. Save: 0 ok. */
int jk_active_load(const char *state_dir);
int jk_active_save(const char *state_dir, int active);

typedef struct JkConfigIn {
    const char *files_dir, *game_dir, *profiles_dir, *cache_dir, *logs_dir;
    JkSettings set;
    int devtools;               /* <product_root>/devtools/enable exists */
    /* <data_root>/backup, or NULL (no backup). A jk4speed-dev.ini in the game
     * folder that is not one we compose (for any setting, nor the one last
     * written) is copied to <backup_dir>/jk4speed-dev.ini before the first
     * compose; when that backup exists, a later foreign ini goes to
     * <backup_dir>/kept/jk4speed-dev.ini (.2, .3 ...): never overwritten. */
    const char *backup_dir;
    /* <data_root>/state, or NULL: the hash of each file written is noted in
     * <state_dir>/composed.txt ("ini <sha>", "profile <sha>"), so Restore deletes
     * only a file still as the launcher last wrote it, and a later foreign ini
     * is told apart from ours. */
    const char *state_dir;
    /* r5: <product_root>/jk4speed-user.ini, or NULL. Its "KEY=VALUE" lines win
     * over the base's (key letter case ignored) and new keys are added; the
     * bench keys and the pace stay the launcher's. A missing file changes
     * nothing. For testing a setting without a new package. */
    const char *user_ini;
    /* SETTINGS > CONTROLS, or NULL for the defaults: the profile's [input]
     * key binds and Xidi.ini's button lines. */
    const JkControls *controls;
    /* 1.1: the game's profile key ("nfsmw" or "nfs"; NULL: "nfsmw"). The
     * profile is <profiles_dir>/<key>.profile with "id = <key>". */
    const char *profile_key;
    /* 1.11 SETTINGS > VISUALS (the Xbox 360 look), or NULL: the base's line. The value of
     * the base's DXVK_PS4_NFS_SHADOW_DETAIL and envmap_faces lines (jk_visuals.h). */
    const char *shadow_detail, *envmap_faces;
} JkConfigIn;

/* -> <game_dir>/jk4speed-dev.ini (CRLF). 0 ok; <0 with a short reason (no
 * full paths); nothing is written when the old file's backup fails. */
int jk_config_write_ini(const JkConfigIn *in, char *why, size_t cap);
/* -> <profiles_dir>/<profile_key>.profile (LF; default nfsmw). 0 ok; <0 with a short reason. */
int jk_config_write_profile(const JkConfigIn *in, char *why, size_t cap);

/* -> <game_dir>/Xidi.ini (0666): the package's Xidi.ini with the CustomMapper
 * button lines of in->controls (defaults: the package file byte for byte). */
int jk_config_write_xidi(const JkConfigIn *in, char *why, size_t cap);

/* The hash last composed for which ("ini" or "profile") in <state_dir>/composed.txt.
 * 0 found; <0 none. */
int jk_config_composed_sha(const char *state_dir, const char *which, char out[65]);
/* Forgets composed.txt (Restore). */
void jk_config_composed_forget(const char *state_dir);

/* Deletes the files in logs_dir and everything below logs_dir/support (empty bundle
 * folders go too). Nothing else. Returns the files removed, -1 on bad args. */
int jk_logs_clear(const char *logs_dir);

/* The 7 bench keys of the ini, NULL-terminated; a release writes each as 0. */
extern const char *const JK_BENCH_KEYS[];

#endif
