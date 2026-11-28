/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The frontend state machine and the seam (spec 7, 10; owner decisions
 * 2026-11-23: PLAY / PATCH / SETTINGS), driven by pad presses. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_frontend.h"
#include "jk_controls.h"
#include "jk_visuals.h"
#include "jk_fs.h"
#include "jk_sha256.h"
#include "jk_savefx.h"

#define EXE "speed exe v1.3"
#define D3D9 "our d3d9"
#define INI30 "[jk4speed]\r\nA=1\r\nDXVK_PS4_NFS_PACE=30\r\n"
#define INI60 "[jk4speed]\r\nA=1\r\nDXVK_PS4_NFS_PACE=60\r\n"

typedef struct Env {
    char old_saves[400], root[256], product[300], game[300], data[300], mods[300], pkg[300], logs[300], prof[300], usb[300], saves[400];
    JkServices s;
    /* fake host */
    const uint32_t *script; int script_n, script_i;
    int presents, quit_after, screens_n;
    char screens[64][16];
    uint64_t t;
} Env;

static void sha_of(const char *s, char out[65]) { jk_sha256_hex(s, strlen(s), out); }

static int f_present(void *ctx, const JkFrame *f) { Env *e = ctx; (void)f; e->presents++; return 0; }
static int f_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    Env *e = ctx;
    *held = 0; *pressed = 0;
    if (e->script_i < e->script_n) *pressed = *held = e->script[e->script_i++];
    return 0;
}
static int f_quit(void *ctx) { Env *e = ctx; return e->quit_after >= 0 && e->script_i >= e->quit_after; }
static uint64_t f_now(void *ctx) { Env *e = ctx; return e->t; }
static void f_sleep(void *ctx, uint32_t us) { Env *e = ctx; e->t += (uint64_t)us * 1000u; }
static void f_screen(void *ctx, const char *name)
{
    Env *e = ctx;
    if (e->screens_n < 64) snprintf(e->screens[e->screens_n++], 16, "%s", name);
}
static int f_wall(void *ctx, char *out, size_t cap) { (void)ctx; snprintf(out, cap, "2026-11-17_2105"); return 0; }

static void env_new(Env *e, int with_game, int installed)
{
    memset(e, 0, sizeof *e);
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->game, sizeof e->game, "%s/Jk4Speed/NFSMW", e->root);
    snprintf(e->data, sizeof e->data, "%s/Jk4Speed/System", e->root);
    snprintf(e->product, sizeof e->product, "%s/Jk4Speed", e->root);
    snprintf(e->mods, sizeof e->mods, "%s/Jk4Speed/Mods", e->root);
    snprintf(e->pkg, sizeof e->pkg, "%s/pkg", e->root);
    snprintf(e->logs, sizeof e->logs, "%s/Jk4Speed/Logs", e->root);
    snprintf(e->prof, sizeof e->prof, "%s/prospero-win/profiles", e->root);
    snprintf(e->usb, sizeof e->usb, "%s/usb0", e->root);
    snprintf(e->saves, sizeof e->saves, "%s/Jk4Speed/Saves/NFS Most Wanted", e->root);
    snprintf(e->old_saves, sizeof e->old_saves, "%s/prospero-win/old/NFS Most Wanted", e->root);
    mkdir(e->data, 0755);
    char he[65], hd[65], h30[65], h60[65], m[1024];
    sha_of(EXE, he); sha_of(D3D9, hd); sha_of(INI30, h30); sha_of(INI60, h60);
    snprintf(m, sizeof m, "exe speed.exe %s\nrequire GLOBAL\nfile d3d9.dll %s %zu\nfile jk4speed-dev.ini %s %zu\n"
             "accept jk4speed-dev.ini %s\n", he, hd, strlen(D3D9), h30, strlen(INI30), h60);
    tmp_put(e->pkg, "manifest.txt", m);
    tmp_put(e->pkg, "d3d9.dll", D3D9);
    tmp_put(e->pkg, "jk4speed-dev.ini", INI30);
    {                                                     /* the two base files the launcher composes from */
        char dst[400];
        snprintf(dst, sizeof dst, "%s/jk4speed.ini.base", e->pkg);
        jk_fs_copy_checked(JK_ROOT_DIR "/title/config/jk4speed.ini.base", dst, NULL);
        snprintf(dst, sizeof dst, "%s/nfsmw.profile.base", e->pkg);
        jk_fs_copy_checked(JK_ROOT_DIR "/title/config/nfsmw.profile.base", dst, NULL);
    }
    tmp_put(e->logs, "session-1.log", "x\n");
    if (with_game) {
        tmp_put(e->game, "speed.exe", EXE);
        tmp_put(e->game, "GLOBAL/a", "a");
        if (installed) {
            tmp_put(e->game, "d3d9.dll", D3D9);
            tmp_put(e->game, "jk4speed-dev.ini", INI30);
            /* installed by a PATCH: the install record, stamped with this game folder */
            char rec[1200];
            snprintf(rec, sizeof rec, "# Jk4Speed install record v1: how kind sha path\n# game %s\nkept file %s d3d9.dll\n"
                     "kept file %s jk4speed-dev.ini\n", e->game, hd, h30);
            tmp_put(e->data, "state/installed.txt", rec);
        }
    }
    e->quit_after = -1;
    JkServices s = { 0 };
    s.ctx = e; s.present = f_present; s.pad = f_pad; s.should_quit = f_quit; s.now_ns = f_now; s.sleep_us = f_sleep;
    s.screen = f_screen; s.wall_time = f_wall;
    s.game_dir = e->game; s.data_root = e->data; s.files_dir = e->pkg; s.logs_dir = e->logs; s.mods_dir = e->mods; s.product_root = e->product;
    s.profiles_dir = e->prof; s.saves_dir = e->saves; s.old_saves_dir = e->old_saves; s.usb_dir = e->usb;
    s.build_id = "JK4SPEED 1.0";
    e->s = s;
}

static JkApp app;

static void press(uint32_t b) { jk_app_press(&app, b); }
static void open_settings(void) { press(JK_PAD_DOWN); press(JK_PAD_DOWN); press(JK_PAD_CROSS); }

static void opens_on_play_focus(void)
{
    Env e; env_new(&e, 1, 1);
    CHECK_EQ(jk_app_init(&app, &e.s, NULL), 0);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.focus, JK_HOME_PLAY);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    CHECK_EQ(JK_HOME_COUNT, 3);                        /* PLAY, PATCH, SETTINGS */
    tmp_rm_rf(e.root);
}

static void cross_on_play_when_ready_starts_game(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    tmp_rm_rf(e.root);
}

static void play_unpatched_opens_patch(void)
{
    Env e; env_new(&e, 1, 0);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_NOT_INSTALLED);
    press(JK_PAD_CROSS);
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(jk_patch_primary(&app.m), JK_ACT_INSTALL);
    tmp_rm_rf(e.root);
}

static void install_then_cross_starts(void)
{
    Env e; env_new(&e, 1, 0);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);                 /* Home PLAY -> Patch */
    press(JK_PAD_CROSS);                 /* Install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    CHECK(tmp_exists(e.game, "d3d9.dll"));
    press(JK_PAD_CROSS);                 /* Play */
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    char buf[2048];
    CHECK(tmp_get(e.data, "state/launcher.log", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "install ok") != NULL);
    CHECK(strstr(buf, "start pace=30") != NULL);
    tmp_rm_rf(e.root);
}

static void install_failure_shown(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.pkg, "d3d9.dll", "corrupt");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(strstr(app.m.msg, "d3d9.dll") != NULL);
    CHECK(!app.m.no_pristine);
    press(JK_PAD_SQUARE);                /* Support bundle from a failed install */
    CHECK_EQ(app.m.screen, JK_SCR_LOGS);
    CHECK_EQ(app.m.logs_focus, JK_LOG_BUNDLE);
    press(JK_PAD_CIRCLE);                /* back to where we came from */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(e.root);
}

/* Owner decision D6: 30 default, 60 freely selectable. r4: the choice is kept in
 * <data>/state/launcher.ini and the launcher composes the ini and the profile. */
static void frame_rate_saves_settings_and_composes(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    CHECK_EQ(app.m.set_focus, JK_SET_FRAMERATE);
    CHECK_EQ(app.m.pace, JK_PACE_30);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.pace, JK_PACE_60);
    char buf[8192];
    CHECK(tmp_get(e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "pace=60"));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "DXVK_PS4_NFS_PACE=60\r\n"));
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.pace, JK_PACE_60);
    press(JK_PAD_LEFT);
    CHECK_EQ(app.m.pace, JK_PACE_30);
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "DXVK_PS4_NFS_PACE=30\r\n"));
    press(JK_PAD_CROSS);                               /* ✕ flips it too */
    CHECK_EQ(app.m.pace, JK_PACE_60);
    /* a later open reads it back, and PLAY reports it */
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.pace, JK_PACE_60);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    char log[4096];
    CHECK(tmp_get(e.data, "state/launcher.log", log, sizeof log) > 0 && strstr(log, "start pace=60"));
    tmp_rm_rf(e.root);
}

static void settings_change_rewrites_ini_and_profile(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.data, "state/launcher.ini", "pace=30\nfps_hud=1\nlogs=1\n");
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    press(JK_PAD_RIGHT);
    char ini[8192], prof[8192];
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", ini, sizeof ini) > 0);
    CHECK(strstr(ini, "DXVK_PS4_NFS_PACE=60\r\n") != NULL);
    CHECK(strstr(ini, "DXVK_PS4_FRAME_LOG=1\r\n") != NULL);
    CHECK(strstr(ini, "DXVK_PS4_AUTOTEST=0\r\n") != NULL);
    CHECK(tmp_get(e.prof, "nfsmw.profile", prof, sizeof prof) > 0);
    CHECK(strstr(prof, "env = DXVK_PS4_FPS_HUD=fps\n") != NULL);
    CHECK(strstr(prof, "env = DXVK_PS4_LOG_DIR=") != NULL);
    CHECK(strstr(prof, "DXVK_PS4_DIAG") == NULL);
    /* the other two settings are kept when the pace is saved */
    CHECK(tmp_get(e.data, "state/launcher.ini", ini, sizeof ini) > 0);
    CHECK(strstr(ini, "pace=60") && strstr(ini, "fps_hud=1") && strstr(ini, "logs=1"));
    tmp_rm_rf(e.root);
}

static void play_composes_before_start(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.game, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_AUTOTEST=1\r\nDXVK_PS4_NFS_TIMING=1\r\n");   /* a bench leftover */
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_READY);          /* a composed ini is ours at any hash */
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    char ini[8192], prof[8192];
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", ini, sizeof ini) > 0);
    CHECK(strstr(ini, "DXVK_PS4_AUTOTEST=0\r\n") != NULL);
    CHECK(strstr(ini, "DXVK_PS4_NFS_TIMING=0\r\n") != NULL);
    CHECK(strstr(ini, "DXVK_PS4_NFS_PACE=30\r\n") != NULL);
    CHECK(tmp_get(e.prof, "nfsmw.profile", prof, sizeof prof) > 0);
    CHECK(strstr(prof, "winedebug = -all\n") != NULL);
    CHECK(strstr(prof, "env = DXVK_PS4_FPS_HUD=0\n") != NULL);
    tmp_rm_rf(e.root);
}

static void devtools_flag_sets_autotest_on_play(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.product, "devtools/enable", "1");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    char ini[8192];
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", ini, sizeof ini) > 0);
    CHECK(strstr(ini, "DXVK_PS4_AUTOTEST=1\r\n") != NULL);
    tmp_rm_rf(e.root);
}

/* Production package (owner, 2026-11-24: "ren PKG precis som JkFlow"): no
 * test hook and no user ini; both files are ignored. */
static void prod_ignores_devtools_and_user_ini(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.prod = 1;
    tmp_put(e.product, "devtools/enable", "1");
    tmp_put(e.product, "jk4speed-user.ini", "DXVK_PS4_NFS_SHADOWS_NV=1\r\nDXVK_PS4_TEST_KEY=5\r\n");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    char ini[8192];
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", ini, sizeof ini) > 0);
    CHECK(strstr(ini, "DXVK_PS4_AUTOTEST=0\r\n") != NULL);
    CHECK(strstr(ini, "DXVK_PS4_TEST_KEY") == NULL);
    tmp_rm_rf(e.root);
}

/* CONTROLS: PLAY writes Xidi.ini from the saved layout (Xidi base in the package). */
static void play_composes_xidi_from_controls(void)
{
    Env e; env_new(&e, 1, 1);
    char dst[400], st[400], buf[4096];
    snprintf(dst, sizeof dst, "%s/Xidi.ini", e.pkg);
    jk_fs_copy_checked(JK_ROOT_DIR "/title/config/Xidi.ini", dst, NULL);
    snprintf(st, sizeof st, "%s/state", e.data);
    JkControls c;
    jk_controls_default(&c);
    jk_controls_assign(&c, JK_ACT_GAS, JK_CB_CROSS);
    CHECK_EQ(jk_controls_save(st, &c), 0);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);                                   /* PLAY */
    CHECK(app.done);
    CHECK(tmp_get(e.game, "Xidi.ini", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "ButtonA = Button(1)") != NULL);
    CHECK(strstr(buf, "TriggerRT = Null") != NULL);
    tmp_rm_rf(e.root);
}

/* 1.11 SETTINGS > VISUALS (Xbox 360 look): ↑↓ a slider (wraps), ←→ its value, saved at
 * once (the ini keys composed at once too), △ the defaults, ○ back to the VISUALS row. */
