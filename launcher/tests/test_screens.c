/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Layout rules every screen must keep (spec G2, G3). */
#include "jk_test.h"
#include "jk_art.h"
#include "jk_cases.h"
#include "jk_loader.h"

#include <stdlib.h>

#define PANEL_Y_T 280                      /* PANEL_Y in jk_screens.c */

static JkFrame frame_new(void)
{
    JkFrame f = { calloc((size_t)JK_W * JK_H, 4), JK_W, JK_H, JK_W * 4 };
    return f;
}
static uint32_t px(const JkFrame *f, int x, int y) { return ((const uint32_t *)(const void *)f->pixels)[(size_t)y * f->width + x]; }

/* Nothing but the background outside the title-safe area. */
static void safe_area_respected(void)
{
    JkFrame bg = frame_new(), f = frame_new();
    jk_background(&bg);
    static JkCase cases[JK_CASES_MAX];
    int n = jk_cases(cases);
    for (int i = 0; i < n; i++) {
        jk_screen_draw(&f, &cases[i].m);
        int bad = 0;
        for (int y = 0; y < JK_H && !bad; y++)
            for (int x = 0; x < JK_W; x++) {
                int inside = x >= JK_SAFE_X0 && x < JK_SAFE_X1 && y >= JK_SAFE_Y0 && y < JK_SAFE_Y1;
                /* the dialog dims the whole screen: compare its shade only */
                if (!inside && cases[i].m.dialog) continue;
                if (!inside && px(&f, x, y) != px(&bg, x, y)) { bad = 1; fprintf(stderr, "    %s: pixel at %d,%d\n", cases[i].name, x, y); break; }
            }
        CHECK(!bad);
    }
    free(bg.pixels); free(f.pixels);
}

/* Every screen shows button hints at the bottom left. */
static void hint_bar_present_every_screen(void)
{
    JkFrame bg = frame_new(), f = frame_new();
    jk_background(&bg);
    static JkCase cases[JK_CASES_MAX];
    int n = jk_cases(cases);
    for (int i = 0; i < n; i++) {
        if (cases[i].m.phase == JK_PH_INSTALLING) continue;     /* nothing to press while installing */
        if (cases[i].m.ctl_listen) continue;                    /* CONTROLS listening: any button is the answer */
        jk_screen_draw(&f, &cases[i].m);
        int chalk = 0;
        for (int y = 950; y < 1010; y++)
            for (int x = 120; x < 400; x++) chalk += px(&f, x, y) == JK_RGB_CHALK;
        if (chalk < 50) fprintf(stderr, "    %s: no hint bar\n", cases[i].name);
        CHECK(chalk >= 50);
    }
    free(bg.pixels); free(f.pixels);
}

/* Drawing is a pure function of the model. */
static void draw_deterministic(void)
{
    JkFrame a = frame_new(), b = frame_new();
    static JkCase cases[JK_CASES_MAX];
    int n = jk_cases(cases);
    for (int i = 0; i < n; i++) {
        jk_screen_draw(&a, &cases[i].m);
        jk_screen_draw(&b, &cases[i].m);
        CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) == 0);
    }
    free(a.pixels); free(b.pixels);
}

/* The focused Home item sits on a light plate with dark text. */
static void home_focus_visible(void)
{
    JkFrame f = frame_new();
    JkModel m = case_base(JK_SCR_HOME, JK_ST_READY);
    for (int item = 0; item < JK_HOME_COUNT; item++) {
        m.focus = item;
        jk_screen_draw(&f, &m);
        int y = JK_HOME_Y0 + item * JK_HOME_STEP + 40, chalk = 0, ink = 0;
        for (int x = JK_HOME_X; x < JK_HOME_X + 200; x++) { chalk += px(&f, x, y) == JK_RGB_CHALK; ink += px(&f, x, y) == JK_RGB_INK; }
        CHECK(chalk > 40);
        CHECK(ink > 5);
    }
    free(f.pixels);
}

/* Non-background pixels in a box. */
static int changed_in(const JkFrame *f, const JkFrame *bg, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += px(f, x, y) != px(bg, x, y);
    return n;
}

/* Owner requirement 2026-11-23: the release badge is in the top-right corner
 * of every screen. */
