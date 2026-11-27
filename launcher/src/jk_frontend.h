/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The single seam between the Jk4Speed launcher UI and the title that hosts
 * it (spec 10). The host fills JkServices and calls jk_frontend_run(); the UI
 * calls no PS4 API itself. The host then acts on JkDecision: start the game
 * (drawing jk_loader_draw until the game's first frame) or leave to home.
 *
 * There is no developer mode, no hidden key and no switch file: what the
 * player sees depends only on the game folder and the package. (The title
 * may wrap these services with its console test hook, devtools/jk_devhook.h,
 * which only attaches when <data_root>/devtools/enable exists and changes
 * nothing the player sees.)
 */
#ifndef JK_FRONTEND_H
#define JK_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#include "jk_config.h"
#include "jk_screens.h"
#include "jk_ui.h"

/* Pad buttons as scePadRead reports them (the same values JkFlow uses).
 * The host maps the left stick onto the D-pad bits. */
enum {
    JK_PAD_L3 = 0x2u, JK_PAD_R3 = 0x4u, JK_PAD_OPTIONS = 0x8u,
    JK_PAD_UP = 0x10u, JK_PAD_RIGHT = 0x20u, JK_PAD_DOWN = 0x40u, JK_PAD_LEFT = 0x80u,
    JK_PAD_L2 = 0x100u, JK_PAD_R2 = 0x200u, JK_PAD_L1 = 0x400u, JK_PAD_R1 = 0x800u,
    JK_PAD_TRIANGLE = 0x1000u, JK_PAD_CIRCLE = 0x2000u, JK_PAD_CROSS = 0x4000u, JK_PAD_SQUARE = 0x8000u,
    JK_PAD_TOUCHPAD = 0x100000u
};

/* 1.1: one game the launcher can start. Slot 0 is the Black Edition
 * (NFSMW), slot 1 the original (NFS). Strings are owned by the host. */
typedef struct JkGameSlot {
    const char *key;            /* profile key: "nfsmw" or "nfs" */
    const char *label;          /* the side's name on Home, e.g. "MOST WANTED - BLACK EDITION" */
    const char *leaf;           /* folder name under the product root: "NFSMW" / "NFS" */
    const char *game_dir;       /* the game folder (jk_game_dir_find_leaf) */
    const char *game_dir_other; /* a second spelling of it, or NULL */
    const char *data_root;      /* backup, modset, state of this game: System or System/nfs */
    const char *saves_dir;      /* the game's save folder inside saves root */
    const char *saves_win;      /* the saves root as Wine sees it: Z:\data\Jk4Speed\Saves */
    int exe_any;                /* 1: any speed.exe is accepted (no hash lock) */
    int mods;                   /* 1: the mods engine and MODS screen are on */
    int require_x360;           /* as JkServices.require_x360, for this slot */
    const char *files_dir;      /* this game's package folder (manifest + files); NULL: JkServices.files_dir */
} JkGameSlot;

typedef struct JkServices {
    void *ctx;
    /* Required. Show a finished 1920x1080 BGRX frame. <0: display lost, the
     * run ends with an error. Should wait for a free buffer. */
    int (*present)(void *ctx, const JkFrame *frame);
    /* Buttons held and newly pressed (edges) since the last call. <0: no
     * controller yet. May be NULL (no input: only should_quit ends the run). */
    int (*pad)(void *ctx, uint32_t *held, uint32_t *pressed);
    /* 1 when the system asked the app to close. May be NULL. */
    int (*should_quit)(void *ctx);
    /* Required: a monotonic clock and a sleep. */
    uint64_t (*now_ns)(void *ctx);
    void (*sleep_us)(void *ctx, uint32_t us);
    /* Local date and time as "YYYY-MM-DD_HHMM" for the support bundle's
     * folder name. 0 ok. May be NULL (bundles are then numbered). */
    int (*wall_time)(void *ctx, char *out, size_t cap);
    /* Told the name of each screen when it changes: "home", "patch",
     * "settings", "controls", "saves", "logs", "about", "dialog", "failed",
     * "mods", "credits", "help", "interrupted". May be NULL. */
    void (*screen)(void *ctx, const char *name);
    /* One line for the title's session log (no newline). May be NULL. The
     * launcher also keeps <data_root>/state/launcher.log itself. */
    void (*log)(void *ctx, const char *line);

    /* Folders (jk_layout.h). game_dir: /data/Jk4Speed/NFSMW; data_root:
     * /data/Jk4Speed/System (backup/, modset/, state/ ...); files_dir: the
     * package's jk4speed-files/ (manifest.txt and our files); logs_dir:
     * /data/Jk4Speed/Logs (session logs; support bundles go to logs_dir/
     * support). Required: game_dir, data_root, files_dir. */
    const char *game_dir, *data_root, *files_dir, *logs_dir;
    /* runtime_root (radv.log) may be NULL. profiles_dir: where the profile
     * the patcher installs goes. Required when the package has a "profile"
     * line. */
    const char *runtime_root, *profiles_dir;
    /* The native DXVK server ("server" line). May be NULL (r4 title). */
    const char *override_dir;
    /* /data/Jk4Speed/Mods: where the player drops mod archives (the USB's
     * Jk4Speed/ folder is read as well). /data/Jk4Speed: the product root. */
    const char *mods_dir, *product_root;
    /* The game's save folder: /data/Jk4Speed/Saves/NFS Most Wanted (the
     * prefix redirects My Documents there; jk_saves.h). May be NULL (Save
     * games then only explains). */
    const char *saves_dir;
    /* read only; the r3 save folder to copy once; may be NULL. At init the
     * frontend copies it into saves_dir (jk_saves_migrate_once). */
    const char *old_saves_dir;
    /* A USB drive's root (e.g. /mnt/usb0) for the support bundle and the
     * save export/import, or NULL. */
    const char *usb_dir;
    /* Free bytes on the volume holding path, <0 unknown. May be NULL (the
     * install then skips its free-space check). */
    long long (*free_bytes)(void *ctx, const char *path);
    /* Release verification result (stub until the signed manifest exists).
     * NULL: unofficial, "No signature was found in this package." Only drives
     * the badge and About; nothing is ever turned off. */
    const JkRelease *release;
    /* Extra logs for the support bundle (absolute paths, NULL-terminated), or
     * NULL for the defaults (jk_support.h). */
    const char *const *log_files;
    /* Shown bottom right, e.g. "JK4SPEED 1.0". May be NULL. */
    const char *build_id;
    /* Set when the previous start died before the game's first frame: the
     * run opens on "The game didn't start". A short sentence, never a path;
     * the log is a file name only. */
    const char *start_failure, *start_failure_log;
    /* The PS4 user name, masked in the support bundle. May be NULL. */
    const char *local_user;
    /* A second spelling of the game folder (jk_game_dir_find's other, e.g.
     * /data/Jk4Speed/nfsmw next to /data/Jk4Speed/NFSMW), or NULL. When set,
     * Home and Patch name both folders and PLAY / PATCH do nothing until the
     * player keeps only one: no guessing. */
    const char *game_dir_other;
    /* The library root that holds the Wine prefix (prefix/), shared by every
     * run: /data/Jk4Speed/System. NULL: data_root. A devtools clean-test run
     * has its own data_root (/data/Jk4Speed/clean-test/System: backup, modset,
     * state, cache, the launcher log) but this one prefix. */
    const char *shared_root;
    /* r5 (owner, 2026-11-24): the Xbox 360 Stuff Pack is required. Its MODS
     * row stays ON, a saved choice without it is corrected at open, and PATCH
     * refuses before any write when the pack is neither installed nor found.
     * The upscaled HUD and the HD roads stay optional. 0: any subset (tests). */
    int require_x360;
    /* Production package: no test hook (devtools/enable is ignored, the ini
     * gets DXVK_PS4_AUTOTEST=0) and no jk4speed-user.ini. */
    int prod;
    /* 1.1: the two games. ngames == 0: the old single fields above (game_dir,
     * data_root, saves_dir, game_dir_other, require_x360) describe slot 0
     * and there is no second game. */
    JkGameSlot games[2];
    int ngames;
} JkServices;

typedef enum JkAction { JK_START_GAME = 1, JK_LEAVE_HOME } JkAction;

typedef struct JkDecision {
    JkAction action;
    JkPace pace;       /* for the host's session log; the ini is already written */
    int game;          /* the slot to start (JkServices.games index) */
} JkDecision;

/* Runs the launcher until the player starts the game or the system asks the
 * app to close. 0: *out holds the decision. <0: bad arguments, out of memory
 * or the display was lost. The background cache stays for the loader; call
 * jk_loader_release() at the game's first frame. */
int jk_frontend_run(const JkServices *s, JkDecision *out);

/* ---- the state machine, exposed for host tests ---- */

typedef struct JkApp {
    JkModel m;
    const JkServices *s;        /* what the code reads: the host's services, or (ngames > 0) view */
    JkServices view;            /* 1.1: *src with the active game's folders in the old single fields */
    const JkServices *src;      /* the host's services */
    const JkGameSlot *slot;     /* the active game, or NULL (ngames == 0: the single game of 1.0) */
    int active;                 /* its index */
    JkFrame *frame;             /* NULL: no drawing (tests) */
    JkScreenId logs_back;       /* where ○ on Logs & Support returns (Settings or Patch) */
    JkScreenId mods_back;       /* where ○ on Mods returns (Settings or Patch) */
    JkScreenId help_back;       /* where ○ on Help returns, as it was left */
    JkSettings set;             /* the player's settings (<data_root>/state/launcher.ini) */
    int done;
    int busy;                   /* a long action ran: the next poll's presses are dropped */
    int mods_recover;           /* jk_modset_recover at open: 1 rolled back, 2 finished, 3 another folder's, 0 none, <0 failed */
    int prefix_bad;             /* slot 1: PLAY found the prefix's user.reg unusable; PATCH sets the prefix up again */
    int pending;              /* JK_MS_PEND_*: a journal of this game folder waits; PLAY opens Patch */
    char pending_why[300];      /* its reason, shown on Patch FAILED */
    JkAction action;
    char last_screen[16];
} JkApp;

/* 1.1: the host's services with the active side. */
/* Reads the folders and opens Home (or "The game didn't start"). 0 ok. */
int jk_app_init(JkApp *a, const JkServices *s, JkFrame *frame);
/* Handles one poll's newly pressed buttons (one action at most). Returns 1
 * when the model changed. */
int jk_app_press(JkApp *a, uint32_t pressed);
/* r5: at open, sets the Wine prefix up when it is not complete (one time,
 * "Setting up Jk4Speed" with progress), then back to the opening screen.
 * 1 set up; 0 nothing to do; <0 failed (Patch FAILED says why). */
int jk_app_setup(JkApp *a);
/* Frees the launcher's caches, the background included, and forgets the
 * host's free-space callback (tests). */
void jk_frontend_release(void);
/* The model of the running jk_frontend_run(), read only; NULL when no run is
 * active. For the title's diagnostics (screen and focus in a state dump). */
const JkModel *jk_frontend_model(void);

#endif