static void visuals_sliders_save_and_reset(void)
{
    Env e; env_new(&e, 1, 1);
    char st[400], buf[8192];
    snprintf(st, sizeof st, "%s/state", e.data);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_VISUALS) press(JK_PAD_DOWN);
    CHECK_EQ(app.m.set_focus, JK_SET_VISUALS);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_VISUALS);
    CHECK_EQ(app.m.vis_focus, JK_VIS_BRIGHTNESS);
    press(JK_PAD_RIGHT);
    JkVisuals v;
    jk_visuals_load(st, &v);
    CHECK(!strcmp(jk_visuals_value(&v, JK_VIS_BRIGHTNESS), "0.22"));
    CHECK(!strcmp(jk_visuals_value(&app.m.vis, JK_VIS_BRIGHTNESS), "0.22"));
    press(JK_PAD_UP);                                      /* wraps to the last slider */
    CHECK_EQ(app.m.vis_focus, JK_VIS_N - 1);
    while (app.m.vis_focus != JK_VIS_REFLECTIONS) press(JK_PAD_UP);
    press(JK_PAD_RIGHT);                                   /* 2 faces */
    jk_visuals_load(st, &v);
    CHECK(!strcmp(jk_visuals_envmap_faces(&v), "2"));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "\r\nenvmap_faces = 2\r\n"));
    press(JK_PAD_TRIANGLE);                                /* the defaults */
    jk_visuals_load(st, &v);
    CHECK(jk_visuals_is_default(&v));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "\r\nenvmap_faces = 1\r\n"));
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    CHECK_EQ(app.m.set_focus, JK_SET_VISUALS);
    tmp_rm_rf(e.root);
}

/* The game folder's X360Stuff.ini carries the player's values only from PLAY; the
 * launcher puts the installed file back when it opens, so the mod check never sees a
 * changed file. Someone else's X360Stuff.ini is never touched. */
static void visuals_x360_at_play_only(void)
{
    Env e; env_new(&e, 1, 1);
    char path[600], sha[65];
    snprintf(path, sizeof path, "%s/scripts/X360Stuff.ini", e.game);
    CHECK_EQ(jk_fs_mkdir_parent(path), 0);
    if (!jk_fs_exists(JK_ROOT_DIR "/../bench/x360/X360Stuff.ini")) {   /* a mod file: not in the public repository */
        printf("  skip: no bench/x360/X360Stuff.ini\n");
        tmp_rm_rf(e.root);
        return;
    }
    CHECK_EQ(jk_fs_copy_checked(JK_ROOT_DIR "/../bench/x360/X360Stuff.ini", path, JK_VIS_X360_SHA), 0);
    tmp_put(e.data, "state/visuals.txt", "MiddleGrayValue 0.14\nMotionBlur 1\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK(jk_fs_sha256(path, sha, NULL) == 0 && !strcmp(sha, JK_VIS_X360_SHA));   /* not before PLAY */
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    char *t = jk_fs_read(path, 64 * 1024, NULL);
    CHECK(t && strstr(t, "\r\nMiddleGrayValue = 0.14\r\n") && strstr(t, "\r\nMotionBlur = 1\r\n"));
    free(t);
    jk_app_init(&app, &e.s, NULL);                         /* back from the game: the installed file again */
    CHECK(jk_fs_sha256(path, sha, NULL) == 0 && !strcmp(sha, JK_VIS_X360_SHA));
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    tmp_put(e.game, "scripts/X360Stuff.ini", "MiddleGrayValue = 0.5\r\n");          /* not ours */
    press(JK_PAD_CROSS);
    char buf[64];
    CHECK(tmp_get(e.game, "scripts/X360Stuff.ini", buf, sizeof buf) > 0 && !strcmp(buf, "MiddleGrayValue = 0.5\r\n"));
    tmp_rm_rf(e.root);
}

/* CONTROLS: select a function, ✕ listens, the first button pressed is taken;
 * a function that had it is left empty; △ gives the defaults back. */
static void controls_capture_and_reset(void)
{
    Env e; env_new(&e, 1, 1);
    char st[400];
    snprintf(st, sizeof st, "%s/state", e.data);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_CONTROLS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_CONTROLS);
    CHECK_EQ(app.m.ctl_focus, JK_ACT_GAS);
    press(JK_PAD_CROSS);                                   /* listen */
    CHECK(app.m.ctl_listen);
    press(JK_PAD_R1);                                      /* captured at once: gas on R1 */
    CHECK(!app.m.ctl_listen);
    CHECK_EQ(app.m.controls.btn[JK_ACT_GAS], JK_CB_R1);
    CHECK_EQ(app.m.controls.btn[JK_ACT_SHIFT_UP], JK_CB_NONE);
    JkControls saved;
    jk_controls_load(st, &saved);
    CHECK_EQ(saved.btn[JK_ACT_GAS], JK_CB_R1);
    press(JK_PAD_DOWN);                                    /* brake */
    press(JK_PAD_CROSS);
    press(JK_PAD_UP);                                      /* D-pad is not a remappable button: still listening */
    CHECK(app.m.ctl_listen);
    press(JK_PAD_TOUCHPAD);                                /* touchpad carries keys only: brake refused, still listening */
    CHECK(app.m.ctl_listen);
    CHECK_EQ(app.m.controls.btn[JK_ACT_CHALLENGE], JK_CB_TOUCHPAD);
    press(JK_PAD_OPTIONS);                                 /* cancel */
    CHECK(!app.m.ctl_listen);
    CHECK_EQ(app.m.controls.btn[JK_ACT_BRAKE], JK_CB_L2);
    press(JK_PAD_TRIANGLE);                                /* defaults */
    CHECK(jk_controls_is_default(&app.m.controls));
    jk_controls_load(st, &saved);
    CHECK(jk_controls_is_default(&saved));
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    press(JK_PAD_CROSS);                                   /* back in: challenge rival on R3 */
    while (app.m.ctl_focus != JK_ACT_CHALLENGE) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    press(JK_PAD_R3);
    CHECK_EQ(app.m.controls.btn[JK_ACT_CHALLENGE], JK_CB_R3);
    CHECK_EQ(app.m.controls.btn[JK_ACT_CAMERA], JK_CB_NONE);
    tmp_rm_rf(e.root);
}

static void frame_rate_disabled_before_install(void)
{
    Env e; env_new(&e, 1, 0);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.pace, JK_PACE_30);
    CHECK(!tmp_exists(e.game, "jk4speed-dev.ini"));
    tmp_rm_rf(e.root);
}

/* PATCH composes the config at its end, with the player's saved settings. */
static void patch_composes_with_players_settings(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.data, "state/launcher.ini", "pace=60\nfps_hud=0\nlogs=0\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.pace, JK_PACE_60);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS); press(JK_PAD_CROSS);   /* PATCH, install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    char buf[8192];
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "DXVK_PS4_NFS_PACE=60\r\n") != NULL);
    CHECK(strstr(buf, "DXVK_PS4_RACE_LOCK=0\r\n") != NULL);
    CHECK(tmp_exists(e.prof, "nfsmw.profile"));
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    tmp_rm_rf(e.root);
}

static void settings_items_open_and_circle_returns(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    static const struct { int item; JkScreenId screen; } items[] = {
        { JK_SET_MODS, JK_SCR_MODS }, { JK_SET_CONTROLS, JK_SCR_CONTROLS }, { JK_SET_SAVES, JK_SCR_SAVES },
        { JK_SET_LOGS, JK_SCR_LOGS }, { JK_SET_ABOUT, JK_SCR_ABOUT },
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        while (app.m.set_focus != items[i].item) press(JK_PAD_DOWN);
        press(JK_PAD_CROSS);
        CHECK_EQ(app.m.screen, items[i].screen);
        press(JK_PAD_CIRCLE);
        CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
        CHECK_EQ(app.m.set_focus, items[i].item);      /* focus kept */
    }
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.focus, JK_HOME_SETTINGS);
    tmp_rm_rf(e.root);
}

static void circle_backs_out_everywhere(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK(!app.done);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.focus, JK_HOME_PATCH);
    press(JK_PAD_UP); press(JK_PAD_UP);            /* wraps to SETTINGS */
    CHECK_EQ(app.m.focus, JK_HOME_SETTINGS);
    tmp_rm_rf(e.root);
}

static void restore_needs_confirm(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.game, "d3d9.dll", "player's own");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);       /* to Patch, install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.dialog, JK_DLG_RESTORE);
    press(JK_PAD_CIRCLE);                          /* cancel */
    CHECK_EQ(app.m.dialog, 0);
    char buf[64];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, D3D9));
    press(JK_PAD_TRIANGLE); press(JK_PAD_CROSS);   /* confirm */
    CHECK_EQ(app.m.dialog, 0);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "player's own"));
    CHECK_EQ(app.m.check.state, JK_ST_NOT_INSTALLED);
    press(JK_PAD_TRIANGLE);                        /* nothing left to restore */
    CHECK_EQ(app.m.dialog, 0);
    tmp_rm_rf(e.root);
}

static void support_saves_bundle(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_LOGS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_LOGS);
    CHECK_EQ(app.m.logs_focus, JK_LOG_ENABLE);
    press(JK_PAD_DOWN); press(JK_PAD_DOWN);
    CHECK_EQ(app.m.logs_focus, JK_LOG_BUNDLE);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.bundle_state, 1);
    CHECK(strstr(app.m.bundle_dir, "support/2026-11-17_2105") != NULL);
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/MANIFEST.txt"));
    CHECK(tmp_exists(e.logs, "support/2026-11-17_2105/session-1.log"));
    char buf[4096];
    CHECK(tmp_get(e.logs, "support/2026-11-17_2105/MANIFEST.txt", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "pace: 30") != NULL);
    CHECK(strstr(buf, "release: unofficial") != NULL);
    tmp_rm_rf(e.root);
}

static void init_copies_r3_saves_once(void)
{
    Env e; env_new(&e, 1, 1);
    put_save(e.old_saves, "RAZOR/RAZOR", "RAZOR", 1);
    CHECK_EQ(jk_app_init(&app, &e.s, NULL), 0);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1));
    CHECK(same_save(e.old_saves, "RAZOR/RAZOR", "RAZOR", 1));
}

static void saves_export_and_import(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.saves, "RAZOR/RAZOR", "console save");
    put_save(e.usb, "Jk4Speed/saves/NFS Most Wanted/JOKER/JOKER", "JOKER", 2);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_SAVES) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_SAVES);
    CHECK_EQ(app.m.saves_profiles, 1);
    CHECK_EQ(app.m.usb, 1);
    press(JK_PAD_CROSS);                               /* export */
    CHECK_EQ(app.m.saves_state, 1);
    CHECK(tmp_exists(e.usb, "Jk4Speed/saves/2026-11-17_2105/NFS Most Wanted/RAZOR/RAZOR"));
    press(JK_PAD_SQUARE);                              /* import asks first */
    CHECK_EQ(app.m.dialog, JK_DLG_IMPORT);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.dialog, 0);
    CHECK(!tmp_exists(e.saves, "JOKER/JOKER"));
    press(JK_PAD_SQUARE); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.saves_state, 2);
    CHECK(tmp_exists(e.saves, "JOKER/JOKER"));
    CHECK(tmp_exists(e.data, "backup/saves/2026-11-17_2105/NFS Most Wanted/RAZOR/RAZOR"));
    CHECK_EQ(app.m.saves_profiles, 2);
    tmp_rm_rf(e.root);
}

/* Player name: Triangle, the pad-driven keyboard, OK; the profile on disk
 * is renamed after a backup (owner finding 2026-11-23: no keyboard in game). */
static void rename_with_the_pad(void)
{
    Env e; env_new(&e, 1, 1);
    put_save(e.saves, "NAME/NAME", "NAME", 1);
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_SAVES) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.dialog, JK_DLG_KEYBOARD);
    CHECK(!strcmp(jk_keyboard_text(&app.m.kb), "NAME"));
    press(JK_PAD_CIRCLE);                                   /* cancel: nothing changes */
    CHECK_EQ(app.m.dialog, 0);
    CHECK(tmp_exists(e.saves, "NAME/NAME"));
    press(JK_PAD_TRIANGLE);
    for (int i = 0; i < 4; i++) press(JK_PAD_SQUARE);
    const char *word = "jok";
    for (const char *p = word; *p; p++) {
        for (int t = 0; t < 60 && strcmp(jk_keyboard_focus_label(&app.m.kb), (char[2]){ *p, 0 }); t++)
            press(t % 10 == 9 ? JK_PAD_DOWN : JK_PAD_RIGHT);
        press(JK_PAD_CROSS);
    }
    CHECK(!strcmp(jk_keyboard_text(&app.m.kb), "jok"));
    for (int t = 0; t < 60 && strcmp(jk_keyboard_focus_label(&app.m.kb), "OK"); t++) press(t % 10 == 9 ? JK_PAD_DOWN : JK_PAD_RIGHT);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.saves_state, 3);
    CHECK(same_save(e.saves, "jok/jok", "jok", 1));
    CHECK(!tmp_exists(e.saves, "NAME"));
    CHECK(strstr(app.m.saves_msg, "backup/saves") != NULL);
    tmp_rm_rf(e.root);
}

static void saves_without_usb_says_so(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.saves, "RAZOR/RAZOR", "console save");
    e.s.usb_dir = NULL;
    jk_app_init(&app, &e.s, NULL);
    open_settings();
    while (app.m.set_focus != JK_SET_SAVES) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.usb, 0);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.saves_state, -1);
    CHECK(strstr(app.m.saves_msg, "USB") != NULL);
    tmp_rm_rf(e.root);
}

/* PATCH > □ opens the mods screen: the four mods of the release catalog in
 * <usb>/Jk4Speed/ (the matrix itself is test_modset); □ there applies. */