static void badge_on_every_screen(void)
{
    JkFrame bg = frame_new(), f = frame_new();
    jk_background(&bg);
    static JkCase cases[JK_CASES_MAX];
    int n = jk_cases(cases);
    for (int i = 0; i < n; i++) {
        JkModel m = cases[i].m;
        m.dialog = JK_DLG_NONE;                       /* a dialog dims the corner too */
        jk_screen_draw(&f, &m);
        int c = changed_in(&f, &bg, 1100, JK_BADGE_Y, JK_BADGE_RIGHT, JK_BADGE_Y + 40);
        if (c < 100) fprintf(stderr, "    %s: no badge\n", cases[i].name);
        CHECK(c >= 100);
    }
    free(bg.pixels); free(f.pixels);
}

static void badge_text_follows_release_state(void)
{
    JkRelease r;
    memset(&r, 0, sizeof r);
    CHECK(!strcmp(jk_badge_text(&r), "Unofficial build"));
    r.state = JK_REL_CHANGED;
    CHECK(!strcmp(jk_badge_text(&r), "Unofficial build"));
    r.state = JK_REL_OFFICIAL;
    CHECK(!strcmp(jk_badge_text(&r), "Official Build by XxXJoker13XxX"));
    CHECK(!strcmp(jk_badge_text(NULL), "Unofficial build"));
    /* r5 (owner): the badge carries no key fingerprint; another key draws the same (About shows it) */
    JkFrame a = frame_new(), b = frame_new();
    JkModel m = case_base(JK_SCR_HOME, JK_ST_READY);
    m.release = case_official();
    jk_screen_draw(&a, &m);
    m.release.key_id = "99990000";
    jk_screen_draw(&b, &m);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) == 0);
    free(a.pixels); free(b.pixels);
}

/* The focused Settings item sits on a light plate with dark text. */
static void settings_focus_visible(void)
{
    JkFrame f = frame_new();
    JkModel m = case_base(JK_SCR_SETTINGS, JK_ST_READY);
    for (int item = 0; item < JK_SET_COUNT; item++) {
        m.set_focus = item;
        jk_screen_draw(&f, &m);
        int y = JK_SET_Y0 + item * JK_SET_STEP + 36, chalk = 0, ink = 0;
        for (int x = JK_SET_X; x < JK_SET_X + 200; x++) { chalk += px(&f, x, y) == JK_RGB_CHALK; ink += px(&f, x, y) == JK_RGB_INK; }
        CHECK(chalk > 40);
        CHECK(ink > 5);
    }
    free(f.pixels);
}

static void pace_enabled_only_with_our_files(void)
{
    JkModel m = case_base(JK_SCR_SETTINGS, JK_ST_READY);
    CHECK(jk_pace_enabled(&m));
    m.check.state = JK_ST_CHANGED;
    CHECK(jk_pace_enabled(&m));
    m.check.state = JK_ST_UPDATE;
    CHECK(jk_pace_enabled(&m));
    m.check.state = JK_ST_NOT_INSTALLED;
    CHECK(!jk_pace_enabled(&m));
    m.check.state = JK_ST_NO_GAME;
    CHECK(!jk_pace_enabled(&m));
}

static void patch_primary_actions(void)
{
    JkModel m = case_base(JK_SCR_PATCH, JK_ST_NO_GAME);
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_CHECK_AGAIN);
    m.check.state = JK_ST_NOT_INSTALLED;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_INSTALL);
    m.check.state = JK_ST_READY;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_START);
    m.phase = JK_PH_FAILED; m.check.state = JK_ST_NOT_INSTALLED;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_INSTALL);
    m.phase = JK_PH_INSTALLING;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_NONE);
    m.phase = JK_PH_IDLE; m.check.state = JK_ST_BAD_PACKAGE;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_NONE);
    m.check.state = JK_ST_READY; m.check.has_backup = 1;
    CHECK(jk_patch_can_restore(&m));
    m.check.has_backup = 0;
    CHECK(!jk_patch_can_restore(&m));
}

/* Branding (lead decision 4): Home in every state, the failure and the
 * interrupted screens carry "JK4SPEED · Made by XxXJoker13XxX" bottom right. */
