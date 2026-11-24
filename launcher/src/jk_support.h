/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Support bundle (spec 9) and the launcher's own event log.
 *
 * jk_support_bundle copies the logs of the last runs into
 * <logs_dir>/support/<stamp>/ with a MANIFEST.txt, and, when a USB folder is
 * given and present, also to <usb_dir>/Jk4Speed-support/<stamp>/. Never
 * copied: registry files (*.reg), saves, game data. Each log is cut to its
 * last JK_SUPPORT_LOG_MAX bytes; the PS4 user name is masked as <ps4-user>.
 */
#ifndef JK_SUPPORT_H
#define JK_SUPPORT_H

#include <stddef.h>

#define JK_SUPPORT_LOG_MAX (4LL * 1024 * 1024)
#define JK_SUPPORT_SESSIONS 3
#define JK_LAUNCHER_LOG_MAX (256LL * 1024)

typedef struct JkSupportIn {
    const char *data_root;   /* required: /data/Jk4Speed/System */
    const char *game_dir;    /* /data/Jk4Speed/NFSMW-X360 (may be NULL) */
    const char *files_dir;   /* the package's jk4speed-files/ (may be NULL) */
    const char *logs_dir;    /* session logs (session-*.log) and support/: /data/Jk4Speed/Logs; required for bundles */
    const char *runtime_root;  /* radv.log; may be NULL; NULL: <logs_dir>/radv.log */
    const char *profiles_dir;  /* /data/Jk4Speed/System/profiles (nfsmw.profile); may be NULL */
    const char *usb_dir;     /* a USB drive's root, or NULL */
    const char *build_id, *local_user;
    const char *stamp;       /* folder name, e.g. "2026-11-17_2105"; NULL: bundle-N */
    /* Extra logs (absolute paths, NULL-terminated). NULL: the defaults, from
     * <game_dir>: speed_d3d9.log, speed_frames.log, dxn_server.txt, dxn_stage.txt,
     * d3d9.log (when there), jk4speed-dev.ini; <profiles_dir>/nfsmw.profile;
     * radv.log. Every log is cut to its last JK_SUPPORT_LOG_MAX bytes
     * (dxn_server.txt reaches 32 MB). */
    const char *const *log_files;
    const char *state, *pace, *release;  /* one word each for MANIFEST.txt; may be NULL */
} JkSupportIn;

/* 0 ok: out_dir holds the bundle folder. usb_ok (may be NULL): 1 when the
 * USB copy was made. <0: no data root or the folder could not be written. */
int jk_support_bundle(const JkSupportIn *in, char *out_dir, size_t cap, int *usb_ok);

/* Appends one line to <data_root>/state/launcher.log; above
 * JK_LAUNCHER_LOG_MAX it becomes launcher.log.1 first. */
void jk_log_event(const char *data_root, const char *line);

#endif