static void mods_screen_from_patch(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.usb, "Jk4Speed/NFSMWUHUD01022026b.7z", "7z\xBC\xAF\x27\x1C but not the released archive");
    tmp_put(e.usb, "Jk4Speed/readme.txt", "ignored");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);           /* PATCH */
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    CHECK_EQ(app.m.usb, 1);
    for (int k = 0; k < JK_MOD_N; k++) CHECK(app.m.mods.mod[k].state != JK_MS_READY);
    CHECK_EQ(app.m.mods.unknown, 1);                   /* a damaged 7z: named, left alone */
    CHECK(strstr(app.m.mods.unknown_name, "NFSMWUHUD01022026b.7z") != NULL);
    press(JK_PAD_SQUARE);                              /* nothing to install: the originals stay */
    CHECK_EQ(app.m.mods_state, 1);
    CHECK_EQ(app.m.mods_done, 0);
    CHECK(!tmp_exists(e.game, "scripts/X360Stuff.asi"));
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(e.root);
}

/* r4: archives are read from <mods_dir> and <usb>/Jk4Speed/, never from the
 * product root itself. Each damaged .7z counts as one unknown archive. */
static void frontend_reads_mods_from_mods_dir_and_usb(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.mods, "inmods.7z", "7z\xBC\xAF\x27\x1C damaged");
    tmp_put(e.usb, "Jk4Speed/onusb.7z", "7z\xBC\xAF\x27\x1C damaged");
    char prod[320];
    snprintf(prod, sizeof prod, "%s/Jk4Speed", e.root);
    tmp_put(prod, "inroot.7z", "7z\xBC\xAF\x27\x1C damaged");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);          /* PATCH */
    press(JK_PAD_SQUARE);                             /* MODS */
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    CHECK_EQ(app.m.mods.unknown, 2);
    tmp_rm_rf(e.root);
}

static void launcher_log_in_state_dir(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);                              /* PLAY */
    char log[2048];
    CHECK(tmp_get(e.data, "state/launcher.log", log, sizeof log) > 0 && strstr(log, "start pace="));
    CHECK(!tmp_exists(e.data, "logs/launcher.log"));
    tmp_rm_rf(e.root);
}

static void mods_without_usb(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.usb_dir = NULL;
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS); press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    CHECK_EQ(app.m.usb, 0);
    press(JK_PAD_SQUARE);                              /* /data/Jk4Speed/ is still looked at: nothing there */
    CHECK_EQ(app.m.mods_state, 1);
    CHECK_EQ(app.m.mods.want, 0);
    tmp_rm_rf(e.root);
}

/* SETTINGS > MODS: ✕ (or ← →) turns the focused mod ON/OFF and saves the
 * choice at once; HD HUD / HD Roads cannot be turned ON while the pack is OFF;
 * the defaults: all three ON (owner decision 2026-11-23). */
/* r5: the Xbox 360 pack is required (owner, 2026-11-24): its row cannot be
 * turned off, a saved choice without it is corrected at open, and PATCH
 * refuses before any write when the pack is neither installed nor found. */
static void x360_row_locked(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.require_x360 = 1;
    JkPaths p = { .game_dir = e.s.game_dir, .data_root = e.s.data_root, .files_dir = e.s.files_dir };
    jk_modset_set_choice(&p, JK_MS_BIT(JK_MOD_R));         /* an r4 choice with the pack OFF */
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_R));
    open_settings();
    while (app.m.set_focus != JK_SET_MODS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.mods_focus, JK_MOD_X);
    press(JK_PAD_CROSS);                                   /* the pack: locked ON */
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_R));
    press(JK_PAD_LEFT);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_R));
    press(JK_PAD_DOWN);                                    /* the HUD: optional */
    press(JK_PAD_CROSS);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_DEFAULT_ON);
    press(JK_PAD_CROSS);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_X) | JK_MS_BIT(JK_MOD_R));
    tmp_rm_rf(e.root);
}

static void patch_refuses_without_x360(void)
{
    Env e; env_new(&e, 1, 0);
    e.s.require_x360 = 1;
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);                                   /* PLAY -> Patch */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    press(JK_PAD_CROSS);                                   /* Install */
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(strstr(app.m.msg, "Xbox 360 Stuff Pack") != NULL);
    CHECK(!tmp_exists(e.game, "d3d9.dll"));                /* nothing written */
    CHECK(!tmp_exists(e.data, "state/installed.txt"));
    tmp_rm_rf(e.root);
}

static void settings_mods_toggles(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    JkPaths p = { .game_dir = e.s.game_dir, .data_root = e.s.data_root, .files_dir = e.s.files_dir };
    open_settings();
    while (app.m.set_focus != JK_SET_MODS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    CHECK_EQ(app.m.mods_focus, JK_MOD_X);
    CHECK_EQ(app.m.mods.on, JK_MS_DEFAULT_ON);
    press(JK_PAD_CROSS);                               /* pack OFF */
    CHECK_EQ(app.m.mods_state, 2);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_DEFAULT_ON & ~JK_MS_BIT(JK_MOD_X));
    CHECK_EQ(app.m.mods.on, JK_MS_DEFAULT_ON & ~JK_MS_BIT(JK_MOD_X));
    press(JK_PAD_DOWN);                                /* HUD: still ON */
    CHECK_EQ(app.m.mods_focus, JK_MOD_H);
    press(JK_PAD_CROSS);                               /* OFF */
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_R));
    press(JK_PAD_RIGHT);                               /* and ON again with the pack OFF: any subset works */
    CHECK_EQ(app.m.mods_state, 2);
    CHECK_EQ(jk_modset_choice(&p), JK_MS_BIT(JK_MOD_H) | JK_MS_BIT(JK_MOD_R));
    press(JK_PAD_UP); press(JK_PAD_LEFT);              /* pack ON */
    CHECK_EQ(jk_modset_choice(&p), JK_MS_DEFAULT_ON);
    press(JK_PAD_DOWN);
    press(JK_PAD_UP);
    CHECK_EQ(app.m.mods_focus, JK_MOD_X);
    press(JK_PAD_UP);                                  /* wraps to the roads */
    CHECK_EQ(app.m.mods_focus, JK_MOD_R);
    press(JK_PAD_CROSS);                               /* roads OFF */
    CHECK_EQ(jk_modset_choice(&p), JK_MS_DEFAULT_ON & ~JK_MS_BIT(JK_MOD_R));
    press(JK_PAD_SQUARE);                              /* apply: no archives, nothing to install */
    CHECK_EQ(app.m.mods_state, 1);
    CHECK_EQ(app.m.mods.want, 0);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    CHECK_EQ(app.m.set_focus, JK_SET_MODS);
    tmp_rm_rf(e.root);
}

/* The release stub drives the badge and About; it never blocks PLAY. */
static void release_state_never_blocks_play(void)
{
    for (int st = JK_REL_UNOFFICIAL; st <= JK_REL_CHANGED; st++) {
        Env e; env_new(&e, 1, 1);
        JkRelease r = { 0 };
        r.state = (JkReleaseState)st;
        r.key_id = "4A1F2B3C";
        e.s.release = st == JK_REL_UNOFFICIAL ? NULL : &r;
        jk_app_init(&app, &e.s, NULL);
        CHECK_EQ(app.m.release.state, (JkReleaseState)st);
        press(JK_PAD_CROSS);
        CHECK(app.done);
        CHECK_EQ(app.action, JK_START_GAME);
        char log[4096];
        CHECK(tmp_get(e.data, "state/launcher.log", log, sizeof log) > 0 && strstr(log, "release: "));
        tmp_rm_rf(e.root);
    }
}

static void failed_screen_square_saves_bundle(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.start_failure = "The game stopped before its first picture.";
    e.s.start_failure_log = "session-1.log";
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_FAILED);
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.bundle_state, 1);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK(!app.done);
    tmp_rm_rf(e.root);
}

static void circle_on_failed_does_nothing(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.start_failure = "x";
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CIRCLE);                          /* only the hinted ✕ leaves */
    CHECK_EQ(app.m.screen, JK_SCR_FAILED);
    tmp_rm_rf(e.root);
}

static void play_rechecks_before_starting(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    char p[512];
    snprintf(p, sizeof p, "%s/d3d9.dll", e.game);
    unlink(p);                                     /* changed after the launcher opened */
    press(JK_PAD_CROSS);
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(e.root);
}

static void restore_with_bad_package(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.game, "d3d9.dll", "player's own");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);      /* install */
    tmp_put(e.pkg, "manifest.txt", "broken\n");   /* the package is damaged afterwards */
    press(JK_PAD_CIRCLE); press(JK_PAD_DOWN); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.check.state, JK_ST_BAD_PACKAGE);
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.dialog, JK_DLG_RESTORE);
    press(JK_PAD_CROSS);
    char buf[64];
    CHECK(tmp_get(e.game, "d3d9.dll", buf, sizeof buf) > 0 && !strcmp(buf, "player's own"));
    tmp_rm_rf(e.root);
}

/* presses made while the install runs are dropped, not acted on afterwards */
static void run_drops_presses_made_during_install(void)
{
    Env e; env_new(&e, 1, 0);
    static const uint32_t script[] = { JK_PAD_CROSS, JK_PAD_CROSS, JK_PAD_CROSS, 0 };
    e.script = script; e.script_n = 4; e.quit_after = 4;
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&e.s, &d), 0);
    CHECK_EQ(d.action, JK_LEAVE_HOME);             /* READY TO PLAY was shown, the game not started */
    tmp_rm_rf(e.root);
}

static void installing_ignores_input_and_back_is_blocked(void)
{
    Env e; env_new(&e, 1, 0);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);
    app.m.phase = JK_PH_INSTALLING;                /* as seen by a press during the install */
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(e.root);
}

static void bad_package_square_opens_support(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.pkg, "manifest.txt", "not a manifest\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_BAD_PACKAGE);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);       /* PATCH */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.screen, JK_SCR_LOGS);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(e.root);
}

static void wraps_up_down(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    for (int i = 0; i < JK_HOME_COUNT; i++) press(JK_PAD_DOWN);
    CHECK_EQ(app.m.focus, JK_HOME_PLAY);
    press(JK_PAD_UP);
    CHECK_EQ(app.m.focus, JK_HOME_SETTINGS);
    press(JK_PAD_CROSS);
    for (int i = 0; i < JK_SET_COUNT; i++) press(JK_PAD_DOWN);
    CHECK_EQ(app.m.set_focus, JK_SET_FRAMERATE);
    press(JK_PAD_UP);
    CHECK_EQ(app.m.set_focus, JK_SET_ABOUT);
    tmp_rm_rf(e.root);
}

/* Two buttons in one poll: only the first in priority order acts, never both. */
static void one_action_per_poll(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN | JK_PAD_CROSS);
    CHECK(!(app.done && app.m.focus != JK_HOME_PLAY));
    tmp_rm_rf(e.root);
}

/* The host's free-space callback reaches the patcher. */
static long long tiny(void *ctx, const char *path) { (void)ctx; (void)path; return 5; }
static void free_space_from_host(void)
{
    Env e; env_new(&e, 1, 0);
    e.s.free_bytes = tiny;
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(strstr(app.m.msg, "free space") != NULL);
    jk_frontend_release();
    CHECK(jk_fs_free_fn == NULL);
    tmp_rm_rf(e.root);
}

/* ---- the run loop ---- */

static void run_held_cross_activates_once(void)
{
    Env e; env_new(&e, 1, 0);
    static const uint32_t script[] = { JK_PAD_CROSS, 0, 0, 0, JK_PAD_CIRCLE };
    e.script = script; e.script_n = 5; e.quit_after = 5;
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&e.s, &d), 0);
    CHECK_EQ(d.action, JK_LEAVE_HOME);              /* went to Patch, back, then quit */
    tmp_rm_rf(e.root);
}

static void run_starts_game(void)
{
    Env e; env_new(&e, 1, 1);
    static const uint32_t script[] = { 0, 0, JK_PAD_CROSS };
    e.script = script; e.script_n = 3;
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&e.s, &d), 0);
    CHECK_EQ(d.action, JK_START_GAME);
    CHECK_EQ(d.pace, JK_PACE_30);
    CHECK(e.presents >= 1);
    CHECK(e.screens_n >= 1 && !strcmp(e.screens[0], "home"));
    tmp_rm_rf(e.root);
}

static void run_should_quit_leaves_home(void)
{
    Env e; env_new(&e, 1, 1);
    e.quit_after = 0;
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&e.s, &d), 0);
    CHECK_EQ(d.action, JK_LEAVE_HOME);
    tmp_rm_rf(e.root);
}

static int lost(void *ctx, const JkFrame *f) { (void)ctx; (void)f; return -1; }
static void run_display_lost_fails(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.present = lost;
    JkDecision d;
    CHECK(jk_frontend_run(&e.s, &d) < 0);
    tmp_rm_rf(e.root);
}

static void run_bad_arguments(void)
{
    JkDecision d;
    CHECK(jk_frontend_run(NULL, &d) < 0);
    JkServices s = { 0 };
    CHECK(jk_frontend_run(&s, &d) < 0);            /* no present / clock */
}

static void screen_names_reported(void)
{
    Env e; env_new(&e, 1, 1);
    static const uint32_t script[] = { JK_PAD_DOWN, JK_PAD_DOWN, JK_PAD_CROSS, JK_PAD_DOWN, JK_PAD_DOWN, JK_PAD_DOWN, JK_PAD_DOWN,
                                       JK_PAD_CROSS, JK_PAD_CIRCLE, JK_PAD_CIRCLE };   /* Settings: Frame rate, Show FPS, Mods, Visuals, Controls */
    e.script = script; e.script_n = 10; e.quit_after = 10;
    JkDecision d;
    jk_frontend_run(&e.s, &d);
    CHECK(e.screens_n >= 5);
    CHECK(!strcmp(e.screens[0], "home"));
    CHECK(!strcmp(e.screens[1], "settings"));
    CHECK(!strcmp(e.screens[2], "controls"));
    CHECK(!strcmp(e.screens[3], "settings"));
    CHECK(!strcmp(e.screens[4], "home"));
    tmp_rm_rf(e.root);
}