static void footer_on_every_home_state(void)
{
    CHECK(!strcmp(JK_S_FOOTER, "JK4SPEED \xC2\xB7 Made by XxXJoker13XxX"));
    JkFrame bg = frame_new(), f = frame_new();
    jk_background(&bg);
    static const JkState states[] = { JK_ST_BAD_PACKAGE, JK_ST_NO_GAME, JK_ST_WRONG_EXE, JK_ST_MISSING_FILES,
                                      JK_ST_NOT_INSTALLED, JK_ST_UPDATE, JK_ST_CHANGED, JK_ST_READY };
    int n = (int)(sizeof states / sizeof states[0]);
    int w = jk_text_width(JK_FONT_HINT, JK_S_FOOTER);
    for (int i = 0; i < n + 3; i++) {
        JkModel m = case_base(i < n ? JK_SCR_HOME : i == n ? JK_SCR_FAILED : i == n + 1 ? JK_SCR_INTERRUPTED : JK_SCR_HOME,
                              i < n ? states[i] : JK_ST_READY);
        m.build_id = NULL;                            /* only the footer may draw there */
        if (i == n + 2) m.game_other = "/data/Jk4Speed/nfsmw";
        jk_screen_draw(&f, &m);
        int c = changed_in(&f, &bg, JK_BADGE_RIGHT - w, 956, JK_BADGE_RIGHT - w / 2, 1000);
        if (c < 100) fprintf(stderr, "    case %d: no footer\n", i);
        CHECK(c >= 100);
    }
    free(bg.pixels); free(f.pixels);
}

/* Settings: eight items in the brief's order (1.11: VISUALS after MODS); Show FPS / ms reads OFF by default. */
static void settings_labels_in_order(void)
{
    static const char *const want[] = { "FRAME RATE", "SHOW FPS / MS", "MODS", "VISUALS", "CONTROLS", "SAVE GAMES", "LOGS & SUPPORT", "ABOUT" };
    CHECK_EQ(JK_SET_COUNT, 8);
    for (int i = 0; i < 8; i++) CHECK(!strcmp(jk_settings_label(i), want[i]));
    CHECK(JK_SET_Y0 + 7 * JK_SET_STEP + JK_SET_ITEM_H < 940);          /* the list clears the hint bar */
    JkModel m = case_base(JK_SCR_SETTINGS, JK_ST_READY);
    CHECK(!strcmp(jk_settings_value(&m, JK_SET_FPS_HUD), "OFF"));
    m.fps_hud = 1;
    CHECK(!strcmp(jk_settings_value(&m, JK_SET_FPS_HUD), "ON"));
    CHECK(!strcmp(jk_settings_value(&m, JK_SET_FRAMERATE), "30 FPS"));
    CHECK(jk_settings_value(&m, JK_SET_MODS) == NULL);
}

/* MODS: each JK_MS_ROW_* has its exact words, and the screen draws them. */
/* 1.00 final (owner, 2026-11-24): while PATCH works before the first counted
 * step, the bar still moves at each update, and the screen asks for patience. */
static void patch_bar_moves_before_first_step(void)
{
    JkFrame a = frame_new(), b = frame_new();
    JkModel m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED);
    m.phase = JK_PH_INSTALLING;
    m.prog_done = 0; m.prog_total = 60;
    snprintf(m.prog_file, sizeof m.prog_file, "Reading pack.zip (120 MB)");
    m.prog_ticks = 1;
    jk_screen_draw(&a, &m);
    m.prog_ticks = 2;
    jk_screen_draw(&b, &m);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) != 0);
    CHECK(strstr(JK_S_P_PATIENCE, "about 5 minutes") != NULL);
    m.screen = JK_SCR_MODS;
    jk_screen_draw(&a, &m);
    m.prog_ticks = 3;
    jk_screen_draw(&b, &m);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) != 0);
    free(a.pixels); free(b.pixels);
}

static void mods_rows_follow_scan(void)
{
    static const struct { int row; const char *text; } rows[] = {
        { JK_MS_ROW_ON_INSTALLED, "Installed" },
        { JK_MS_ROW_ON_PENDING, "Installs at the next Patch" },
        { JK_MS_ROW_ON_NO_ARCHIVE, "Download not found" },
        { JK_MS_ROW_OFF_PENDING, "Removed at the next Patch" },
        { JK_MS_ROW_OFF, "Off" },
        { JK_MS_ROW_BAD, "Archive damaged" },
    };
    JkFrame a = frame_new(), b = frame_new();
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        CHECK(!strcmp(jk_mods_row_text(rows[i].row), rows[i].text));
        JkModel m = case_base(JK_SCR_MODS, JK_ST_READY);
        m.mods.row[JK_MOD_H] = rows[i].row;
        jk_screen_draw(&a, &m);
        m.mods.row[JK_MOD_H] = rows[(i + 1) % 6].row;
        jk_screen_draw(&b, &m);
        CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) != 0);
    }
    free(a.pixels); free(b.pixels);
}

