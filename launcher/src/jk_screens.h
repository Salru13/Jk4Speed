/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The screens (spec 7; owner decisions 2026-11-23) as pure drawing from a
 * model. Three main screens, as in JkFlow: PLAY (Home), PATCH and SETTINGS.
 * Settings holds FRAME RATE, SHOW FPS / MS, MODS, CONTROLS, SAVE GAMES,
 * LOGS & SUPPORT and ABOUT. Home □ opens Help (four pages). Every screen
 * carries the release badge in its top-right corner; Home, "The game didn't
 * start" and "Patch was interrupted" carry the credit bottom right. The
 * frontend owns the model and the input; nothing here reads files or the pad.
 */
#ifndef JK_SCREENS_H
#define JK_SCREENS_H

#include "jk_modset.h"
#include "jk_pace.h"
#include "jk_patcher.h"
#include "jk_saves.h"
#include "jk_keyboard.h"
#include "jk_ui.h"

#include <stddef.h>
#include <stdint.h>

typedef enum JkScreenId {
    JK_SCR_HOME = 0, JK_SCR_PATCH, JK_SCR_SETTINGS, JK_SCR_CONTROLS, JK_SCR_SAVES, JK_SCR_LOGS,
    JK_SCR_ABOUT, JK_SCR_FAILED, JK_SCR_MODS, JK_SCR_CREDITS, JK_SCR_HELP, JK_SCR_INTERRUPTED, JK_SCR_SETUP
} JkScreenId;
typedef enum JkHomeItem { JK_HOME_PLAY = 0, JK_HOME_PATCH, JK_HOME_SETTINGS, JK_HOME_COUNT } JkHomeItem;
typedef enum JkSetItem {
    JK_SET_FRAMERATE = 0, JK_SET_FPS_HUD, JK_SET_MODS, JK_SET_CONTROLS, JK_SET_SAVES, JK_SET_LOGS, JK_SET_ABOUT, JK_SET_COUNT
} JkSetItem;
/* LOGS & SUPPORT rows */
typedef enum JkLogItem { JK_LOG_ENABLE = 0, JK_LOG_CLEAR, JK_LOG_BUNDLE, JK_LOG_COUNT } JkLogItem;
/* Help pages (← →, wrapping) */
typedef enum JkHelpPage { JK_HELP_FOLDER = 0, JK_HELP_MODS, JK_HELP_SAVES, JK_HELP_TROUBLE, JK_HELP_COUNT } JkHelpPage;
typedef enum JkPatchPhase { JK_PH_IDLE = 0, JK_PH_INSTALLING, JK_PH_DONE, JK_PH_FAILED, JK_PH_RESTORED } JkPatchPhase;
/* Which step of a PATCH failed: the failure text only says what that step
 * proves (CORE: our files, maybe part way; SEED: our files in, the prefix
 * not; MODS: our files in, the mod files unchanged; PENDING: the mod swap is
 * half done and the next start rolls it back; RESTORE: Restore stopped). */
typedef enum JkFailKind { JK_FAIL_CORE = 0, JK_FAIL_SEED, JK_FAIL_MODS, JK_FAIL_PENDING, JK_FAIL_RESTORE,
               JK_FAIL_PENDING_RESTORE, JK_FAIL_DAMAGED } JkFailKind;
enum { JK_DLG_NONE = 0, JK_DLG_RESTORE = 1, JK_DLG_IMPORT = 2, JK_DLG_COLLIDE = 3, JK_DLG_PICK = 4, JK_DLG_KEYBOARD = 5 };

/* What ✕ does on the Patch screen. */
typedef enum JkPatchAction { JK_ACT_NONE = 0, JK_ACT_CHECK_AGAIN, JK_ACT_INSTALL, JK_ACT_START } JkPatchAction;

/* Release verification (docs/launcher/2026-11-23-release-signing-design.md).
 * Not built yet: the host passes a stub. Later it comes only from the signed
 * release manifest, never from an editable string or ini. No state changes
 * what the launcher or the game does: PLAY works in every state. */
typedef enum JkReleaseState { JK_REL_UNOFFICIAL = 0, JK_REL_OFFICIAL, JK_REL_CHANGED } JkReleaseState;
typedef struct JkRelease {
    JkReleaseState state;
    const char *version;      /* "1.0.0", may be NULL */
    const char *key_label;    /* "JK4SPEED release key 2026", may be NULL */
    const char *key_id;       /* short fingerprint shown with the badge, e.g. "4A1F2B3C"; may be NULL */
    const char *reason;       /* UNOFFICIAL: one line why; NULL: "No signature was found…" */
    int changed;              /* CHANGED: how many files differ */
} JkRelease;

/* Home layout (tests use it to find the items). */
enum { JK_HOME_X = 150, JK_HOME_Y0 = 290, JK_HOME_STEP = 112, JK_HOME_ITEM_H = 92 };
/* Settings list layout (seven items above the hint bar). */
enum { JK_SET_X = 150, JK_SET_Y0 = 262, JK_SET_STEP = 82, JK_SET_ITEM_H = 68 };
/* The badge's top-right corner area (tests check it is drawn on every screen). */
enum { JK_BADGE_RIGHT = 1794, JK_BADGE_Y = 58 };