/* ---- every action is reachable with the pad alone (spec G3) ---- */

/* Breadth-first over the UI's states (screen, focus, phase, dialog, results),
 * each reached by replaying its press sequence on a fresh folder. */
#define BFS_MAX 4096
#define SEQ_MAX 16
typedef struct Node { uint32_t seq[SEQ_MAX]; int n; } Node;

static long state_key(const JkApp *a)
{
    const JkModel *m = &a->m;
    /* a focus that is not on screen does not count (it only changes where ○ lands) */
    int focus = m->screen == JK_SCR_HOME ? m->focus : 0;
    int set_focus = m->screen == JK_SCR_SETTINGS ? m->set_focus : 0;
    int bundle = m->screen == JK_SCR_LOGS || m->screen == JK_SCR_FAILED ? m->bundle_state + 1 : 0;
    int logs = m->screen == JK_SCR_LOGS ? m->logs_focus + 1 : 0;   /* the bundle row is reached by focus */
    int saves = m->screen == JK_SCR_SAVES ? m->saves_state + 1 : 0;
    int phase = m->screen == JK_SCR_PATCH ? (int)m->phase : 0;
    return ((((((((long)m->screen * 8 + focus) * 8 + set_focus) * 8 + phase) * 4 + m->dialog) * 4 + m->pace) * 4
             + bundle) * 4 + saves) * 2 + a->done + 10000000000L * logs
           + 100000000L * (m->check.state * 2 + m->check.has_backup);
}

static void replay(const Node *nd, Env *f)
{
    env_new(f, 1, 0);
    tmp_put(f->game, "d3d9.dll", "player's own");
    tmp_put(f->saves, "RAZOR/RAZOR", "save");
    put_save(f->usb, "Jk4Speed/saves/NFS Most Wanted/JOKER/JOKER", "JOKER", 2);
    jk_app_init(&app, &f->s, NULL);
    for (int i = 0; i < nd->n && !app.done; i++) press(nd->seq[i]);
}

static void every_action_reachable(void)
{
    static const uint32_t buttons[] = { JK_PAD_UP, JK_PAD_DOWN, JK_PAD_LEFT, JK_PAD_RIGHT, JK_PAD_CROSS,
                                        JK_PAD_CIRCLE, JK_PAD_SQUARE, JK_PAD_TRIANGLE };
    static Node queue[BFS_MAX];
    static long seen[BFS_MAX];
    int head = 0, tail = 0, nseen = 0;
    int got_start = 0, got_install = 0, got_restore = 0, got_pace = 0, got_bundle = 0, got_export = 0, got_import = 0;
    int got_hud = 0, got_logs = 0, got_help = 0;
    queue[tail++].n = 0;
    Env f;
    replay(&queue[0], &f);
    seen[nseen++] = state_key(&app);
    tmp_rm_rf(f.root);
    while (head < tail) {
        Node cur = queue[head++];
        if (cur.n >= SEQ_MAX) continue;
        for (size_t b = 0; b < sizeof buttons / sizeof buttons[0]; b++) {
            Node next = cur;
            next.seq[next.n++] = buttons[b];
            replay(&next, &f);
            got_start |= app.done && app.action == JK_START_GAME;
            got_install |= app.m.phase == JK_PH_DONE;
            got_restore |= app.m.phase == JK_PH_RESTORED;
            got_pace |= app.m.pace == JK_PACE_60;
            got_bundle |= app.m.bundle_state == 1;
            got_export |= app.m.saves_state == 1;
            got_import |= app.m.saves_state == 2;
            got_hud |= app.m.fps_hud;
            got_logs |= app.m.logs_on;
            got_help |= app.m.screen == JK_SCR_HELP;
            long k = state_key(&app);
            int known = 0;
            for (int i = 0; i < nseen && !known; i++) known = seen[i] == k;
            if (!known && nseen < BFS_MAX && tail < BFS_MAX && !app.done) { seen[nseen++] = k; queue[tail++] = next; }
            tmp_rm_rf(f.root);
        }
    }
    CHECK(tail < BFS_MAX);                     /* the whole state space was walked */
    CHECK(got_start);
    CHECK(got_install);
    CHECK(got_restore);
    CHECK(got_pace);
    CHECK(got_bundle);
    CHECK(got_export);
    CHECK(got_import);
    CHECK(got_hud);
    CHECK(got_logs);
    CHECK(got_help);
}

/* A package with a prefix seed (Task 8): <files_dir>/seed, its runtime twins
 * in <files_dir>/../win. */
static void put_seed(Env *e)
{
    static const char REG[] = "WINE REGISTRY Version 2\n", DLL[] = "MZ twin";
    char hr[65], hd[65], idx[600], rel[100];
    sha_of(REG, hr);
    sha_of(DLL, hd);
    snprintf(rel, sizeof rel, "seed/objects/%s", hr);
    tmp_put(e->pkg, rel, REG);
    tmp_put(e->root, "win/wine/lib/wine/i386-windows/x.dll", DLL);
    snprintf(idx, sizeof idx, "JKSEED 1\nd drive_c/windows\nf %s %zu system.reg obj\n"
             "f %s %zu drive_c/windows/x.dll app:win/wine/lib/wine/i386-windows/x.dll\n", hr, strlen(REG), hd, strlen(DLL));
    tmp_put(e->pkg, "seed/index.txt", idx);
}

/* PLAY without the seeded prefix opens Patch; PATCH imports it once
 * (System/prefix with its marker); a later open finds it present and leaves
 * what the game changed in it alone. */
/* r5: the Wine prefix is set up when the launcher opens (JkFlow), once, with
 * its own screen; PATCH then only patches. */
static void open_seeds_prefix_before_home(void)
{
    Env e; env_new(&e, 1, 1);
    put_seed(&e);
    char prefix[400], buf[4096];
    snprintf(prefix, sizeof prefix, "%s/prefix", e.data);
    jk_fs_mkdir_p(prefix);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(jk_app_setup(&app), 1);                       /* 1: it set the prefix up */
    CHECK(tmp_exists(prefix, ".jkseed-complete"));
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.check.state, JK_ST_READY);              /* no "Patch needed" for the prefix */
    CHECK(tmp_get(e.data, "state/launcher.log", buf, sizeof buf) > 0 && strstr(buf, "prefix seeded"));
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(jk_app_setup(&app), 0);                       /* second open: nothing to do */
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    tmp_rm_rf(e.root);
}

static void patch_seeds_prefix_once(void)
{
    Env e; env_new(&e, 1, 1);
    put_seed(&e);
    char prefix[400], buf[2048];
    snprintf(prefix, sizeof prefix, "%s/prefix", e.data);
    jk_fs_mkdir_p(prefix);                               /* jk_layout_make leaves it empty */
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_UPDATE);           /* files in place, prefix missing: Patch needed */
    press(JK_PAD_CROSS);                                 /* PLAY */
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(jk_patch_primary(&app.m), JK_ACT_INSTALL);
    press(JK_PAD_CROSS);                                 /* PATCH */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    CHECK(tmp_get(prefix, "system.reg", buf, sizeof buf) > 0 && !strcmp(buf, "WINE REGISTRY Version 2\n"));
    CHECK(tmp_get(prefix, "drive_c/windows/x.dll", buf, sizeof buf) > 0 && !strcmp(buf, "MZ twin"));
    CHECK(tmp_exists(prefix, ".jkseed-complete"));
    CHECK(tmp_get(e.data, "state/launcher.log", buf, sizeof buf) > 0 && strstr(buf, "prefix seeded"));
    /* the game writes its registry; the next open and PLAY leave it */
    tmp_put(prefix, "system.reg", "the game wrote this\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    CHECK(tmp_get(prefix, "system.reg", buf, sizeof buf) > 0 && !strcmp(buf, "the game wrote this\n"));
    tmp_rm_rf(e.root);
}

/* A prefix folder that is not ours: PATCH fails with the reason and never
 * touches it; PLAY still opens Patch. */
static void patch_refuses_a_foreign_prefix(void)
{
    Env e; env_new(&e, 1, 1);
    put_seed(&e);
    char prefix[400], buf[256];
    snprintf(prefix, sizeof prefix, "%s/prefix", e.data);
    tmp_put(prefix, "user.reg", "someone else's\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_UPDATE);
    press(JK_PAD_CROSS);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(strstr(app.m.msg, "prefix") != NULL);
    CHECK(tmp_get(prefix, "user.reg", buf, sizeof buf) > 0 && !strcmp(buf, "someone else's\n"));
    CHECK(!tmp_exists(prefix, "system.reg"));
    tmp_rm_rf(e.root);
}

/* compose backs up a jk4speed-dev.ini that is not ours to <data_root>/backup once. */
static void compose_backs_up_a_foreign_ini(void)
{
    Env e; env_new(&e, 1, 1);
    char buf[256];
    tmp_put(e.game, "jk4speed-dev.ini", "[bench]\r\nmine=1\r\n");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CROSS);                                 /* PLAY composes */
    CHECK(tmp_get(e.data, "backup/jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\nmine=1\r\n"));
    tmp_rm_rf(e.root);
}

/* ---- Task 5: Help, Patch interrupted, Logs & Support, Show FPS, two folders ---- */

static void open_set_item(int item)
{
    open_settings();
    for (int i = 0; i < JK_SET_COUNT && app.m.set_focus != item; i++) press(JK_PAD_DOWN);
}

static void settings_order_and_defaults(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(JK_SET_COUNT, 8);
    CHECK_EQ(JK_SET_FRAMERATE, 0); CHECK_EQ(JK_SET_FPS_HUD, 1); CHECK_EQ(JK_SET_MODS, 2); CHECK_EQ(JK_SET_VISUALS, 3);
    CHECK_EQ(JK_SET_CONTROLS, 4); CHECK_EQ(JK_SET_SAVES, 5); CHECK_EQ(JK_SET_LOGS, 6); CHECK_EQ(JK_SET_ABOUT, 7);
    CHECK_EQ(app.m.fps_hud, 0);                        /* Show FPS / ms: OFF */
    CHECK_EQ(app.m.logs_on, 0);
    open_set_item(JK_SET_LOGS);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_LOGS);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    CHECK_EQ(app.m.set_focus, JK_SET_LOGS);
    tmp_rm_rf(e.root);
}

static void toggle_fps_and_logs_persist(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    open_set_item(JK_SET_FPS_HUD);
    press(JK_PAD_RIGHT);                               /* Show FPS / ms ON */
    CHECK_EQ(app.m.fps_hud, 1);
    char buf[8192];
    CHECK(tmp_get(e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "fps_hud=1"));
    CHECK(tmp_get(e.prof, "nfsmw.profile", buf, sizeof buf) > 0 && strstr(buf, "env = DXVK_PS4_FPS_HUD=fps\n"));
    press(JK_PAD_CROSS);                               /* ✕ flips it back */
    CHECK_EQ(app.m.fps_hud, 0);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.fps_hud, 1);
    while (app.m.set_focus != JK_SET_LOGS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.logs_focus, JK_LOG_ENABLE);
    press(JK_PAD_CROSS);                               /* Enable logs ON */
    CHECK_EQ(app.m.logs_on, 1);
    CHECK(tmp_get(e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "logs=1") && strstr(buf, "fps_hud=1"));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "DXVK_PS4_FRAME_LOG=1\r\n"));
    /* a later open reads both back */
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.fps_hud, 1);
    CHECK_EQ(app.m.logs_on, 1);
    open_set_item(JK_SET_LOGS);
    press(JK_PAD_CROSS);
    press(JK_PAD_LEFT);                                /* ← turns it OFF */
    CHECK_EQ(app.m.logs_on, 0);
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && strstr(buf, "DXVK_PS4_FRAME_LOG=0\r\n"));
    tmp_rm_rf(e.root);
}

static void clear_logs_message(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.logs, "session-2.log", "y\n");
    tmp_put(e.logs, "support/old/MANIFEST.txt", "m\n");
    jk_app_init(&app, &e.s, NULL);
    open_set_item(JK_SET_LOGS);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.log_files, 3);
    press(JK_PAD_DOWN);
    CHECK_EQ(app.m.logs_focus, JK_LOG_CLEAR);
    press(JK_PAD_CROSS);
    CHECK(!tmp_exists(e.logs, "session-1.log") && !tmp_exists(e.logs, "session-2.log"));
    CHECK(!strcmp(app.m.logs_msg, "3 log files cleared."));
    CHECK_EQ(app.m.log_files, 0);
    press(JK_PAD_CROSS);                               /* nothing left */
    CHECK(!strcmp(app.m.logs_msg, "No log files to clear."));
    tmp_put(e.logs, "session-3.log", "z\n");
    press(JK_PAD_UP); press(JK_PAD_DOWN); press(JK_PAD_CROSS);
    CHECK(!strcmp(app.m.logs_msg, "1 log file cleared."));
    tmp_rm_rf(e.root);
}

/* A PATCH cut off (journal left in <data>/modset): the next open shows
 * "Patch was interrupted" once; ✕ starts PATCH; the open after is Home. */