/* The no-clean-original failure: exact words, at most three names. */
static void no_clean_original_text(void)
{
    JkModel m = case_base(JK_SCR_PATCH, JK_ST_READY);
    m.phase = JK_PH_FAILED;
    m.no_pristine = 1;
    strcpy(m.mods.pristine_needed[0], "GLOBAL/InGameA.bun");
    m.mods.npristine_needed = 1;
    char body[600];
    jk_no_pristine_body(&m, body, sizeof body);
    CHECK(!strcmp(body, "Patch needs an unchanged copy of these game files to turn mods on or off, and none was found: "
                        "GLOBAL/InGameA.bun. Your game still plays as it is now. Press \xE2\x96\xA1 for Help."));
    strcpy(m.mods.pristine_needed[1], "GLOBAL/InGameB.lzc");
    strcpy(m.mods.pristine_needed[2], "TRACKS/ROUTESL2RA/Paths2.bun");
    strcpy(m.mods.pristine_needed[3], "GLOBAL/HUD.bin");
    m.mods.npristine_needed = 5;
    jk_no_pristine_body(&m, body, sizeof body);
    CHECK(strstr(body, "found: GLOBAL/InGameA.bun, GLOBAL/InGameB.lzc, TRACKS/ROUTESL2RA/Paths2.bun and 2 more. Your") != NULL);
    CHECK(strstr(body, "HUD.bin") == NULL);
    CHECK(!strcmp(JK_S_NP_HEAD, "MODS CAN'T BE CHANGED"));
}

static void interrupted_copy_exact(void)
{
    CHECK(!strcmp(JK_S_INT_HEAD, "PATCH WAS INTERRUPTED"));
    CHECK(!strcmp(JK_S_INT_BODY, "The last Patch stopped before it finished. The console lost power or the app was closed. "
                                 "Every game file is back the way it was before that Patch. Nothing was lost."));
    CHECK(!strcmp(JK_S_H_PATCH_AGAIN, "Patch again"));
    CHECK(!strcmp(JK_S_H_HOME, "Home"));
}

/* M-3: the Help texts name the active game's folders and saves */
static void help_texts_follow_the_game(void)
{
    char all[3][8192] = { "", "", "" };
    for (int side = 0; side < 3; side++) {
        JkModel m = case_base(JK_SCR_HELP, JK_ST_READY);
        if (side) { m.ngames = 2; m.active = side - 1; }   /* 0: one game (1.0); 1: slot 0; 2: slot 1 */
        for (int pg = 0; pg < JK_HELP_COUNT; pg++)
            for (int i = 0; i < jk_help_count(pg); i++) {
                strcat(all[side], jk_help_text(&m, pg, i));
                strcat(all[side], "\n");
            }
    }
    for (int side = 0; side < 2; side++) {                 /* the single game and slot 0 read as before */
        CHECK(strstr(all[side], "/data/Jk4Speed/NFSMW/speed.exe"));
        CHECK(strstr(all[side], "/data/Jk4Speed/Saves\n"));
        CHECK(!strstr(all[side], "Saves-NFS"));
    }
    CHECK(strstr(all[2], "/data/Jk4Speed/NFS\n"));
    CHECK(strstr(all[2], "/data/Jk4Speed/NFS/speed.exe"));
    CHECK(strstr(all[2], "/data/Jk4Speed/Saves-NFS\n"));
    CHECK(!strstr(all[2], "NFSMW"));
}

/* Help: four pages, each draws something different. */
static void help_pages_draw(void)
{
    static const char *const want[] = { "GAME FOLDER", "MODS", "SAVE GAMES", "TROUBLESHOOTING" };
    CHECK_EQ(JK_HELP_COUNT, 4);
    JkFrame a = frame_new(), b = frame_new();
    for (int i = 0; i < 4; i++) {
        CHECK(!strcmp(jk_help_title(i), want[i]));
        JkModel m = case_base(JK_SCR_HELP, JK_ST_READY);
        m.help_page = i;
        jk_screen_draw(&a, &m);
        m.help_page = (i + 1) % 4;
        jk_screen_draw(&b, &m);
        CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) != 0);
    }
    free(a.pixels); free(b.pixels);
}

/* Two game folders: named, and PLAY / PATCH blocked. */
static void two_folders_blocks_patch(void)
{
    JkModel m = case_base(JK_SCR_PATCH, JK_ST_NOT_INSTALLED);
    m.game_dir = "/data/Jk4Speed/NFSMW";
    m.game_other = "/data/Jk4Speed/nfsmw";
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_NONE);
    CHECK(!jk_patch_can_mods(&m));
    m.check.has_backup = 1;
    CHECK(!jk_patch_can_restore(&m));
    char body[400];
    jk_two_folders_body(&m, body, sizeof body);
    CHECK(strstr(body, "/data/Jk4Speed/NFSMW and /data/Jk4Speed/nfsmw") != NULL);
}

