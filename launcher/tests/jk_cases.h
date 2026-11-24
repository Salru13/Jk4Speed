/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Every screen and state (spec 7, owner decisions 2026-11-23), as models
 * (goldens and checks). */
#ifndef JK_CASES_H
#define JK_CASES_H

#include "jk_screens.h"
#include "jk_strings.h"
#include "jk_fs.h"

#include <string.h>

#define JK_CASES_MAX 96

typedef struct JkCase { const char *name; JkModel m; } JkCase;

static JkModel case_base(JkScreenId s, JkState st)
{
    JkModel m;
    memset(&m, 0, sizeof m);
    m.screen = s;
    m.check.state = st;
    m.pace = JK_PACE_30;
    m.build_id = "JK4SPEED 1.0";
    m.saves_profiles = -1;
    m.release.state = JK_REL_UNOFFICIAL;          /* no signing yet: what every build shows today */
    return m;
}

static JkRelease case_official(void)
{
    JkRelease r;
    memset(&r, 0, sizeof r);
    r.state = JK_REL_OFFICIAL;
    r.version = "1.0.0";
    r.key_label = "JK4SPEED release key 2026";
    r.key_id = "4A1F2B3C";
    return r;
}

/* What an import preview shows: a PC save, a console export, one already here. */
static void case_saves_found(JkSavesFound *f)
{
    memset(f, 0, sizeof *f);
    static const char *const alias[3] = { "GOATS", "RAZOR", "JOKER" };
    static const long long when[3] = { 1357171200LL, 1795000000LL, 1795400000LL };
    for (int i = 0; i < 3; i++) {
        snprintf(f->c[i].alias, sizeof f->c[i].alias, "%s", alias[i]);
        f->c[i].mtime = when[i];
        f->c[i].console = i == 1;
    }
    f->c[2].exists = 1;
    snprintf(f->c[2].keep_as, sizeof f->c[2].keep_as, "JOKE2");
    f->n = 3;
    f->collisions = 1;
    f->rejected = 1;
    snprintf(f->rejected_name, sizeof f->rejected_name, "NOTES/NOTES");
}