static void interrupted_screen_once(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.data, "modset/journal.txt", "JK4MS 2\nstate swapping\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.mods_recover, 1);
    CHECK_EQ(app.m.screen, JK_SCR_INTERRUPTED);
    press(JK_PAD_CIRCLE);                              /* ○ Home */
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    tmp_put(e.data, "modset/journal.txt", "JK4MS 2\nstate swapping\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_INTERRUPTED);
    press(JK_PAD_SQUARE);                              /* □ Help, ○ back to it */
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_INTERRUPTED);
    press(JK_PAD_CROSS);                               /* ✕ Patch again */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(tmp_exists(e.game, "d3d9.dll"));
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    tmp_rm_rf(e.root);
}

static void help_from_home_square(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN);                                /* focus PATCH */
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_FOLDER);
    press(JK_PAD_RIGHT); CHECK_EQ(app.m.help_page, JK_HELP_MODS);
    press(JK_PAD_RIGHT); CHECK_EQ(app.m.help_page, JK_HELP_SAVES);
    press(JK_PAD_RIGHT); CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_RIGHT); CHECK_EQ(app.m.help_page, JK_HELP_FOLDER);   /* wraps */
    press(JK_PAD_LEFT); CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_CROSS);                               /* nothing else acts */
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.focus, JK_HOME_PATCH);              /* focus kept */
    CHECK(!app.done);
    tmp_rm_rf(e.root);
}

/* A managed game file with no untouched copy anywhere: the PATCH that has to
 * put it back is refused ("MODS CAN'T BE CHANGED") and □ opens Help on
 * Troubleshooting. */
static void no_clean_original_screen(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.game, "GLOBAL/attributes.bin", "a modded file, no untouched copy");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);           /* PATCH */
    press(JK_PAD_CROSS);                               /* install */
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(app.m.no_pristine);
    CHECK(app.m.mods.npristine_needed >= 1);
    CHECK(!strcmp(app.m.mods.pristine_needed[0], "GLOBAL/attributes.bin"));
    char buf[64];
    CHECK(tmp_get(e.game, "GLOBAL/attributes.bin", buf, sizeof buf) > 0 && !strcmp(buf, "a modded file, no untouched copy"));
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);               /* the failure is still shown */
    CHECK(app.m.no_pristine);
    tmp_rm_rf(e.root);
}

/* The title found /data/Jk4Speed/NFSMW and /data/Jk4Speed/nfsmw: both are
 * named, PLAY does not start, PATCH does not install. */
static void two_game_folders_shown(void)
{
    Env e; env_new(&e, 1, 0);
    char other[400];
    snprintf(other, sizeof other, "%s/Jk4Speed/nfsmw", e.root);
    tmp_put(other, "speed.exe", EXE);
    e.s.game_dir_other = other;
    jk_app_init(&app, &e.s, NULL);
    CHECK(app.m.game_other != NULL && !strcmp(app.m.game_other, other));
    press(JK_PAD_CROSS);                               /* PLAY: shows Patch, starts nothing */
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(jk_patch_primary(&app.m), JK_ACT_NONE);
    press(JK_PAD_CROSS);
    CHECK(!tmp_exists(e.game, "d3d9.dll"));            /* nothing installed */
    press(JK_PAD_SQUARE);                              /* □ Help on Troubleshooting */
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    tmp_rm_rf(e.root);
}

/* I-1: with two game folders nothing writes into either: Settings toggles
 * save launcher.ini only, MODS □ Apply now and PLAY are refused. */
static void two_folders_write_nothing(void)
{
    Env e; env_new(&e, 1, 1);
    char other[400];
    snprintf(other, sizeof other, "%s/Jk4Speed/nfsmw", e.root);
    tmp_put(other, "speed.exe", EXE);
    e.s.game_dir_other = other;
    jk_app_init(&app, &e.s, NULL);
    char buf[8192];
    open_set_item(JK_SET_FPS_HUD);
    press(JK_PAD_RIGHT);                               /* saved ... */
    CHECK(tmp_get(e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "fps_hud=1"));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, INI30));   /* ... not composed */
    CHECK(!tmp_exists(e.prof, "nfsmw.profile"));
    while (app.m.set_focus != JK_SET_MODS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    press(JK_PAD_SQUARE);                              /* Apply now */
    CHECK_EQ(app.m.mods_state, -1);
    CHECK(strstr(app.m.mods_msg, "Two game folders") != NULL);
    CHECK(!tmp_exists(e.data, "backup/pristine"));     /* not even the unchanged copies were taken */
    CHECK(!tmp_exists(e.data, "modset/record.txt"));
    press(JK_PAD_CIRCLE); press(JK_PAD_CIRCLE);        /* Home */
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    press(JK_PAD_CROSS);                               /* PLAY */
    CHECK(!app.done);
    char log[8192];
    CHECK(tmp_get(e.data, "state/launcher.log", log, sizeof log) > 0 && strstr(log, "config: not written (two game folders)"));
    tmp_rm_rf(e.root);
}

/* I-2: a mods failure after our files were installed says so (not "your game
 * was not changed"). A cut-off Patch of another game folder that still exists
 * stops the mod step. */
static void mods_failure_after_install_says_installed(void)
{
    Env e; env_new(&e, 1, 0);
    char other[400], j[800];
    snprintf(other, sizeof other, "%s/Jk4Speed/clean-test/NFSMW", e.root);
    tmp_put(other, "speed.exe", EXE);
    snprintf(j, sizeof j, "JK4MS 3\ngame %s\nop patch\nstate swapping\n", other);
    tmp_put(e.data, "modset/journal.txt", j);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);           /* PATCH */
    press(JK_PAD_CROSS);                               /* install */
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_MODS);
    CHECK(!app.m.no_pristine);
    CHECK(tmp_exists(e.game, "d3d9.dll"));             /* our files are in */
    char body[700];
    jk_patch_failure_body(&app.m, body, sizeof body);
    CHECK(strstr(body, "The Jk4Speed files are installed; the mod files were not changed.") != NULL);
    CHECK(strstr(body, "Your game was not changed") == NULL);
    CHECK(strstr(body, "copy") == NULL);
    tmp_rm_rf(e.root);
}

/* MODS △ opens Help on Troubleshooting (the reasons there point to it). */
static void mods_triangle_help(void)
{
    Env e; env_new(&e, 1, 1);
    jk_app_init(&app, &e.s, NULL);
    open_set_item(JK_SET_MODS);
    press(JK_PAD_CROSS);
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    tmp_rm_rf(e.root);
}

/* A start failure and a rolled-back PATCH together: both are shown. */
static void interrupted_then_failed(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.start_failure = "x";
    tmp_put(e.data, "modset/journal.txt", "JK4MS 2\nstate swapping\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_INTERRUPTED);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_FAILED);
    tmp_rm_rf(e.root);
}

/* "The game didn't start": △ opens Help. */
static void failed_screen_triangle_help(void)
{
    Env e; env_new(&e, 1, 1);
    e.s.start_failure = "x";
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.screen, JK_SCR_HELP);
    CHECK_EQ(app.m.help_page, JK_HELP_TROUBLE);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_FAILED);
    tmp_rm_rf(e.root);
}

/* ---- final review fixes ---- */

static void put_journal(Env *e, const char *game, const char *op, const char *state, const char *lines)
{
    char j[2000];
    snprintf(j, sizeof j, "JK4MS 3\ngame %s\nop %s\nstate %s\n%s", game, op, state, lines);
    tmp_put(e->data, "modset/journal.txt", j);
}

/* I-1 / ledger (c): a journal written for another game folder is never
 * rolled back into this one, and with two folders recover does not run. */
static void recover_ignores_other_game_dir(void)
{
    Env e; env_new(&e, 1, 1);
    char other[400], buf[256];
    snprintf(other, sizeof other, "%s/Jk4Speed/clean-test/NFSMW", e.root);
    tmp_put(other, "speed.exe", EXE);
    tmp_put(e.data, "modset/pre/GLOBAL/a", "the other folder's original");
    put_journal(&e, other, "patch", "swapping", "1\tGLOBAL/a\t-\tK\t-\n");
    jk_app_init(&app, &e.s, NULL);
    CHECK(tmp_get(e.game, "GLOBAL/a", buf, sizeof buf) > 0 && !strcmp(buf, "a"));          /* this folder unchanged */
    CHECK(tmp_exists(e.data, "modset/journal.txt"));                                        /* the journal kept */
    CHECK(tmp_get(e.data, "modset/pre/GLOBAL/a", buf, sizeof buf) > 0 && !strcmp(buf, "the other folder's original"));
    CHECK_EQ(app.m.screen, JK_SCR_HOME);                                                    /* nothing destructive shown */
    CHECK(tmp_get(e.data, "state/launcher.log", buf, sizeof buf) > 0);
    press(JK_PAD_CROSS);                                                                    /* this folder still plays */
    CHECK(app.done);
    /* two folders: even this folder's own journal is left for later */
    Env f; env_new(&f, 1, 1);
    char second[400];
    snprintf(second, sizeof second, "%s/Jk4Speed/nfsmw", f.root);
    tmp_put(second, "speed.exe", EXE);
    f.s.game_dir_other = second;
    tmp_put(f.data, "modset/pre/GLOBAL/a", "an original");
    put_journal(&f, f.game, "patch", "swapping", "1\tGLOBAL/a\t-\tK\t-\n");
    jk_app_init(&app, &f.s, NULL);
    CHECK(tmp_get(f.game, "GLOBAL/a", buf, sizeof buf) > 0 && !strcmp(buf, "a"));
    CHECK(tmp_exists(f.data, "modset/journal.txt"));
    tmp_rm_rf(e.root);
    tmp_rm_rf(f.root);
}

/* I-2: on a folder that had mod files before Jk4Speed (no unchanged copy),
 * Restore still removes Jk4Speed's files, leaves those files as they are
 * and names them. */
static void restore_without_pristine_removes_our_files(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.game, "GLOBAL/attributes.bin", "a modded file, no untouched copy");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);           /* PATCH */
    press(JK_PAD_CROSS);                               /* install: our files in, mods refused */
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK(app.m.no_pristine);
    CHECK(tmp_exists(e.game, "d3d9.dll"));
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.dialog, JK_DLG_RESTORE);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    CHECK(!tmp_exists(e.game, "d3d9.dll"));
    char buf[128];
    CHECK(tmp_get(e.game, "GLOBAL/attributes.bin", buf, sizeof buf) > 0 && !strcmp(buf, "a modded file, no untouched copy"));
    CHECK_EQ(app.m.restore_left_n, 1);
    CHECK(!strcmp(app.m.restore_left[0], "GLOBAL/attributes.bin"));
    CHECK(!app.m.check.has_backup);
    char body[700];
    jk_restored_body(&app.m, body, sizeof body);
    CHECK(strstr(body, "GLOBAL/attributes.bin") != NULL);
    CHECK(strstr(body, "back as it was before Jk4Speed") == NULL);
    tmp_rm_rf(e.root);
}

/* I-3: Restore gives back the player's own ini (the first one backed up) and
 * the first file kept aside at each managed path; a later foreign ini is kept
 * aside too, never overwritten; the composed profile goes. */
static void restore_returns_player_ini_and_first_kept(void)
{
    Env e; env_new(&e, 1, 0);
    char buf[256];
    tmp_put(e.game, "jk4speed-dev.ini", "[bench]\r\nmine=1\r\n");
    tmp_put(e.game, "scripts/X360Stuff.asi", "player's asi");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS); press(JK_PAD_CROSS);     /* PATCH */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(!tmp_exists(e.game, "scripts/X360Stuff.asi"));
    CHECK(tmp_get(e.data, "backup/jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\nmine=1\r\n"));
    CHECK(tmp_exists(e.prof, "nfsmw.profile"));
    /* a second foreign ini, then a settings change: kept aside, the first backup stays */
    tmp_put(e.game, "jk4speed-dev.ini", "[bench2]\r\n");
    press(JK_PAD_CIRCLE);
    app.m.focus = JK_HOME_PLAY;
    open_set_item(JK_SET_FPS_HUD);
    press(JK_PAD_RIGHT);
    CHECK(tmp_get(e.data, "backup/jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\nmine=1\r\n"));
    CHECK(tmp_get(e.data, "backup/kept/jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench2]\r\n"));
    /* a second foreign asi: kept as .2 (Settings > Mods, □ Apply now) */
    tmp_put(e.game, "scripts/X360Stuff.asi", "player's asi 2");
    while (app.m.set_focus != JK_SET_MODS) press(JK_PAD_DOWN);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.screen, JK_SCR_MODS);
    press(JK_PAD_SQUARE);
    CHECK_EQ(app.m.mods_state, 1);
    CHECK(!tmp_exists(e.game, "scripts/X360Stuff.asi"));
    press(JK_PAD_CIRCLE); press(JK_PAD_CIRCLE);        /* Settings, Home */
    app.m.focus = JK_HOME_PLAY;
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);           /* Patch */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    press(JK_PAD_TRIANGLE); press(JK_PAD_CROSS);       /* Restore */
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\nmine=1\r\n"));
    CHECK(!tmp_exists(e.data, "backup/jk4speed-dev.ini"));
    CHECK(tmp_get(e.game, "scripts/X360Stuff.asi", buf, sizeof buf) > 0 && !strcmp(buf, "player's asi"));
    CHECK(tmp_get(e.data, "backup/kept/scripts/X360Stuff.asi.2", buf, sizeof buf) > 0 && !strcmp(buf, "player's asi 2"));
    CHECK(!tmp_exists(e.prof, "nfsmw.profile"));
    CHECK(!tmp_exists(e.game, "d3d9.dll"));
    tmp_rm_rf(e.root);
}

/* I-3 (c): an ini or profile changed after Jk4Speed last wrote it is not ours
 * to delete: Restore leaves it (and the player's backup stays). */