/* I-2: a Patch failure says "not changed" only where the step proves it. */
static void failure_text_says_what_is_true(void)
{
    char body[700];
    JkModel m = case_base(JK_SCR_PATCH, JK_ST_READY);
    m.phase = JK_PH_FAILED;
    m.fail_kind = JK_FAIL_PENDING;
    snprintf(m.msg, sizeof m.msg, "%s", JK_S_E_SWAP_NEXT);
    jk_patch_failure_body(&m, body, sizeof body);
    CHECK(strstr(body, "Could not put the mod files in place. Start Jk4Speed again") == body);
    CHECK(strstr(body, "not changed") == NULL && strstr(body, "as before") == NULL);
    m.fail_kind = JK_FAIL_MODS;
    snprintf(m.msg, sizeof m.msg, "%s", "not enough free space: 900 MB needed, 10 MB free");
    jk_patch_failure_body(&m, body, sizeof body);
    CHECK(strstr(body, "Not enough free space: 900 MB needed, 10 MB free. The Jk4Speed files are installed; the mod files were not changed.") == body);
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_INSTALL);        /* "Press ✕ to try again": ✕ is Try again, not Play */
    m.no_pristine = 1;
    CHECK_EQ(jk_patch_primary(&m), JK_ACT_START);          /* "Your game still plays as it is now" */
    m.no_pristine = 0;
    m.fail_kind = JK_FAIL_CORE;
    m.check.has_backup = 0;
    jk_patch_failure_body(&m, body, sizeof body);
    CHECK(strstr(body, "not changed") == NULL && strstr(body, "installed") == NULL);
    m.check.has_backup = 1;
    jk_patch_failure_body(&m, body, sizeof body);
    CHECK(strstr(body, "Restore puts back") != NULL);
    m.fail_kind = JK_FAIL_RESTORE;
    jk_patch_failure_body(&m, body, sizeof body);
    CHECK(strstr(body, "Restore did not finish") != NULL);
}

/* I-3: one name per mod on the MODS rows and in the engine's reasons. */
static void one_name_per_mod(void)
{
    CHECK(!strcmp(JK_S_M_ROW_X, jk_modset_name(JK_MOD_X)));
    CHECK(!strcmp(JK_S_M_ROW_H, jk_modset_name(JK_MOD_H)));
    CHECK(!strcmp(JK_S_M_ROW_R, jk_modset_name(JK_MOD_R)));
    CHECK(!strcmp(JK_S_H_MODS, "Mods"));
}

static void loader_frames_differ_and_loop(void)
{
    JkFrame a = frame_new(), b = frame_new();
    jk_loader_draw(&a, 0);
    jk_loader_draw(&b, 1500);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) != 0);
    jk_loader_draw(&b, 3000);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)JK_W * JK_H * 4) == 0);
    jk_loader_release();
    free(a.pixels); free(b.pixels);
}

static int region_same(const JkFrame *a, const JkFrame *b, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (px(a, x, y) != px(b, x, y)) return 0;
    return 1;
}

/* 1.1: the chosen half has the accent frame; the other has none. */
static void home_two_accent_frame_follows_active(void)
{
    JkFrame f = frame_new();
    int x_left = JK_HALF_X0 + 100, x_right = JK_HALF_X0 + JK_HALF_W + JK_HALF_GAP + 100, y = JK_HALF_Y + 1;
    JkModel m = case_two(JK_SCR_HOME, JK_ST_READY, JK_ST_READY, 0);
    jk_screen_draw(&f, &m);
    CHECK_EQ(px(&f, x_left, y), JK_RGB_DUSK);
    CHECK(px(&f, x_right, y) != JK_RGB_DUSK);
    m.active = 1;
    jk_screen_draw(&f, &m);
    CHECK(px(&f, x_left, y) != JK_RGB_DUSK);
    CHECK_EQ(px(&f, x_right, y), JK_RGB_DUSK);
    free(f.pixels);
}