/* Fills out[] (at least JK_CASES_MAX entries). Returns the count. */
static int jk_cases(JkCase *out)
{
    int n = 0;
    JkModel m;

    m = case_base(JK_SCR_HOME, JK_ST_READY); m.check.has_backup = 1;
    out[n++] = (JkCase){ "home-ready", m };
    m = case_base(JK_SCR_HOME, JK_ST_READY); m.check.has_backup = 1; m.release = case_official();
    out[n++] = (JkCase){ "home-ready-official", m };
    m = case_base(JK_SCR_HOME, JK_ST_NOT_INSTALLED);
    out[n++] = (JkCase){ "home-not-installed", m };
    m = case_base(JK_SCR_HOME, JK_ST_NO_GAME);
    out[n++] = (JkCase){ "home-no-game", m };
    m = case_base(JK_SCR_HOME, JK_ST_WRONG_EXE); strcpy(m.check.found_exe, "1a2b3c4d");
    out[n++] = (JkCase){ "home-wrong-exe", m };
    m = case_base(JK_SCR_HOME, JK_ST_UPDATE); m.check.has_backup = 1;
    out[n++] = (JkCase){ "home-update", m };
    m = case_base(JK_SCR_HOME, JK_ST_READY); m.focus = JK_HOME_SETTINGS; m.check.has_backup = 1;
    out[n++] = (JkCase){ "home-settings-focus", m };

    m = case_base(JK_SCR_PATCH, JK_ST_NO_GAME);
    out[n++] = (JkCase){ "patch-no-game", m };
    m = case_base(JK_SCR_PATCH, JK_ST_WRONG_EXE); strcpy(m.check.found_exe, "1a2b3c4d");
    out[n++] = (JkCase){ "patch-wrong-exe", m };
    m = case_base(JK_SCR_PATCH, JK_ST_MISSING_FILES);
    strcpy(m.check.first_missing, "DATA/example-file.bin"); m.check.missing_count = 4;
    out[n++] = (JkCase){ "patch-missing", m };
    m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED);
    out[n++] = (JkCase){ "patch-ready-to-install", m };
    m = case_base(JK_SCR_PATCH, JK_ST_CHANGED); m.check.has_backup = 1;
    out[n++] = (JkCase){ "patch-changed", m };
    m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED); m.phase = JK_PH_INSTALLING;
    m.prog_done = 5; m.prog_total = 12; strcpy(m.prog_file, "TRACKS/STREAML2RA.BUN");
    out[n++] = (JkCase){ "patch-installing", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.phase = JK_PH_DONE; m.check.has_backup = 1;
    out[n++] = (JkCase){ "patch-done", m };
    m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED); m.phase = JK_PH_FAILED;
    strcpy(m.msg, "not enough free space: 1082 MB needed, 640 MB free");
    out[n++] = (JkCase){ "patch-failed", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.phase = JK_PH_FAILED; m.fail_kind = JK_FAIL_MODS; m.check.has_backup = 1;
    snprintf(m.msg, sizeof m.msg, JK_S_E_SPACE, "1190", "640");
    out[n++] = (JkCase){ "patch-failed-mods", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.phase = JK_PH_FAILED; m.fail_kind = JK_FAIL_PENDING; m.check.has_backup = 1;
    snprintf(m.msg, sizeof m.msg, "%s", JK_S_E_SWAP_NEXT);
    out[n++] = (JkCase){ "patch-failed-pending", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.dialog = JK_DLG_RESTORE; m.check.has_backup = 1;
    out[n++] = (JkCase){ "patch-restore-dialog", m };
    m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED); m.phase = JK_PH_RESTORED;
    out[n++] = (JkCase){ "patch-restored", m };
    m = case_base(JK_SCR_PATCH, JK_ST_BAD_PACKAGE); strcpy(m.check.reason, "package manifest line 3: bad sha256");
    out[n++] = (JkCase){ "patch-bad-package", m };

    /* the mod set (rows from the scan): pack installs at the next Patch, HUD's
     * archive damaged, roads ON without their download */
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = 0;
    m.mods.mod[JK_MOD_X].state = JK_MS_READY; strcpy(m.mods.mod[JK_MOD_X].archive, "NFSMW X360 Stuff Pack v4.2-20260101T000000Z-001.zip");
    m.mods.mod[JK_MOD_H].state = JK_MS_BAD; strcpy(m.mods.mod[JK_MOD_H].archive, "NFSMWUHUD01022026b.7z");
    snprintf(m.mods.mod[JK_MOD_H].note, sizeof m.mods.mod[JK_MOD_H].note, JK_S_E_NOT_RELEASED, "NFSMWUHUD01022026b.7z", JK_S_E_NAME_H);
    m.mods.want = m.mods.target = 1; m.mods.on = JK_MS_DEFAULT_ON; m.mods_focus = JK_MOD_H;
    m.mods.row[JK_MOD_X] = JK_MS_ROW_ON_PENDING; m.mods.row[JK_MOD_H] = JK_MS_ROW_BAD; m.mods.row[JK_MOD_R] = JK_MS_ROW_ON_NO_ARCHIVE;
    out[n++] = (JkCase){ "mods", m };
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.mods.installed = -1; m.mods.on = JK_MS_DEFAULT_ON;
    for (int k = 0; k < JK_MOD_N; k++) m.mods.row[k] = JK_MS_ROW_ON_NO_ARCHIVE;
    out[n++] = (JkCase){ "mods-none", m };
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = 7; m.mods.want = m.mods.target = 7; m.mods_state = 1; m.mods_done = 26;
    m.mods.on = JK_MS_DEFAULT_ON;
    for (int k = 0; k < JK_MOD_N; k++) { m.mods.mod[k].state = JK_MS_READY; m.mods.row[k] = JK_MS_ROW_ON_INSTALLED; }
    strcpy(m.mods.mod[0].archive, "NFSMW X360 Stuff Pack v4.2.zip");
    strcpy(m.mods.mod[1].archive, "NFSMWUHUD01022026b.7z");
    strcpy(m.mods.mod[2].archive, "nfsmw-hd-road-textures.zip");
    m.x360_required = 1;                                 /* r5: as the title runs it (pack row REQUIRED) */
    out[n++] = (JkCase){ "mods-installed", m };
    /* the roads focused and turned OFF: still installed, out at the next PATCH */
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = 7; m.mods.want = m.mods.target = 3;
    m.mods.on = JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_H); m.mods_state = 2;
    for (int k = 0; k < JK_MOD_N; k++) { m.mods.mod[k].state = JK_MS_READY; m.mods.row[k] = JK_MS_ROW_ON_INSTALLED; }
    m.mods.row[JK_MOD_R] = JK_MS_ROW_OFF_PENDING;
    m.mods_focus = JK_MOD_R;
    out[n++] = (JkCase){ "mods-roads-off", m };
    /* the pack OFF, HUD and roads ON: any subset works */
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = 0; m.mods.want = m.mods.target = 6;
    m.mods.on = JK_MS_BIT(JK_MOD_H) | JK_MS_BIT(JK_MOD_R);
    for (int k = 0; k < JK_MOD_N; k++) m.mods.mod[k].state = JK_MS_READY;
    m.mods.row[JK_MOD_X] = JK_MS_ROW_OFF; m.mods.row[JK_MOD_H] = JK_MS_ROW_ON_PENDING; m.mods.row[JK_MOD_R] = JK_MS_ROW_ON_PENDING;
    m.mods_focus = JK_MOD_X; m.mods_state = 2;
    out[n++] = (JkCase){ "mods-pack-off", m };
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = -1; m.mods_state = -1; m.mods.on = JK_MS_DEFAULT_ON;
    for (int k = 0; k < JK_MOD_N; k++) { m.mods.mod[k].state = JK_MS_READY; m.mods.row[k] = JK_MS_ROW_ON_PENDING; }
    strcpy(m.mods_msg, "not enough free space: 1190 MB needed, 640 MB free");
    out[n++] = (JkCase){ "mods-failed", m };
    /* the roads ON and installed, their download gone: kept; the HUD can't be built */
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.mods.installed = 4; m.mods.on = JK_MS_DEFAULT_ON; m.mods.want = m.mods.target = 5;
    m.mods.mod[JK_MOD_X].state = JK_MS_READY;
    m.mods.row[JK_MOD_X] = JK_MS_ROW_ON_PENDING; m.mods.row[JK_MOD_H] = JK_MS_ROW_ON_NO_ARCHIVE; m.mods.row[JK_MOD_R] = JK_MS_ROW_ON_INSTALLED;
    m.mods.problems = 1; snprintf(m.mods.problem[0], sizeof m.mods.problem[0], JK_S_E_NEED_DL, JK_S_E_NAME_H, "GLOBAL/InGameA.bun");
    m.mods_focus = JK_MOD_H;
    out[n++] = (JkCase){ "mods-no-archive", m };
    m = case_base(JK_SCR_MODS, JK_ST_READY); m.usb = 1; m.phase = JK_PH_INSTALLING;
    m.prog_done = 3; m.prog_total = 10; strcpy(m.prog_file, "Reading NFSMWUHUD01022026b.7z");
    out[n++] = (JkCase){ "mods-installing", m };

    m = case_base(JK_SCR_SETTINGS, JK_ST_READY);
    out[n++] = (JkCase){ "settings-fps30", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.pace = JK_PACE_60;
    out[n++] = (JkCase){ "settings-fps60", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.pace = JK_PACE_OTHER;
    out[n++] = (JkCase){ "settings-fps-other", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_NOT_INSTALLED);
    out[n++] = (JkCase){ "settings-fps-disabled", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.set_focus = JK_SET_MODS;
    out[n++] = (JkCase){ "settings-mods-focus", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.set_focus = JK_SET_SAVES;
    out[n++] = (JkCase){ "settings-saves-focus", m };

    m = case_base(JK_SCR_CONTROLS, JK_ST_READY);
    out[n++] = (JkCase){ "controls", m };

    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 2; m.usb = 1;
    out[n++] = (JkCase){ "saves", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY);
    out[n++] = (JkCase){ "saves-none", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 2; m.usb = 1; m.saves_state = 1;
    strcpy(m.saves_msg, "/mnt/usb0/Jk4Speed/saves/2026-11-23_2100");
    out[n++] = (JkCase){ "saves-exported", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 3; m.usb = 1; m.saves_state = 2;
    strcpy(m.saves_msg, "/data/Jk4Speed/backup/saves/2026-11-23_2110");
    out[n++] = (JkCase){ "saves-imported", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 2; m.saves_state = -1;
    strcpy(m.saves_msg, "No USB drive found. Plug one in and try again.");
    out[n++] = (JkCase){ "saves-failed", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 0; m.usb = 1;     /* the console today: folder there, empty */
    out[n++] = (JkCase){ "saves-empty", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 1; m.usb = 1; m.dialog = JK_DLG_IMPORT;
    case_saves_found(&m.saves_found);
    out[n++] = (JkCase){ "saves-import-preview", m };
    m.dialog = JK_DLG_COLLIDE;
    out[n++] = (JkCase){ "saves-collision", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 2; m.usb = 1; m.dialog = JK_DLG_PICK;
    strcpy(m.save_names[0], "GOATS"); strcpy(m.save_names[1], "NAME"); m.save_count = 2; m.save_pick = 1;
    out[n++] = (JkCase){ "saves-rename-pick", m };
    m.dialog = JK_DLG_KEYBOARD;
    jk_keyboard_init(&m.kb, JK_S_R_KB, "NAME", JK_ALIAS_MAX, jk_alias_char);
    m.kb.row = 1; m.kb.col = 3;
    out[n++] = (JkCase){ "keyboard", m };
    m.kb.shift = 1; m.kb.row = 4; m.kb.col = 9;
    out[n++] = (JkCase){ "keyboard-shift-ok", m };
    static char *credits;
    if (!credits) credits = jk_fs_read(JK_ROOT_DIR "/assets/CREDITS.txt", 64 * 1024, NULL);
    m = case_base(JK_SCR_CREDITS, JK_ST_READY); m.credits = credits;
    out[n++] = (JkCase){ "credits", m };
    m.credits_top = 24;
    out[n++] = (JkCase){ "credits-scrolled", m };
    m = case_base(JK_SCR_SAVES, JK_ST_READY); m.saves_profiles = 1; m.saves_state = 3;
    strcpy(m.saves_msg, "/data/Jk4Speed/backup/saves/2026-11-23_2200");
    out[n++] = (JkCase){ "saves-renamed", m };

    /* LOGS & SUPPORT */
    m = case_base(JK_SCR_LOGS, JK_ST_READY); m.log_files = 3; m.log_bytes = 3LL * 1024 * 1024 + 52428;
    out[n++] = (JkCase){ "logs", m };
    m = case_base(JK_SCR_LOGS, JK_ST_READY); m.logs_on = 1; m.log_files = 1; m.log_bytes = 2048;
    strcpy(m.logs_msg, JK_S_L_LOGS_ON);
    out[n++] = (JkCase){ "logs-on", m };
    m = case_base(JK_SCR_LOGS, JK_ST_READY); m.logs_focus = JK_LOG_CLEAR; strcpy(m.logs_msg, "3 log files cleared.");
    out[n++] = (JkCase){ "logs-cleared", m };
    m = case_base(JK_SCR_LOGS, JK_ST_READY); m.logs_focus = JK_LOG_BUNDLE; m.log_files = 4; m.log_bytes = 9000;
    m.bundle_state = 1; m.bundle_usb = 1; strcpy(m.bundle_dir, "/data/Jk4Speed/Logs/support/2026-11-17_2105");
    out[n++] = (JkCase){ "support-saved", m };
    m = case_base(JK_SCR_LOGS, JK_ST_READY); m.logs_focus = JK_LOG_BUNDLE; m.bundle_state = -1;
    out[n++] = (JkCase){ "support-failed", m };

    /* Settings r4: Show FPS / ms on, and the Logs & Support item */
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.set_focus = JK_SET_FPS_HUD; m.fps_hud = 1;
    out[n++] = (JkCase){ "settings-fps-hud-on", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.set_focus = JK_SET_FPS_HUD;
    out[n++] = (JkCase){ "settings-fps-hud-off", m };
    m = case_base(JK_SCR_SETTINGS, JK_ST_READY); m.set_focus = JK_SET_LOGS;
    out[n++] = (JkCase){ "settings-logs-focus", m };

    /* Help, four pages */
    static const char *const help_names[JK_HELP_COUNT] = { "help-folder", "help-mods", "help-saves", "help-trouble" };
    for (int i = 0; i < JK_HELP_COUNT; i++) {
        m = case_base(JK_SCR_HELP, JK_ST_READY); m.help_page = i;
        out[n++] = (JkCase){ help_names[i], m };
    }

    /* Patch was interrupted; MODS CAN'T BE CHANGED; two game folders; logs on */
    m = case_base(JK_SCR_INTERRUPTED, JK_ST_READY);
    out[n++] = (JkCase){ "interrupted", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.phase = JK_PH_FAILED; m.no_pristine = 1; m.check.has_backup = 1;
    strcpy(m.mods.pristine_needed[0], "GLOBAL/InGameA.bun"); strcpy(m.mods.pristine_needed[1], "GLOBAL/GlobalB.lzc");
    strcpy(m.mods.pristine_needed[2], "TRACKS/STREAML2RA.BUN"); m.mods.npristine_needed = 4;
    out[n++] = (JkCase){ "patch-no-clean-original", m };
    m = case_base(JK_SCR_HOME, JK_ST_READY); m.game_dir = "/data/Jk4Speed/NFSMW"; m.game_other = "/data/Jk4Speed/nfsmw";
    out[n++] = (JkCase){ "home-two-folders", m };
    m = case_base(JK_SCR_PATCH, JK_ST_READY); m.game_dir = "/data/Jk4Speed/NFSMW"; m.game_other = "/data/Jk4Speed/nfsmw";
    out[n++] = (JkCase){ "patch-two-folders", m };
    m = case_base(JK_SCR_HOME, JK_ST_READY); m.check.has_backup = 1; m.logs_on = 1;
    out[n++] = (JkCase){ "home-logs-on", m };

    m = case_base(JK_SCR_ABOUT, JK_ST_READY);
    out[n++] = (JkCase){ "about-unofficial", m };
    m = case_base(JK_SCR_ABOUT, JK_ST_READY); m.release = case_official();
    out[n++] = (JkCase){ "about-official", m };
    m = case_base(JK_SCR_ABOUT, JK_ST_CHANGED); m.release = case_official(); m.release.state = JK_REL_CHANGED; m.release.changed = 2;
    out[n++] = (JkCase){ "about-changed", m };

    m = case_base(JK_SCR_FAILED, JK_ST_READY);
    m.failure = "The game stopped before its first picture."; m.failure_log = "session-12.log";
    out[n++] = (JkCase){ "failed", m };
    m = case_base(JK_SCR_FAILED, JK_ST_READY);
    m.failure = "The game stopped before its first picture."; m.failure_log = "session-12.log";
    m.bundle_state = 1; strcpy(m.bundle_dir, "/data/Jk4Speed/Logs/support/2026-11-17_2105");
    out[n++] = (JkCase){ "failed-saved", m };
    return n;
}

#endif