static void restore_keeps_changed_composed(void)
{
    Env e; env_new(&e, 1, 0);
    char buf[256];
    tmp_put(e.game, "jk4speed-dev.ini", "[bench]\r\n");
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    tmp_put(e.game, "jk4speed-dev.ini", "[edited by hand]\r\n");
    tmp_put(e.prof, "nfsmw.profile", "[edited by hand]\n");
    press(JK_PAD_TRIANGLE); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[edited by hand]\r\n"));
    CHECK(tmp_get(e.prof, "nfsmw.profile", buf, sizeof buf) > 0 && !strcmp(buf, "[edited by hand]\n"));
    CHECK(tmp_get(e.data, "backup/jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\n"));
    tmp_rm_rf(e.root);
}

/* Minor 2: before the first PATCH, a settings change is saved but nothing is
 * written into the game folder or the profiles folder. */
static void compose_waits_for_install(void)
{
    Env e; env_new(&e, 1, 0);
    tmp_put(e.game, "jk4speed-dev.ini", "[bench]\r\n");
    jk_app_init(&app, &e.s, NULL);
    open_set_item(JK_SET_FPS_HUD);
    press(JK_PAD_RIGHT);
    char buf[256];
    CHECK(tmp_get(e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "fps_hud=1"));
    CHECK(tmp_get(e.game, "jk4speed-dev.ini", buf, sizeof buf) > 0 && !strcmp(buf, "[bench]\r\n"));
    CHECK(!tmp_exists(e.prof, "nfsmw.profile"));
    CHECK(!tmp_exists(e.data, "backup/jk4speed-dev.ini"));
    /* our files already there but no install record: PLAY opens Patch, composes nothing */
    Env f; env_new(&f, 1, 1);
    char rec[400];
    snprintf(rec, sizeof rec, "%s/state/installed.txt", f.data);
    unlink(rec);
    jk_app_init(&app, &f.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK(!tmp_exists(f.prof, "nfsmw.profile"));
    tmp_rm_rf(e.root);
    tmp_rm_rf(f.root);
}

/* Ledger (d): a journal that could not be rolled back at the open opens
 * Patch FAILED (pending), never a silent Home; a Restore's says RESTORE. */
static void failed_recover_opens_patch_failed(void)
{
    Env e; env_new(&e, 1, 1);
    char line[600];
    snprintf(line, sizeof line, "1\tGLOBAL/a\t%s/speed.exe/x\tK\t-\n", e.game);   /* under a file: cannot be undone */
    put_journal(&e, e.game, "patch", "swapping", line);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_PENDING);
    CHECK(app.m.msg[0] != 0);
    CHECK(tmp_exists(e.data, "modset/journal.txt"));
    put_journal(&e, e.game, "restore", "swapping", line);
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_PENDING_RESTORE);
    char body[700];
    jk_patch_failure_body(&app.m, body, sizeof body);
    CHECK(strstr(body, "Restore") != NULL);
    tmp_rm_rf(e.root);
}

/* Ledger (d): while a journal is pending, PLAY never starts the game. */
static void pending_journal_blocks_play(void)
{
    Env e; env_new(&e, 1, 1);
    char line[600];
    snprintf(line, sizeof line, "1\tGLOBAL/a\t%s/speed.exe/x\tK\t-\n", e.game);   /* under a file: cannot be undone */
    put_journal(&e, e.game, "patch", "swapping", line);
    jk_app_init(&app, &e.s, NULL);
    press(JK_PAD_CIRCLE);                              /* Home */
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    press(JK_PAD_CROSS);                               /* PLAY */
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_PENDING);
    tmp_rm_rf(e.root);
}

/* Ledger (a): a damaged journal opens Patch FAILED with copy that does not
 * contradict itself, and ✕ Patch again gets out: the journal is moved aside,
 * the player's file is kept, the Patch finishes. */
static void damaged_journal_has_way_out(void)
{
    Env e; env_new(&e, 1, 1);
    tmp_put(e.data, "modset/journal.txt", "not a journal\n");
    tmp_put(e.data, "modset/pre/scripts/X360Stuff.asi", "the player's original");
    jk_app_init(&app, &e.s, NULL);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_DAMAGED);
    char body[700];
    jk_patch_failure_body(&app.m, body, sizeof body);
    CHECK(strstr(body, "nothing was changed") == NULL);
    CHECK(strstr(body, "Patch") != NULL);
    press(JK_PAD_CIRCLE); press(JK_PAD_CROSS);         /* PLAY is blocked too */
    CHECK(!app.done);
    CHECK_EQ(jk_patch_primary(&app.m), JK_ACT_INSTALL);
    press(JK_PAD_CROSS);                               /* Patch again */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(!tmp_exists(e.data, "modset/journal.txt"));
    CHECK(tmp_exists(e.data, "modset/journal.damaged.txt"));
    char buf[128];
    /* put back (nothing was in its place), then kept aside by this Patch (mods off): never lost */
    CHECK(tmp_get(e.data, "backup/kept/scripts/X360Stuff.asi", buf, sizeof buf) > 0 && !strcmp(buf, "the player's original"));
    press(JK_PAD_CROSS);                               /* and the game starts */
    CHECK(app.done);
    tmp_rm_rf(e.root);
}

/* ---- 1.1: two games (JkGameSlot) ---- */

#define REG_SH "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders] 1700000000\n"
#define REG_USH "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders] 1700000000\n"
#define REG_BE "Z:\\\\data\\\\Jk4Speed\\\\Saves"

typedef struct Two { Env e; char nfs[300], nfs_data[300], nfs_saves[400]; } Two;

/* be: 0 none, 1 a game folder, 2 installed too. nfs: 1 = its folder has a speed.exe. */
static void two_new(Two *t, int be, int nfs, int with_reg)
{
    Env *e = &t->e;
    env_new(e, be != 0, be == 2);
    snprintf(t->nfs, sizeof t->nfs, "%s/Jk4Speed/NFS", e->root);
    snprintf(t->nfs_data, sizeof t->nfs_data, "%s/Jk4Speed/System/nfs", e->root);
    snprintf(t->nfs_saves, sizeof t->nfs_saves, "%s/Jk4Speed/Saves-NFS/NFS Most Wanted", e->root);
    if (nfs) {
        tmp_put(t->nfs, "speed.exe", "whatever exe the player has");
        tmp_put(t->nfs, "GLOBAL/a", "a");
    }
    if (with_reg)
        tmp_put(e->data, "prefix/user.reg", "WINE REGISTRY Version 2\n\n" REG_SH "\"Personal\"=\"" REG_BE "\"\n\n" REG_USH
                "\"Personal\"=str(2):\"" REG_BE "\"\n");
    JkServices *s = &e->s;
    s->games[0] = (JkGameSlot){ "nfsmw", "MOST WANTED - BLACK EDITION", "NFSMW", e->game, NULL, e->data, e->saves,
                                "Z:\\data\\Jk4Speed\\Saves", 0, 1, 0, NULL };
    s->games[1] = (JkGameSlot){ "nfs", "MOST WANTED - ORIGINAL", "NFS", t->nfs, NULL, t->nfs_data, t->nfs_saves,
                                "Z:\\data\\Jk4Speed\\Saves-NFS", 1, 0, 0, NULL };
    s->ngames = 2;
}

static void two_games_none(void)
{
    Two t; two_new(&t, 0, 0, 0);
    CHECK_EQ(jk_app_init(&app, &t.e.s, NULL), 0);
    CHECK_EQ(app.m.ngames, 2);
    CHECK_EQ(app.m.active, 0);
    CHECK_EQ(app.m.slot_state[0], JK_ST_NO_GAME);
    CHECK_EQ(app.m.slot_state[1], JK_ST_NO_GAME);
    press(JK_PAD_CROSS);                               /* PLAY: opens Patch (the folder help), nothing starts */
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    tmp_rm_rf(t.e.root);
}

static void two_games_pick_present(void)
{
    Two t; two_new(&t, 0, 1, 0);
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.active, 1);
    CHECK_EQ(app.m.slot_state[0], JK_ST_NO_GAME);
    CHECK_EQ(app.m.slot_state[1], JK_ST_NOT_INSTALLED);
    CHECK_EQ(app.m.check.state, JK_ST_NOT_INSTALLED);   /* any speed.exe is accepted */
    CHECK(app.m.no_mods);
    tmp_rm_rf(t.e.root);
}

static void switch_right_left(void)
{
    Two t; two_new(&t, 2, 1, 0);
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.active, 0);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.active, 1);
    CHECK_EQ(app.m.check.state, JK_ST_NOT_INSTALLED);
    CHECK(app.m.game_dir == t.e.s.games[1].game_dir);
    press(JK_PAD_RIGHT);                               /* already on the right */
    CHECK_EQ(app.m.active, 1);
    jk_app_init(&app, &t.e.s, NULL);                   /* remembered in System/state/launcher.ini */
    CHECK_EQ(app.m.active, 1);
    press(JK_PAD_LEFT);
    CHECK_EQ(app.m.active, 0);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    char buf[256];
    CHECK(tmp_get(t.e.data, "state/launcher.ini", buf, sizeof buf) > 0 && strstr(buf, "active=0"));
    open_settings();                                   /* left/right keep their meaning off Home */
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.active, 0);
    tmp_rm_rf(t.e.root);
}

static void switch_keeps_records_apart(void)
{
    Two t; two_new(&t, 1, 1, 0);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CROSS);                               /* PLAY -> Patch (not installed) */
    press(JK_PAD_CROSS);                               /* PATCH side 0 */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    CHECK_EQ(app.m.slot_state[0], JK_ST_READY);
    CHECK_EQ(app.m.slot_state[1], JK_ST_NOT_INSTALLED);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.m.check.state, JK_ST_NOT_INSTALLED);
    CHECK(!app.m.check.has_backup);
    CHECK(!tmp_exists(t.nfs, "d3d9.dll"));
    CHECK(tmp_exists(t.e.data, "state/installed.txt"));
    CHECK(!tmp_exists(t.nfs_data, "state/installed.txt"));
    tmp_rm_rf(t.e.root);
}

static void nfs_no_mods(void)
{
    Two t; two_new(&t, 2, 1, 0);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_RIGHT);
    open_settings();
    CHECK_EQ(app.m.screen, JK_SCR_SETTINGS);
    press(JK_PAD_DOWN);
    CHECK_EQ(app.m.set_focus, JK_SET_FPS_HUD);
    press(JK_PAD_DOWN);
    CHECK_EQ(app.m.set_focus, JK_SET_CONTROLS);        /* no MODS row, no VISUALS row */
    press(JK_PAD_UP);
    CHECK_EQ(app.m.set_focus, JK_SET_FPS_HUD);
    press(JK_PAD_CIRCLE);
    press(JK_PAD_UP); press(JK_PAD_CROSS);             /* PATCH (focus was SETTINGS) */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK(!jk_patch_can_mods(&app.m));
    press(JK_PAD_CROSS);                               /* install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(tmp_exists(t.nfs, "d3d9.dll"));
    CHECK(!tmp_exists(t.nfs_data, "modset"));          /* the mods engine never ran */
    CHECK(!tmp_exists(t.nfs_data, "backup/pristine"));
    CHECK(!tmp_exists(t.e.data, "modset"));
    tmp_rm_rf(t.e.root);
}

/* 1.1: the original game installs from its own package folder (jk4speed-files-nfs: the same
 * files plus ThirteenAG's Widescreen Fix, which the Black Edition gets from X360 Stuff); the
 * Black Edition side never gets it. */
static void nfs_installs_its_own_package(void)
{
    Two t; two_new(&t, 1, 1, 0);
    char pkg[400], he[65], hd[65], hw[65], m[1024];
    static const char WSF[] = "classic widescreen fix asi";
    snprintf(pkg, sizeof pkg, "%s/pkg-nfs", t.e.root);
    sha_of(EXE, he); sha_of(D3D9, hd); sha_of(WSF, hw);
    snprintf(m, sizeof m, "exe speed.exe %s\nrequire GLOBAL\nfile d3d9.dll %s %zu\n"
             "file scripts/NFSMostWanted.WidescreenFix.asi %s %zu\n", he, hd, strlen(D3D9), hw, strlen(WSF));
    tmp_put(pkg, "manifest.txt", m);
    tmp_put(pkg, "d3d9.dll", D3D9);
    tmp_put(pkg, "scripts/NFSMostWanted.WidescreenFix.asi", WSF);
    {   /* its own config base: the Black Edition's without the X360 shadow knobs (make_pkg.sh does the same) */
        size_t n = 0;
        char *base = jk_fs_read(JK_ROOT_DIR "/title/config/jk4speed.ini.base", 1 << 20, &n), out[8192], *o = out;
        CHECK(base && strstr(base, "DXVK_PS4_NFS_SHADOW_DETAIL="));
        for (char *l = base; base && *l;) {
            char *e = strchr(l, '\n');
            size_t len = e ? (size_t)(e - l) + 1 : strlen(l);
            if (strncmp(l, "DXVK_PS4_NFS_SHADOW_DETAIL=", 27) && strncmp(l, "DXVK_PS4_NFS_SHADOWS_NV=", 24)) { memcpy(o, l, len); o += len; }
            l += len;
        }
        *o = 0;
        free(base);
        tmp_put(pkg, "jk4speed.ini.base", out);
        char dst[600];
        snprintf(dst, sizeof dst, "%s/nfsmw.profile.base", pkg);
        jk_fs_copy_checked(JK_ROOT_DIR "/title/config/nfsmw.profile.base", dst, NULL);
    }
    t.e.s.games[1].files_dir = pkg;
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.slot_state[1], JK_ST_NOT_INSTALLED);
    press(JK_PAD_RIGHT);
    press(JK_PAD_CROSS);                               /* PLAY -> Patch (not installed) */
    press(JK_PAD_CROSS);                               /* PATCH the original */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    CHECK(tmp_exists(t.nfs, "d3d9.dll"));
    CHECK(tmp_exists(t.nfs, "scripts/NFSMostWanted.WidescreenFix.asi"));
    char ini[8192];
    CHECK(tmp_get(t.nfs, "jk4speed-dev.ini", ini, sizeof ini) > 0);   /* composed from its own base */
    CHECK(strstr(ini, "DXVK_PS4_NFS_PACE=") != NULL);
    CHECK(strstr(ini, "SHADOW_DETAIL") == NULL && strstr(ini, "SHADOWS_NV") == NULL);
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.slot_state[1], JK_ST_READY);        /* Home checks it against its own manifest */
    press(JK_PAD_LEFT);
    press(JK_PAD_CROSS);                               /* PLAY -> Patch */
    press(JK_PAD_CROSS);                               /* PATCH the Black Edition */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(tmp_exists(t.e.game, "jk4speed-dev.ini"));   /* its own package, as in 1.01 */
    CHECK(tmp_get(t.e.game, "jk4speed-dev.ini", ini, sizeof ini) > 0 && strstr(ini, "DXVK_PS4_NFS_SHADOW_DETAIL=2"));
    CHECK(!tmp_exists(t.e.game, "scripts/NFSMostWanted.WidescreenFix.asi"));
    tmp_rm_rf(t.e.root);
}