/* 1.1: a side without a game is dimmed and says "copy the game here"; the other side is not touched. */
static void home_two_dims_a_side_without_game(void)
{
    JkFrame a = frame_new(), b = frame_new();
    JkModel m = case_two(JK_SCR_HOME, JK_ST_READY, JK_ST_READY, 0);
    jk_screen_draw(&a, &m);
    m.slot_state[1] = JK_ST_NO_GAME;
    jk_screen_draw(&b, &m);
    int rx = JK_HALF_X0 + JK_HALF_W + JK_HALF_GAP;
    CHECK(region_same(&a, &b, JK_HALF_X0, JK_HALF_Y, JK_HALF_X0 + JK_HALF_W, JK_HALF_Y + JK_HALF_H));
    CHECK(!region_same(&a, &b, rx, JK_HALF_Y, rx + JK_HALF_W, JK_HALF_Y + JK_HALF_H));
    /* the text is there: the status line of the dimmed half has SMOKE pixels */
    int smoke = 0;
    for (int y = JK_HALF_Y; y < JK_HALF_Y + JK_HALF_H; y++)
        for (int x = rx + 20; x < rx + JK_HALF_W; x++) smoke += px(&b, x, y) == JK_RGB_SMOKE;
    CHECK(smoke > 200);
    CHECK(strstr(JK_S_H_COPY_HERE, "COPY THE GAME HERE") != NULL);
    free(a.pixels); free(b.pixels);
}

/* 1.1: every screen but Home names the chosen game under the badge. */
static void header_names_the_active_game(void)
{
    JkFrame a = frame_new(), b = frame_new(), c = frame_new();
    JkModel two = case_two(JK_SCR_SETTINGS, JK_ST_READY, JK_ST_READY, 0), one = two;
    one.ngames = 0;
    jk_screen_draw(&a, &two);
    jk_screen_draw(&b, &one);
    int x0 = JK_BADGE_RIGHT - 600, y0 = JK_BADGE_Y + 40, y1 = JK_BADGE_Y + 90;
    CHECK(!region_same(&a, &b, x0, y0, JK_BADGE_RIGHT + 2, y1));
    two.active = 1;
    jk_screen_draw(&c, &two);
    CHECK(!region_same(&a, &c, x0, y0, JK_BADGE_RIGHT + 2, y1));      /* the other game's name */
    free(a.pixels); free(b.pixels); free(c.pixels);
}

/* 1.1: NFS has no mods: its Settings has no MODS row; the unknown exe is said on Patch. */
static void nfs_screens(void)
{
    JkFrame a = frame_new(), b = frame_new();
    JkModel m = case_two(JK_SCR_SETTINGS, JK_ST_READY, JK_ST_READY, 1);
    jk_screen_draw(&a, &m);
    m.no_mods = 0;
    jk_screen_draw(&b, &m);
    CHECK(!region_same(&a, &b, JK_SET_X, JK_SET_Y0 + 2 * JK_SET_STEP, JK_SET_X + 600, JK_SET_Y0 + 2 * JK_SET_STEP + JK_SET_ITEM_H));
    m = case_two(JK_SCR_PATCH, JK_ST_READY, JK_ST_NOT_INSTALLED, 1);
    jk_screen_draw(&a, &m);
    m.check.exe_known = 1;
    jk_screen_draw(&b, &m);
    CHECK(!region_same(&a, &b, 150, PANEL_Y_T, 890, PANEL_Y_T + 300));
    CHECK(!jk_patch_can_mods(&m));
    free(a.pixels); free(b.pixels);
}

int main(void)
{
    RUN(home_two_accent_frame_follows_active);
    RUN(home_two_dims_a_side_without_game);
    RUN(header_names_the_active_game);
    RUN(nfs_screens);
    RUN(safe_area_respected);
    RUN(hint_bar_present_every_screen);
    RUN(draw_deterministic);
    RUN(home_focus_visible);
    RUN(badge_on_every_screen);
    RUN(badge_text_follows_release_state);
    RUN(settings_focus_visible);
    RUN(pace_enabled_only_with_our_files);
    RUN(patch_primary_actions);
    RUN(loader_frames_differ_and_loop);
    RUN(footer_on_every_home_state);
    RUN(settings_labels_in_order);
    RUN(mods_rows_follow_scan);
    RUN(patch_bar_moves_before_first_step);
    RUN(no_clean_original_text);
    RUN(interrupted_copy_exact);
    RUN(help_pages_draw);
    RUN(help_texts_follow_the_game);
    RUN(two_folders_blocks_patch);
    RUN(failure_text_says_what_is_true);
    RUN(one_name_per_mod);
    jk_art_release();
    return jk_test_report("test_screens");
}