typedef struct JkModel {
    JkScreenId screen;
    int focus;                    /* Home: JkHomeItem */
    int set_focus;                /* Settings: JkSetItem */
    JkCheck check;                /* the latest readiness */
    JkPace pace;
    JkPatchPhase phase;
    int prog_done, prog_total;
    char prog_file[160];
    char msg[320];                /* Patch: failure reason */
    int fps_hud;                  /* Show FPS / ms: 0 off (default), 1 on */
    int logs_on;                  /* Enable logs: 0 off (default), 1 on */
    int logs_focus;               /* LOGS & SUPPORT: JkLogItem */
    int log_files;                /* files in the Logs folder (support bundles included) */
    long long log_bytes;
    char logs_msg[160];           /* the last Clear logs / setting result */
    int logs_msg_bad;
    int help_page;                /* JkHelpPage */
    int no_pristine;              /* the last PATCH was refused: no unchanged copy (mods.pristine_needed) */
    int fail_kind;                /* JK_PH_FAILED: JkFailKind */
    int restore_left_n;           /* JK_PH_RESTORED: managed files left as they are (no unchanged copy) */
    char restore_left[JK_MS_PROBLEMS][64];
    const char *game_dir;         /* the game folder ... */
    const char *game_other;       /* ... and a second spelling of it: PLAY / PATCH blocked; NULL: one */
    int bundle_state;             /* 0 not saved, 1 saved, -1 failed */
    char bundle_dir[256];
    int bundle_usb;
    int dialog;                   /* JK_DLG_* */
    /* Save games */
    int saves_profiles;           /* -1: no folder yet */
    int saves_state;              /* 0 idle, 1 exported, 2 imported, 3 renamed, -1 failed */
    char saves_msg[256];          /* the folder written / backed up, or the reason */
    JkSavesFound saves_found;     /* import preview: what the USB drive holds */
    char save_names[JK_SAVES_CAND_MAX][64];   /* rename: the profiles on the console */
    int save_count, save_pick;
    JkKeyboard kb;                /* the pad-driven keyboard (player name) */
    int usb;                      /* a USB drive is plugged in */
    /* Mods (SETTINGS > MODS, or PATCH > □): the four mods, found and ON/OFF */
    JkMsScan mods;                /* the drop folders and the player's choice (jk_modset.h) */
    int mods_focus;               /* the focused row: JK_MOD_* */
    const char *credits;          /* About > Credits: the text of jk4speed-files/about/CREDITS.txt */
    int credits_top;              /* first line shown */
    int mods_state;               /* 0 idle, 1 installed, 2 choice saved, -1 failed or refused */
    int mods_done;                /* files put in place by the last install */
    char mods_msg[360];
    const char *build_id;         /* bottom right, may be NULL */
    const char *failure, *failure_log;
    JkRelease release;
    int x360_required;            /* r5: the pack row is locked ON (no arrows) */
    int siren;                    /* 0..256: the focus bar's red/blue cross-fade */
} JkModel;

void jk_screen_draw(JkFrame *f, const JkModel *m);

JkPatchAction jk_patch_primary(const JkModel *m);
/* Two spellings of the game folder (m->game_other set). */
int jk_two_folders(const JkModel *m);
/* "Two game folders: <game_dir> and <game_other>. ..." */
void jk_two_folders_body(const JkModel *m, char *out, size_t cap);
/* A Patch failure's text: the reason, then what is true for its JkFailKind. */
void jk_patch_failure_body(const JkModel *m, char *out, size_t cap);
/* The no-clean-original failure's text, naming up to three files. */
void jk_no_pristine_body(const JkModel *m, char *out, size_t cap);
/* BACKUP RESTORED: the text, naming up to three files left as they are. */
void jk_restored_body(const JkModel *m, char *out, size_t cap);
/* The words of a MODS row (JK_MS_ROW_*). */
const char *jk_mods_row_text(int row);
/* Settings: an item's label, and its value ("30 FPS", "OFF"...; NULL: none). */
const char *jk_settings_label(int item);
const char *jk_settings_value(const JkModel *m, int item);
/* A Help page's name. */
const char *jk_help_title(int page);
int jk_patch_can_restore(const JkModel *m);
/* □ on Patch opens Mods (the game must be usable and nothing failed). */
int jk_patch_can_mods(const JkModel *m);
/* □ on Patch opens Help instead: two game folders, or "MODS CAN'T BE CHANGED". */
int jk_patch_square_help(const JkModel *m);
/* Lines in a CREDITS.txt text (for scrolling). */
int jk_credits_lines(const char *text);
/* The frame-rate item works once our ini can be there (not before PATCH). */
int jk_pace_enabled(const JkModel *m);
/* The badge text: "Official Build by …" only for JK_REL_OFFICIAL. */
const char *jk_badge_text(const JkRelease *r);

#endif