static void nfs_play_sets_saves(void)
{
    Two t; two_new(&t, 0, 1, 1);
    static const uint32_t script[] = { JK_PAD_CROSS, JK_PAD_CROSS, 0, JK_PAD_CROSS };
    t.e.script = script;
    t.e.script_n = 4;
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&t.e.s, &d), 0);
    CHECK_EQ(d.action, JK_START_GAME);
    CHECK_EQ(d.game, 1);
    char buf[8192];
    CHECK(tmp_get(t.e.data, "prefix/user.reg", buf, sizeof buf) > 0);
    CHECK(strstr(buf, "\"Personal\"=\"Z:\\\\data\\\\Jk4Speed\\\\Saves-NFS\""));
    CHECK(strstr(buf, "\"Personal\"=str(2):\"Z:\\\\data\\\\Jk4Speed\\\\Saves-NFS\""));
    /* the Widescreen Fix's HKCU settings: 1080p from the first start, Install Dir = this game's folder */
    char *k = strstr(buf, "[Software\\\\EA Games\\\\Need for Speed Most Wanted] ");
    CHECK(k && strstr(k, "\"g_RacingResolution\"=dword:87800438\n"));
    char want[700], *w = want;
    w += snprintf(want, sizeof want, "\"Install Dir\"=\"Z:");
    for (const char *c = t.nfs; *c; c++) { if (*c == '/') { *w++ = '\\'; *w++ = '\\'; } else *w++ = *c; }
    strcpy(w, "\"\n");
    CHECK(k && strstr(k, want));
    CHECK(tmp_get(t.nfs_data, "state/launcher.log", buf, sizeof buf) > 0 && strstr(buf, "start pace=30"));
    char prof[600];
    snprintf(prof, sizeof prof, "%s/nfs.profile", t.e.prof);       /* the profile of this game's key */
    CHECK(jk_fs_exists(prof));
    jk_frontend_release();
    tmp_rm_rf(t.e.root);
}

static void be_play_unchanged(void)
{
    Two t; two_new(&t, 2, 1, 1);
    char before[2048], after[2048];
    tmp_get(t.e.data, "prefix/user.reg", before, sizeof before);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    CHECK_EQ(app.active, 0);
    tmp_get(t.e.data, "prefix/user.reg", after, sizeof after);
    CHECK(!strcmp(before, after));                     /* already the BE saves: not written */
    tmp_rm_rf(t.e.root);
}

static void nfs_play_refused_without_user_reg(void)
{
    Two t; two_new(&t, 0, 1, 0);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);          /* Patch, install */
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_CROSS);                               /* PLAY from Patch */
    CHECK(!app.done);
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    CHECK_EQ(app.m.phase, JK_PH_FAILED);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_PREFIX);
    CHECK(strstr(app.m.msg, "user.reg") != NULL);
    tmp_rm_rf(t.e.root);
}

/* I-2: slot 0 is 1.01's: a missing or odd user.reg never blocks PLAY */
static void be_play_without_user_reg_starts(void)
{
    Two t; two_new(&t, 2, 1, 0);                       /* no prefix/user.reg */
    char buf[2048];
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    CHECK(tmp_get(t.e.data, "state/launcher.log", buf, sizeof buf) > 0 && strstr(buf, "saves folder not set"));
    CHECK(!tmp_exists(t.e.data, "prefix/user.reg"));
    tmp_rm_rf(t.e.root);
    /* a user.reg without a Personal line */
    Two u; two_new(&u, 2, 1, 0);
    tmp_put(u.e.data, "prefix/user.reg", "WINE REGISTRY Version 2\n\n" REG_SH "\"Desktop\"=\"x\"\n");
    jk_app_init(&app, &u.e.s, NULL);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    tmp_get(u.e.data, "prefix/user.reg", buf, sizeof buf);
    CHECK(strstr(buf, "\"Desktop\"=\"x\"") && !strstr(buf, "Personal"));   /* never rewritten */
    tmp_rm_rf(u.e.root);
}

/* the seed of this test carries a user.reg */
static void put_seed_reg(Env *e)
{
    static const char REG[] = "WINE REGISTRY Version 2\n\n" REG_SH "\"Personal\"=\"" REG_BE "\"\n\n" REG_USH
                              "\"Personal\"=str(2):\"" REG_BE "\"\n";
    char hr[65], idx[400], rel[100];
    sha_of(REG, hr);
    snprintf(rel, sizeof rel, "seed/objects/%s", hr);
    tmp_put(e->pkg, rel, REG);
    snprintf(idx, sizeof idx, "JKSEED 1\nf %s %zu user.reg obj\n", hr, strlen(REG));
    tmp_put(e->pkg, "seed/index.txt", idx);
}

/* I-2: slot 1 with the marker but no user.reg is an incomplete prefix: PATCH seeds it again */
static void nfs_missing_user_reg_is_reseeded(void)
{
    Two t; two_new(&t, 0, 1, 0);
    put_seed_reg(&t.e);
    char prefix[400], buf[2048];
    snprintf(prefix, sizeof prefix, "%s/prefix", t.e.data);
    jk_fs_mkdir_p(prefix);
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(jk_app_setup(&app), 1);                   /* first seed */
    CHECK(tmp_exists(prefix, "user.reg"));
    char reg[450];
    snprintf(reg, sizeof reg, "%s/user.reg", prefix);
    jk_fs_remove(reg);                                 /* marker stays, user.reg is gone */
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.active, 1);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);          /* PLAY -> Patch, PATCH */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(tmp_exists(prefix, "user.reg"));
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    press(JK_PAD_CROSS);                               /* PLAY */
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    tmp_get(t.e.data, "prefix/user.reg", buf, sizeof buf);
    CHECK(strstr(buf, "Saves-NFS"));
    tmp_rm_rf(t.e.root);
}

/* I-2: a user.reg PLAY cannot use on slot 1 is repaired by the PATCH that the failed PLAY points to */
static void nfs_bad_user_reg_play_then_patch(void)
{
    Two t; two_new(&t, 0, 1, 0);
    put_seed_reg(&t.e);
    char prefix[400];
    snprintf(prefix, sizeof prefix, "%s/prefix", t.e.data);
    jk_fs_mkdir_p(prefix);
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(jk_app_setup(&app), 1);
    tmp_put(t.e.data, "prefix/user.reg", "WINE REGISTRY Version 2\n\n" REG_SH "\"Desktop\"=\"x\"\n");
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);          /* Patch screen, PATCH (nothing to seed yet: bad file unnoticed) */
    press(JK_PAD_CROSS);                               /* PLAY from Patch: refused */
    CHECK(!app.done);
    CHECK_EQ(app.m.fail_kind, JK_FAIL_PREFIX);
    CHECK(strstr(app.m.msg, "Personal") != NULL);
    CHECK_EQ(app.m.check.state, JK_ST_UPDATE);         /* Patch needed: the prefix */
    press(JK_PAD_CROSS);                               /* the offered way out */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    press(JK_PAD_CROSS);
    CHECK(app.done);
    CHECK_EQ(app.action, JK_START_GAME);
    char buf[2048];
    tmp_get(t.e.data, "prefix/user.reg", buf, sizeof buf);
    CHECK(strstr(buf, "Saves-NFS"));
    tmp_rm_rf(t.e.root);
}

/* ---- 1.1: the NFS slot patches like the Black Edition, in its own folders ---- */

#define DINPUT_OURS "our dinput8 (loads Xidi)"
#define DINPUT_THEIRS "the player's own dinput8.dll, an ASI loader that does not load Xidi"

/* The package as the real one: d3d9, dinput8, Xidi.32.dll, Xidi.ini, dxvk.conf and the ini. */
static void two_full_pkg(Two *t)
{
    Env *e = &t->e;
    char he[65], hd[65], hi[65], hx[65], hc[65], hini[65], hini60[65], hxi[65], m[2048], src[400], dst[400];
    sha_of(EXE, he); sha_of(D3D9, hd); sha_of(DINPUT_OURS, hi); sha_of("our xidi32", hx); sha_of("dxvk conf", hc);
    /* the package's ini is what the launcher composes (so a compose over it is not "a foreign ini") */
    static char ini30[8192], ini60[8192];
    {
        char dir[400];
        snprintf(dir, sizeof dir, "%s/inigen", e->root);
        mkdir(dir, 0755);
        JkConfigIn in;
        memset(&in, 0, sizeof in);
        in.files_dir = e->pkg;
        in.game_dir = dir;
        char why[200];
        for (int k = 0; k < 2; k++) {
            in.set.pace = k ? JK_PACE_60 : JK_PACE_30;
            CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
            char path[500];
            snprintf(path, sizeof path, "%s/jk4speed-dev.ini", dir);
            size_t n = 0;
            char *txt = jk_fs_read(path, sizeof ini30, &n);
            memcpy(k ? ini60 : ini30, txt, n + 1);
            free(txt);
        }
    }
    sha_of(ini30, hini); sha_of(ini60, hini60);
    tmp_put(e->pkg, "jk4speed-dev.ini", ini30);
    tmp_put(e->pkg, "dinput8.dll", DINPUT_OURS);
    tmp_put(e->pkg, "Xidi.32.dll", "our xidi32");
    tmp_put(e->pkg, "dxvk.conf", "dxvk conf");
    snprintf(src, sizeof src, JK_ROOT_DIR "/title/config/Xidi.ini");
    snprintf(dst, sizeof dst, "%s/Xidi.ini", e->pkg);
    jk_fs_copy_checked(src, dst, NULL);
    long long xsize = 0;
    jk_fs_sha256(dst, hxi, &xsize);
    snprintf(m, sizeof m,
             "exe speed.exe %s\nrequire GLOBAL\nfile d3d9.dll %s %zu\nfile dinput8.dll %s %zu\nfile Xidi.32.dll %s %zu\n"
             "file Xidi.ini %s %lld\nfile dxvk.conf %s %zu\nfile jk4speed-dev.ini %s %zu\naccept jk4speed-dev.ini %s\n",
             he, hd, strlen(D3D9), hi, strlen(DINPUT_OURS), hx, strlen("our xidi32"), hxi, xsize, hc, strlen("dxvk conf"),
             hini, strlen(ini30), hini60);
    tmp_put(e->pkg, "manifest.txt", m);
}

/* Every file under dir as "<sha256>  <path>", sorted; pruned: find -path patterns to leave out. */
static void snap(const char *dir, const char *prune, char *out, size_t cap)
{
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "cd '%s' 2>/dev/null && find . -type f %s -print0 | sort -z | xargs -0 -r sha256sum", dir, prune);
    FILE *p = popen(cmd, "r");
    size_t n = 0;
    out[0] = 0;
    if (!p) return;
    while (n + 1 < cap && fgets(out + n, (int)(cap - n), p)) n += strlen(out + n);
    pclose(p);
}

#define BE_DATA_PRUNE "-not -path './nfs/*' -not -path './prefix/*' -not -name launcher.log -not -name launcher.ini"

static void nfs_patch_like_be_and_restores(void)
{
    Two t; two_new(&t, 1, 1, 1);
    two_full_pkg(&t);
    tmp_put(t.nfs, "dinput8.dll", DINPUT_THEIRS);               /* the player's own ASI loader */
    tmp_put(t.nfs, "scripts/ExtraOptions.asi", "the player's asi");
    tmp_put(t.nfs, "unlisted.txt", "not in the manifest");
    char before[8192], after[8192], b[1024];
    snap(t.nfs, "", before, sizeof before);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CIRCLE);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* BE: PLAY -> Patch, install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    press(JK_PAD_CIRCLE);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.active, 1);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* NFS: PLAY -> Patch, install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    /* 1: the same package files as the Black Edition, and the composed ini */
    static const char *const files[] = { "d3d9.dll", "dinput8.dll", "Xidi.32.dll", "Xidi.ini", "dxvk.conf", "jk4speed-dev.ini" };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char a[1024], c[1024];
        CHECK(tmp_get(t.nfs, files[i], a, sizeof a) > 0);
        CHECK(tmp_get(t.e.game, files[i], c, sizeof c) > 0);
        CHECK(!strcmp(a, c));                                    /* the same bytes in both folders */
    }
    CHECK(tmp_get(t.nfs, "dinput8.dll", b, sizeof b) > 0 && !strcmp(b, DINPUT_OURS));
    /* 3: nothing outside the manifest is touched, no mods engine ran, no X360 gate */
    CHECK(tmp_get(t.nfs, "scripts/ExtraOptions.asi", b, sizeof b) > 0 && !strcmp(b, "the player's asi"));
    CHECK(tmp_get(t.nfs, "unlisted.txt", b, sizeof b) > 0 && !strcmp(b, "not in the manifest"));
    CHECK(!tmp_exists(t.nfs_data, "modset"));
    CHECK(!tmp_exists(t.nfs_data, "backup/pristine"));
    /* 2: the player's dinput8.dll is backed up under the NFS data_root, not the Black Edition's */
    CHECK(tmp_get(t.nfs_data, "backup/dinput8.dll", b, sizeof b) > 0 && !strcmp(b, DINPUT_THEIRS));
    CHECK(!tmp_exists(t.e.data, "backup/dinput8.dll"));
    /* 4: nfs.profile: the same display, input and key binds as nfsmw.profile; the cache is this game's */
    char np[4096], bp[4096], want[400];
    CHECK(tmp_get(t.e.prof, "nfs.profile", np, sizeof np) > 0);
    CHECK(tmp_get(t.e.prof, "nfsmw.profile", bp, sizeof bp) > 0);
    CHECK(strstr(np, "desktop = 1920x1080") && strstr(np, "l1 = ctrl") && strstr(np, "r1 = shift") && strstr(np, "r3 = c"));
    CHECK(strstr(np, "id = nfs\n") && strstr(bp, "id = nfsmw\n"));
    snprintf(want, sizeof want, "env = DXVK_STATE_CACHE_PATH=%s/cache", t.nfs_data);
    CHECK(strstr(np, want) != NULL);
    snprintf(want, sizeof want, "env = DXVK_STATE_CACHE_PATH=%s/cache", t.e.data);
    CHECK(strstr(bp, want) != NULL && strstr(np, want) == NULL);
    /* 2: Restore puts the NFS folder back byte for byte */
    press(JK_PAD_TRIANGLE);
    CHECK_EQ(app.m.dialog, JK_DLG_RESTORE);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    snap(t.nfs, "", after, sizeof after);
    if (strcmp(before, after)) fprintf(stderr, "--- before\n%s--- after\n%s---\n", before, after);
    CHECK(before[0] && !strcmp(before, after));
    tmp_rm_rf(t.e.root);
}

/* I-4: slot 0's interrupted PATCH is recovered and said whatever side was remembered */
static void be_interrupted_with_nfs_remembered(void)
{
    Two t; two_new(&t, 2, 1, 1);
    char sd[400];
    snprintf(sd, sizeof sd, "%s/state", t.e.data);
    jk_active_save(sd, 1);                              /* the player last had NFS */
    tmp_put(t.e.data, "modset/journal.txt", "JK4MS 2\nstate swapping\n");
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.active, 0);                          /* opens on the affected side */
    CHECK_EQ(app.mods_recover, 1);
    CHECK_EQ(app.m.screen, JK_SCR_INTERRUPTED);
    CHECK(!tmp_exists(t.e.data, "modset/journal.txt")); /* rolled back at the open */
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    press(JK_PAD_RIGHT);                                /* the other side is still reachable */
    CHECK_EQ(app.m.active, 1);
    tmp_rm_rf(t.e.root);
}

/* I-3: a player's own Xidi.ini (first install, no record, bytes differ from the package's) is backed up
 * and comes back byte for byte on Restore */
static void nfs_own_xidi_ini_restored(void)
{
    Two t; two_new(&t, 1, 1, 1);
    two_full_pkg(&t);
    tmp_put(t.nfs, "Xidi.ini", "[Mapper]\nthe player's own xidi setup\n");
    char before[8192], after[8192], b[1024];
    snap(t.nfs, "", before, sizeof before);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_RIGHT);
    CHECK_EQ(app.active, 1);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* NFS: PLAY -> Patch, install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(tmp_get(t.nfs_data, "backup/Xidi.ini", b, sizeof b) > 0 && strstr(b, "xidi setup") != NULL);
    press(JK_PAD_TRIANGLE);
    press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    snap(t.nfs, "", after, sizeof after);
    if (strcmp(before, after)) fprintf(stderr, "--- before\n%s--- after\n%s---\n", before, after);
    CHECK(before[0] && !strcmp(before, after));
    CHECK(tmp_get(t.nfs, "Xidi.ini", b, sizeof b) > 0 && !strcmp(b, "[Mapper]\nthe player's own xidi setup\n"));
    tmp_rm_rf(t.e.root);
}

/* I-3: with an install record already there (1.01's Black Edition install), a differing Xidi.ini is
 * handled as before: kept, no backup */
static void be_existing_record_xidi_unchanged(void)
{
    Two t; two_new(&t, 2, 1, 1);
    two_full_pkg(&t);
    tmp_put(t.e.game, "Xidi.ini", "[Mapper]\nowner's composed xidi\n");
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.active, 0);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* PLAY -> Patch (new files in the package), install */
    CHECK_EQ(app.m.phase, JK_PH_DONE);
    CHECK(!tmp_exists(t.e.data, "backup/Xidi.ini"));
    char rec[4096];
    CHECK(tmp_get(t.e.data, "state/installed.txt", rec, sizeof rec) > 0 && strstr(rec, "kept file") && strstr(rec, "Xidi.ini"));
    CHECK(!strstr(rec, "backup file"));
    tmp_rm_rf(t.e.root);
}

/* Settings are per slot, with the same defaults; changing one slot's never changes the other's. */
static void settings_per_slot_independent(void)
{
    Two t; two_new(&t, 2, 1, 1);
    jk_app_init(&app, &t.e.s, NULL);
    CHECK_EQ(app.m.pace, JK_PACE_30);
    open_settings();
    press(JK_PAD_RIGHT);                                         /* BE: 60 FPS */
    CHECK_EQ(app.m.pace, JK_PACE_60);
    press(JK_PAD_DOWN); press(JK_PAD_RIGHT);                     /* BE: Show FPS on */
    CHECK_EQ(app.m.fps_hud, 1);
    press(JK_PAD_CIRCLE);
    press(JK_PAD_RIGHT);                                         /* NFS: its own defaults */
    CHECK_EQ(app.active, 1);
    CHECK_EQ(app.m.pace, JK_PACE_30);
    CHECK_EQ(app.m.fps_hud, 0);
    char buf[256];
    CHECK(!tmp_exists(t.nfs_data, "state/launcher.ini") || (tmp_get(t.nfs_data, "state/launcher.ini", buf, sizeof buf) > 0 && !strstr(buf, "pace=60")));
    press(JK_PAD_LEFT);
    CHECK_EQ(app.m.pace, JK_PACE_60);                            /* the BE kept its choice */
    CHECK_EQ(app.m.fps_hud, 1);
    tmp_rm_rf(t.e.root);
}

static void slot_records_never_cross(void)
{
    Two t; two_new(&t, 1, 1, 1);
    two_full_pkg(&t);
    jk_app_init(&app, &t.e.s, NULL);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* BE installed */
    press(JK_PAD_CIRCLE);
    press(JK_PAD_RIGHT);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* NFS installed */
    CHECK_EQ(app.m.check.state, JK_ST_READY);
    char be_g[8192], be_d[8192], nfs_g[8192], nfs_d[8192], a[8192];
    snap(t.e.game, "", be_g, sizeof be_g);
    snap(t.e.data, BE_DATA_PRUNE, be_d, sizeof be_d);
    snap(t.nfs, "", nfs_g, sizeof nfs_g);
    snap(t.nfs_data, "-not -name launcher.log", nfs_d, sizeof nfs_d);
    CHECK(be_d[0] && nfs_d[0]);
    /* Restore on NFS leaves the Black Edition's folder, record and backup as they were */
    press(JK_PAD_TRIANGLE); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    snap(t.e.game, "", a, sizeof a);
    CHECK(!strcmp(a, be_g));
    snap(t.e.data, BE_DATA_PRUNE, a, sizeof a);
    CHECK(!strcmp(a, be_d));
    CHECK(tmp_exists(t.e.data, "state/installed.txt") && !tmp_exists(t.nfs_data, "state/installed.txt"));
    /* ... and Restore on the Black Edition leaves NFS as it is (patch NFS again first) */
    press(JK_PAD_CIRCLE);
    CHECK_EQ(app.m.screen, JK_SCR_HOME);
    press(JK_PAD_CROSS); press(JK_PAD_CROSS);                    /* NFS installed again */
    snap(t.nfs, "", nfs_g, sizeof nfs_g);
    snap(t.nfs_data, "-not -name launcher.log", nfs_d, sizeof nfs_d);
    press(JK_PAD_CIRCLE);
    press(JK_PAD_LEFT);
    CHECK_EQ(app.active, 0);
    press(JK_PAD_DOWN); press(JK_PAD_CROSS);                     /* PATCH on the Black Edition */
    CHECK_EQ(app.m.screen, JK_SCR_PATCH);
    press(JK_PAD_TRIANGLE); press(JK_PAD_CROSS);
    CHECK_EQ(app.m.phase, JK_PH_RESTORED);
    snap(t.nfs, "", a, sizeof a);
    CHECK(!strcmp(a, nfs_g));
    snap(t.nfs_data, "-not -name launcher.log", a, sizeof a);
    CHECK(!strcmp(a, nfs_d));
    CHECK(!tmp_exists(t.e.data, "state/installed.txt") && tmp_exists(t.nfs_data, "state/installed.txt"));
    tmp_rm_rf(t.e.root);
}

int main(void)
{
    RUN(nfs_patch_like_be_and_restores);
    RUN(nfs_own_xidi_ini_restored);
    RUN(be_interrupted_with_nfs_remembered);
    RUN(be_existing_record_xidi_unchanged);
    RUN(slot_records_never_cross);
    RUN(settings_per_slot_independent);
    if (getenv("JK_FE_QUICK")) return jk_test_report("test_frontend");   /* only the 1.1 two-game cases (the full walk is slow) */
    RUN(two_games_none);
    RUN(two_games_pick_present);
    RUN(switch_right_left);
    RUN(switch_keeps_records_apart);
    RUN(nfs_no_mods);
    RUN(nfs_installs_its_own_package);
    RUN(nfs_play_sets_saves);
    RUN(be_play_unchanged);
    RUN(nfs_play_refused_without_user_reg);
    RUN(be_play_without_user_reg_starts);
    RUN(nfs_missing_user_reg_is_reseeded);
    RUN(nfs_bad_user_reg_play_then_patch);
    RUN(recover_ignores_other_game_dir);
    RUN(restore_without_pristine_removes_our_files);
    RUN(restore_returns_player_ini_and_first_kept);
    RUN(restore_keeps_changed_composed);
    RUN(compose_waits_for_install);
    RUN(failed_recover_opens_patch_failed);
    RUN(pending_journal_blocks_play);
    RUN(damaged_journal_has_way_out);
    RUN(settings_order_and_defaults);
    RUN(toggle_fps_and_logs_persist);
    RUN(clear_logs_message);
    RUN(interrupted_screen_once);
    RUN(help_from_home_square);
    RUN(no_clean_original_screen);
    RUN(two_game_folders_shown);
    RUN(failed_screen_triangle_help);
    RUN(two_folders_write_nothing);
    RUN(mods_failure_after_install_says_installed);
    RUN(mods_triangle_help);
    RUN(interrupted_then_failed);
    RUN(opens_on_play_focus);
    RUN(cross_on_play_when_ready_starts_game);
    RUN(play_unpatched_opens_patch);
    RUN(install_then_cross_starts);
    RUN(install_failure_shown);
    RUN(frame_rate_saves_settings_and_composes);
    RUN(settings_change_rewrites_ini_and_profile);
    RUN(play_composes_before_start);
    RUN(devtools_flag_sets_autotest_on_play);
    RUN(prod_ignores_devtools_and_user_ini);
    RUN(play_composes_xidi_from_controls);
    RUN(controls_capture_and_reset);
    RUN(visuals_sliders_save_and_reset);
    RUN(visuals_x360_at_play_only);
    RUN(frame_rate_disabled_before_install);
    RUN(patch_composes_with_players_settings);
    RUN(settings_items_open_and_circle_returns);
    RUN(circle_backs_out_everywhere);
    RUN(restore_needs_confirm);
    RUN(support_saves_bundle);
    RUN(saves_export_and_import);
    RUN(init_copies_r3_saves_once);
    RUN(rename_with_the_pad);
    RUN(saves_without_usb_says_so);
    RUN(mods_screen_from_patch);
    RUN(mods_without_usb);
    RUN(frontend_reads_mods_from_mods_dir_and_usb);
    RUN(launcher_log_in_state_dir);
    RUN(settings_mods_toggles);
    RUN(x360_row_locked);
    RUN(patch_refuses_without_x360);
    RUN(release_state_never_blocks_play);
    RUN(failed_screen_square_saves_bundle);
    RUN(installing_ignores_input_and_back_is_blocked);
    RUN(bad_package_square_opens_support);
    RUN(wraps_up_down);
    RUN(circle_on_failed_does_nothing);
    RUN(play_rechecks_before_starting);
    RUN(restore_with_bad_package);
    RUN(run_drops_presses_made_during_install);
    RUN(one_action_per_poll);
    RUN(free_space_from_host);
    RUN(run_held_cross_activates_once);
    RUN(run_starts_game);
    RUN(run_should_quit_leaves_home);
    RUN(run_display_lost_fails);
    RUN(run_bad_arguments);
    RUN(screen_names_reported);
    RUN(every_action_reachable);
    RUN(patch_seeds_prefix_once);
    RUN(open_seeds_prefix_before_home);
    RUN(patch_refuses_a_foreign_prefix);
    RUN(compose_backs_up_a_foreign_ini);
    jk_frontend_release();
    return jk_test_report("test_frontend");
}
