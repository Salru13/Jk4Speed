/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The frontend state machine and run loop. See jk_frontend.h. */
#include "jk_frontend.h"
#include "jk_art.h"
#include "jk_config.h"
#include "jk_fs.h"
#include "jk_modset.h"
#include "jk_saves.h"
#include "jk_seed.h"
#include "jk_strings.h"
#include "jk_support.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *state_name(JkState s)
{
    switch (s) {
    case JK_ST_NO_GAME: return "no-game";
    case JK_ST_WRONG_EXE: return "wrong-exe";
    case JK_ST_MISSING_FILES: return "missing-files";
    case JK_ST_NOT_INSTALLED: return "not-installed";
    case JK_ST_UPDATE: return "update";
    case JK_ST_CHANGED: return "changed";
    case JK_ST_READY: return "ready";
    default: return "bad-package";
    }
}

static const char *release_name(JkReleaseState r)
{
    return r == JK_REL_OFFICIAL ? "official" : r == JK_REL_CHANGED ? "changed" : "unofficial";
}

static void event(JkApp *a, const char *fmt, const char *x, const char *y)
{
    char line[400];
    snprintf(line, sizeof line, fmt, x ? x : "", y ? y : "");
    jk_log_event(a->s->data_root, line);
    if (a->s->log) a->s->log(a->s->ctx, line);
}

static JkPaths paths_of(const JkApp *a)
{
    JkPaths p = { a->s->game_dir, a->s->data_root, a->s->files_dir, a->s->profiles_dir, a->s->override_dir };
    return p;
}

/* r4: the launcher's settings, <data_root>/state/launcher.ini. */
static int state_dir(const JkApp *a, char *out, size_t cap)
{
    return a->s->data_root ? jk_fs_join(out, cap, a->s->data_root, "state") : -1;
}

/* Composes <game>/jk4speed-dev.ini and <profiles>/nfsmw.profile from the
 * settings. Logs a failure and goes on: a missing base file must not keep the
 * player from the game. */
static void compose(JkApp *a)
{
    if (!a->s->game_dir || !jk_fs_is_dir(a->s->game_dir)) return;
    if (jk_two_folders(&a->m)) {                      /* two game folders: nothing is written into either */
        event(a, "config: not written (two game folders)", NULL, NULL);
        return;
    }
    {
        JkPaths p = paths_of(a);
        if (!jk_patch_has_record(&p)) {               /* before the first PATCH: nothing goes into the game folder */
            event(a, "config: not written (not installed yet)", NULL, NULL);
            return;
        }
    }
    char cache[1024], flag[1024], logs[1024], user[1024], why[200] = "";
    JkConfigIn in = { 0 };
    /* r5: the owner's test file /data/Jk4Speed/jk4speed-user.ini (keys win over the base) */
    in.user_ini = !a->s->prod && a->s->product_root && jk_fs_join(user, sizeof user, a->s->product_root, "jk4speed-user.ini") == 0 ? user : NULL;
    in.files_dir = a->s->files_dir;
    in.game_dir = a->s->game_dir;
    in.profiles_dir = a->s->profiles_dir;
    in.set = a->set;
    in.cache_dir = jk_fs_join(cache, sizeof cache, a->s->data_root, "cache") == 0 ? cache : NULL;
    in.logs_dir = a->s->logs_dir;
    if (!in.logs_dir && a->s->product_root && jk_fs_join(logs, sizeof logs, a->s->product_root, "Logs") == 0) in.logs_dir = logs;
    in.devtools = !a->s->prod && a->s->product_root && jk_fs_join(flag, sizeof flag, a->s->product_root, "devtools/enable") == 0
                  && jk_fs_exists(flag);
    char backup[1024];
    in.backup_dir = jk_fs_join(backup, sizeof backup, a->s->data_root, "backup") == 0 ? backup : NULL;   /* a bench ini, once */
    char sd[1024];
    in.state_dir = state_dir(a, sd, sizeof sd) == 0 ? sd : NULL;                  /* composed.txt: what we last wrote */
    if (jk_config_write_ini(&in, why, sizeof why) < 0) event(a, "config: ini not written: %s", why, NULL);
    if (a->s->profiles_dir && jk_config_write_profile(&in, why, sizeof why) < 0)
        event(a, "config: profile not written: %s", why, NULL);
}

/* The package's Wine prefix seed (jk_seed.h): <files_dir>/seed, the app root
 * that holds jk4speed-files/ (its win/ runtime has the seed's twins) and
 * <data_root>/prefix (the title's library root + "/prefix"). 0 ok. */
static int seed_paths(const JkApp *a, char *seed, char *app, char *prefix, size_t cap)
{
    const char *lib = a->s->shared_root ? a->s->shared_root : a->s->data_root;   /* one prefix for every run */
    if (!a->s->files_dir || !lib || jk_fs_join(seed, cap, a->s->files_dir, "seed") < 0 ||
        jk_fs_join(prefix, cap, lib, "prefix") < 0)
        return -1;
    snprintf(app, cap, "%s", a->s->files_dir);
    char *slash = strrchr(app, '/');
    if (!slash || slash == app) return -1;
    *slash = 0;
    return 0;
}

/* -1 the package has no seed; 1 the prefix is complete; 0 PATCH must seed it. */
static int seed_state(const JkApp *a)
{
    char seed[1024], app[1024], prefix[1024];
    return seed_paths(a, seed, app, prefix, sizeof seed) < 0 ? -1 : jk_seed_state(seed, prefix);
}

/* The patcher's check, and "Patch needed" (UPDATE) while our files are in
 * place but the Wine prefix is not: PLAY then opens Patch. */
static void check_now(JkApp *a)
{
    JkPaths p = paths_of(a);
    jk_patch_check(&p, &a->m.check);
    if (a->m.check.state == JK_ST_READY && seed_state(a) == 0) a->m.check.state = JK_ST_UPDATE;
    /* our files there but no install record for this folder: the ini and the
     * profile are composed only by a PATCH, so PLAY opens Patch */
    if (a->m.check.state == JK_ST_READY && !a->m.check.has_backup) a->m.check.state = JK_ST_UPDATE;
    /* a journal of this folder waits (never looked at with two folders) */
    int was = a->pending;
    a->pending = jk_two_folders(&a->m) ? JK_MS_PEND_NONE : jk_modset_pending(&p);
    if (a->pending && !was && !a->pending_why[0])
        snprintf(a->pending_why, sizeof a->pending_why, "%s", a->pending == JK_MS_PEND_DAMAGED ? JK_S_E_JOURNAL : JK_S_E_ROLLBACK);
    if (!a->pending) a->pending_why[0] = 0;
}

/* Patch FAILED for a journal that waits: what it was, and the way out. */
static void show_pending(JkApp *a)
{
    if (!a->pending) return;
    a->m.phase = JK_PH_FAILED;
    a->m.no_pristine = 0;
    a->m.fail_kind = a->pending == JK_MS_PEND_DAMAGED ? JK_FAIL_DAMAGED
                   : a->pending == JK_MS_PEND_RESTORE ? JK_FAIL_PENDING_RESTORE : JK_FAIL_PENDING;
    snprintf(a->m.msg, sizeof a->m.msg, "%s", a->pending_why[0] ? a->pending_why : JK_S_E_ROLLBACK);
}

static void recheck(JkApp *a)
{
    check_now(a);
    a->m.pace = a->set.pace;
}

static void read_saves(JkApp *a)
{
    JkSavesInfo in;
    jk_saves_info(a->s->saves_dir, &in);
    a->m.saves_profiles = in.exists ? in.profiles : -1;
    a->m.usb = a->s->usb_dir && jk_fs_is_dir(a->s->usb_dir);
}

static void on_progress(void *ctx, int done, int total, const char *file);

/* The drop folders: the Mods folder (/data/Jk4Speed/Mods) and <usb>/Jk4Speed/. */
static char drop_usb[1024];
static int drop_dirs(JkApp *a, const char *dirs[2])
{
    int n = 0;
    a->m.usb = a->s->usb_dir && jk_fs_is_dir(a->s->usb_dir);
    if (a->s->mods_dir) dirs[n++] = a->s->mods_dir;
    if (a->m.usb && jk_fs_join(drop_usb, sizeof drop_usb, a->s->usb_dir, "Jk4Speed") == 0) dirs[n++] = drop_usb;
    return n;
}

static void scan_mods(JkApp *a)
{
    const char *dirs[2];
    int n = drop_dirs(a, dirs);
    JkPaths p = paths_of(a);
    jk_modset_scan(jk_modset_catalog(), &p, dirs, n, &a->m.mods);
}

/* PATCH for the mods (none_only: Restore): the untouched files into the
 * pristine store first (once), then the mod set to its target, then the
 * cleanup of the sidecars the store took over. */
static int apply_mods(JkApp *a, int none_only, int *changed, char *why, size_t cap)
{
    const char *dirs[2];
    int n = drop_dirs(a, dirs);
    JkPaths p = paths_of(a);
    const JkMsCatalog *c = jk_modset_catalog();
    char note[300] = "";
    if (changed) *changed = 0;
    int r = jk_modset_pristine_capture(c, &p, on_progress, a, note, sizeof note);
    if (r == -2) event(a, "mods: pristine: %s", note, NULL);          /* refused later only where a PATCH needs it */
    else if (r < 0) {
        snprintf(why, cap, "%s", note);
        event(a, "mods: pristine capture failed: %s", note, NULL);
        return r;
    } else if (note[0]) event(a, "mods: pristine: %s", note, NULL);
    r = jk_modset_apply(c, &p, dirs, n, none_only, on_progress, a, &a->m.mods, changed, why, cap);
    char lv[8];
    snprintf(lv, sizeof lv, "%d", a->m.mods.target);
    event(a, r == 0 ? "mod set %s ok" : "mod set %s failed: %s", lv, r == 0 ? NULL : why);
    if (r == 0) {
        int k = jk_modset_cleanup(&p, note, sizeof note);
        if (k > 0) {
            char nb[16];
            snprintf(nb, sizeof nb, "%d", k);
            event(a, "mods: cleanup removed %s old copies", nb, NULL);
        }
    }
    return r;
}

static void present(JkApp *a)
{
    if (!a->frame) return;
    jk_screen_draw(a->frame, &a->m);
    a->s->present(a->s->ctx, a->frame);
}

static const char *screen_name(const JkModel *m)
{
    if (m->dialog) return "dialog";
    switch (m->screen) {
    case JK_SCR_PATCH: return "patch";
    case JK_SCR_SETTINGS: return "settings";
    case JK_SCR_CONTROLS: return "controls";
    case JK_SCR_SAVES: return "saves";
    case JK_SCR_LOGS: return "logs";
    case JK_SCR_ABOUT: return "about";
    case JK_SCR_FAILED: return "failed";
    case JK_SCR_MODS: return "mods";
    case JK_SCR_CREDITS: return "credits";
    case JK_SCR_HELP: return "help";
    case JK_SCR_INTERRUPTED: return "interrupted";
    case JK_SCR_SETUP: return "setup";
    default: return "home";
    }
}

/* About > Credits: read once from the package (jk4speed-files/about/CREDITS.txt). */
static char *credits_text;
static void load_credits(JkApp *a)
{
    char p[1024];
    if (!credits_text && a->s->files_dir && jk_fs_join(p, sizeof p, a->s->files_dir, "about/CREDITS.txt") == 0)
        credits_text = jk_fs_read(p, 64 * 1024, NULL);
    a->m.credits = credits_text;
}

static void report_screen(JkApp *a)
{
    const char *name = screen_name(&a->m);
    if (strcmp(name, a->last_screen)) {
        snprintf(a->last_screen, sizeof a->last_screen, "%s", name);
        if (a->s->screen) a->s->screen(a->s->ctx, name);
    }
}

/* Files and bytes in the Logs folder: its own files and everything below
 * support/ (what Clear logs deletes). */
static void count_dir(const char *dir, int recurse, int *n, long long *bytes)
{
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1024];
        struct stat st;
        if (jk_fs_join(p, sizeof p, dir, e->d_name) < 0 || lstat(p, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) { if (recurse) count_dir(p, 1, n, bytes); }
        else if (S_ISREG(st.st_mode)) { (*n)++; *bytes += st.st_size; }
    }
    closedir(d);
}

static void count_logs(JkApp *a)
{
    char support[1024];
    a->m.log_files = 0;
    a->m.log_bytes = 0;
    count_dir(a->s->logs_dir, 0, &a->m.log_files, &a->m.log_bytes);
    if (a->s->logs_dir && jk_fs_join(support, sizeof support, a->s->logs_dir, "support") == 0)
        count_dir(support, 1, &a->m.log_files, &a->m.log_bytes);
}

static void go(JkApp *a, JkScreenId s)
{
    a->m.screen = s;
    a->m.dialog = JK_DLG_NONE;
    if (s == JK_SCR_LOGS) {
        a->m.bundle_state = 0; a->m.bundle_dir[0] = 0; a->m.bundle_usb = 0;
        a->m.logs_msg[0] = 0; a->m.logs_msg_bad = 0; a->m.logs_focus = JK_LOG_ENABLE;
        count_logs(a);
    }
    if (s == JK_SCR_SAVES) { a->m.saves_state = 0; a->m.saves_msg[0] = 0; read_saves(a); }
    if (s == JK_SCR_CREDITS) { load_credits(a); a->m.credits_top = 0; }
    if (s == JK_SCR_MODS) { a->m.mods_state = 0; a->m.mods_msg[0] = 0; a->m.mods_done = 0; a->m.mods_focus = 0; a->busy = 1; scan_mods(a); }
    if (s == JK_SCR_HOME || s == JK_SCR_PATCH || s == JK_SCR_SETTINGS) {
        a->m.phase = JK_PH_IDLE; a->m.msg[0] = 0; a->m.no_pristine = 0; recheck(a);
        if (s == JK_SCR_PATCH) show_pending(a);        /* a waiting journal is said on Patch, every time */
    }
}

/* Help at a page; ○ there brings back the screen as it was left. */
static void open_help(JkApp *a, int page)
{
    a->help_back = a->m.screen;
    a->m.screen = JK_SCR_HELP;
    a->m.dialog = JK_DLG_NONE;
    a->m.help_page = page;
}

int jk_app_init(JkApp *a, const JkServices *s, JkFrame *frame)
{
    if (!a || !s) return -1;
    memset(a, 0, sizeof *a);
    a->s = s;
    a->frame = frame;
    a->m.build_id = s->build_id;
    a->m.x360_required = s->require_x360;
    a->m.failure = s->start_failure;
    a->m.failure_log = s->start_failure_log;
    a->m.saves_profiles = -1;
    if (s->release) a->m.release = *s->release;
    else a->m.release.state = JK_REL_UNOFFICIAL;
    jk_fs_free_fn = s->free_bytes;
    jk_fs_free_ctx = s->ctx;
    {
        char sd[1024];
        jk_settings_load(state_dir(a, sd, sizeof sd) == 0 ? sd : NULL, &a->set);
    }
    if (jk_saves_recover(s->saves_dir)) event(a, "saves: an interrupted import was put right", NULL, NULL);
    if (s->old_saves_dir && s->saves_dir && s->data_root) {
        char sd[1024], note[200] = "";
        int r = state_dir(a, sd, sizeof sd) == 0 ? jk_saves_migrate_once(s->old_saves_dir, s->saves_dir, sd, note, sizeof note) : -1;
        event(a, r < 0 ? "saves: r3 copy failed: %s" : "saves: r3 copy: %s", note[0] ? note : "no state folder", NULL);
    }
    a->m.game_dir = s->game_dir;
    a->m.game_other = s->game_dir_other && s->game_dir_other[0] ? s->game_dir_other : NULL;
    if (jk_two_folders(&a->m)) {
        event(a, "mods: recover not run (two game folders)", NULL, NULL);   /* no guessing which folder a journal is for */
    } else {
        JkPaths p = paths_of(a);
        char why[300] = "";
        int r = s->game_dir && s->data_root ? jk_modset_recover(&p, why, sizeof why) : 0;
        a->mods_recover = r;                          /* for the "Patch was interrupted" note (Task 5) */
        if (r == 1) event(a, "mods: an interrupted PATCH was rolled back", NULL, NULL);
        if (r == 2) event(a, "mods: an interrupted PATCH was finished", NULL, NULL);
        if (r == JK_MS_OTHER_FOLDER) event(a, "mods: %s", why, NULL);
        if (r < 0) {
            event(a, "mods: %s", why, NULL);
            snprintf(a->pending_why, sizeof a->pending_why, "%s", why);   /* said on Patch FAILED */
        }
        if (s->require_x360 && s->data_root) {                         /* r5: the pack is required */
            int on = jk_modset_choice(&p);
            if (!(on & JK_MS_BIT(JK_MOD_X)) && jk_modset_set_choice(&p, on | JK_MS_BIT(JK_MOD_X)) == 0)
                event(a, "mods: Xbox 360 Stuff Pack is required: turned ON", NULL, NULL);
        }
    }
    recheck(a);
    a->m.fps_hud = a->set.fps_hud;
    a->m.logs_on = a->set.logs;
    /* a PATCH cut off and rolled back: said once (the journal is gone now);
     * one that could not be rolled back opens Patch FAILED, never Home */
    a->m.screen = a->mods_recover == 1 ? JK_SCR_INTERRUPTED : s->start_failure ? JK_SCR_FAILED : JK_SCR_HOME;
    if (a->pending && a->m.screen == JK_SCR_HOME) { a->m.screen = JK_SCR_PATCH; show_pending(a); }
    event(a, "open state=%s pace=%s", state_name(a->m.check.state), jk_pace_key(a->m.pace));
    if (a->m.game_other) event(a, "two game folders: %s and %s", s->game_dir, a->m.game_other);
    event(a, "release: %s key=%s", release_name(a->m.release.state), a->m.release.key_id ? a->m.release.key_id : "-");
    if (s->start_failure) event(a, "previous start failed: %s", s->start_failure, NULL);
    return 0;
}

/* ---- actions ---- */

static void on_progress(void *ctx, int done, int total, const char *file)
{
    JkApp *a = ctx;
    a->m.prog_done = done;
    a->m.prog_total = total;
    snprintf(a->m.prog_file, sizeof a->m.prog_file, "%s", file ? file : "");
    present(a);
}

/* Free space as the title sees it (FTP cannot report it: console pre-check
 * 2026-11-23), logged at every PATCH. */
static void log_free(JkApp *a)
{
    char d[48], g[48];
    long long fd = jk_fs_free_bytes(a->s->data_root), fg = jk_fs_free_bytes(a->s->game_dir);
    if (fd >= 0) snprintf(d, sizeof d, "%lld MB", fd >> 20); else snprintf(d, sizeof d, "unknown");
    if (fg >= 0) snprintf(g, sizeof g, "%lld MB", fg >> 20); else snprintf(g, sizeof g, "unknown");
    event(a, "free space: data %s, game folder %s", d, g);
}

/* PATCH: the package's Wine prefix seed into <data_root>/prefix when its
 * marker is not there yet (jk_seed_import, with progress). 0 ok or nothing to
 * do; <0 with the reason in why. */
static int seed_prefix(JkApp *a, char *why, size_t cap)
{
    char seed[1024], app[1024], prefix[1024];
    if (seed_paths(a, seed, app, prefix, sizeof seed) < 0 || jk_seed_state(seed, prefix) != 0) return 0;
    const char *roots[] = { app };
    char note[200] = "";
    a->m.prog_done = 0;
    a->m.prog_total = 0;
    int r = jk_seed_import(seed, roots, 1, prefix, on_progress, a, note, sizeof note);
    if (r >= 0) {
        event(a, r == 0 ? "prefix seeded" : "prefix already complete", NULL, NULL);
        if (note[0]) event(a, "prefix: %s", note, NULL);          /* an older seed moved aside */
        return 0;
    }
    event(a, "prefix: %s", note, NULL);
    snprintf(why, cap, "%s", note);
    return -1;
}

/* Two game folders: every write into a game folder is refused (no guessing). */
static int refuse_two(JkApp *a, const char *what)
{
    if (!jk_two_folders(&a->m)) return 0;
    event(a, "%s refused: two game folders", what, NULL);
    return 1;
}

int jk_app_setup(JkApp *a)
{
    if (!a || !a->s || seed_state(a) != 0) return 0;
    JkScreenId back = a->m.screen;
    a->m.screen = JK_SCR_SETUP;
    a->busy = 1;
    present(a);
    char why[300] = "";
    int r = seed_prefix(a, why, sizeof why);
    recheck(a);
    if (r < 0) {
        a->m.screen = JK_SCR_PATCH;
        a->m.phase = JK_PH_FAILED;
        a->m.fail_kind = JK_FAIL_SEED;
        snprintf(a->m.msg, sizeof a->m.msg, "%s", why);
        event(a, "setup failed: %s", why, NULL);
        return -1;
    }
    a->m.screen = back;
    return 1;
}

static void do_install(JkApp *a)
{
    if (refuse_two(a, "patch")) return;
    JkPaths p = paths_of(a);
    if (a->s->require_x360) {                            /* before any write: the pack, installed or found */
        scan_mods(a);
        if (!(a->m.mods.target & JK_MS_BIT(JK_MOD_X))) {
            a->busy = 1;
            a->m.phase = JK_PH_FAILED;
            a->m.fail_kind = JK_FAIL_MODS;
            snprintf(a->m.msg, sizeof a->m.msg, "%s", JK_S_E_NEED_X360);
            event(a, "install refused: Xbox 360 Stuff Pack not found", NULL, NULL);
            return;
        }
    }
    a->busy = 1;
    log_free(a);
    a->m.phase = JK_PH_INSTALLING;
    a->m.no_pristine = 0;
    a->m.fail_kind = JK_FAIL_CORE;
    a->m.prog_done = 0;
    a->m.prog_total = 0;
    a->m.prog_file[0] = 0;
    present(a);
    char why[300];
    int r = jk_patch_install(&p, on_progress, a, why, sizeof why);
    int changed = 0;
    if (r == 0) {
        a->m.fail_kind = JK_FAIL_SEED;
        r = seed_prefix(a, why, sizeof why);                          /* the Wine prefix, once (System/prefix) */
    }
    if (r == 0) {
        a->m.fail_kind = JK_FAIL_MODS;
        a->m.mods.npristine_needed = 0;                                /* only this PATCH's own scan counts */
        r = apply_mods(a, 0, &changed, why, sizeof why);              /* every PATCH: the mods the folders hold */
        if (r == JK_MS_PENDING) {
            a->m.fail_kind = JK_FAIL_PENDING;
            snprintf(a->pending_why, sizeof a->pending_why, "%s", why);   /* PLAY stays blocked, Patch says it */
        }
        a->m.no_pristine = r < 0 && a->m.mods.npristine_needed > 0;   /* "MODS CAN'T BE CHANGED" */
    }
    if (r == 0) compose(a);                           /* the ini and the profile, from the player's settings */
    recheck(a);
    if (r == 0) {
        a->m.phase = JK_PH_DONE;
        event(a, "install ok pace=%s", jk_pace_key(a->m.pace), NULL);
    } else {
        a->m.phase = JK_PH_FAILED;
        snprintf(a->m.msg, sizeof a->m.msg, "%s", why);
        event(a, "install failed: %s", why, NULL);
    }
}

static void do_restore(JkApp *a)
{
    if (refuse_two(a, "restore")) { a->m.dialog = JK_DLG_NONE; return; }
    JkPaths p = paths_of(a);
    a->busy = 1;
    char why[300];
    int changed = 0;
    a->m.mods.npristine_needed = 0;
    a->m.mods.nleft = 0;
    /* the mods out first (a file with no unchanged copy stays as it is and is
     * named), then Jk4Speed's own files: Restore never dead-ends */
    int r = apply_mods(a, 1, &changed, why, sizeof why);
    a->m.restore_left_n = r == 0 ? a->m.mods.nleft : 0;
    memcpy(a->m.restore_left, a->m.mods.left, sizeof a->m.restore_left);
    if (r == JK_MS_PENDING) snprintf(a->pending_why, sizeof a->pending_why, "%s", why);
    if (r == 0) r = jk_patch_restore(&p, why, sizeof why);
    a->m.dialog = JK_DLG_NONE;
    recheck(a);
    if (r == 0) {
        a->m.phase = JK_PH_RESTORED;
        if (a->m.restore_left_n) {
            char n[16];
            snprintf(n, sizeof n, "%d", a->m.restore_left_n);
            event(a, "restore ok; %s files left as they are (no unchanged copy), first %s", n, a->m.restore_left[0]);
        } else event(a, "restore ok", NULL, NULL);
    } else {
        a->m.phase = JK_PH_FAILED;
        a->m.no_pristine = 0;
        a->m.fail_kind = r == JK_MS_PENDING ? JK_FAIL_PENDING_RESTORE : JK_FAIL_RESTORE;
        snprintf(a->m.msg, sizeof a->m.msg, "%s", why);
        event(a, "restore failed: %s", why, NULL);
    }
}

static void stamp_of(JkApp *a, char *stamp, size_t cap)
{
    stamp[0] = 0;
    if (a->s->wall_time && a->s->wall_time(a->s->ctx, stamp, cap) != 0) stamp[0] = 0;
}

static void do_bundle(JkApp *a)
{
    a->busy = 1;
    char stamp[64];
    stamp_of(a, stamp, sizeof stamp);
    JkSupportIn in = { 0 };
    in.data_root = a->s->data_root;
    in.game_dir = a->s->game_dir;
    in.files_dir = a->s->files_dir;
    in.logs_dir = a->s->logs_dir;
    in.runtime_root = a->s->runtime_root;
    in.profiles_dir = a->s->profiles_dir;
    in.usb_dir = a->s->usb_dir;
    in.build_id = a->s->build_id;
    in.local_user = a->s->local_user;
    in.stamp = stamp[0] ? stamp : NULL;
    in.log_files = a->s->log_files;
    in.state = state_name(a->m.check.state);
    in.pace = jk_pace_key(a->m.pace);
    in.release = release_name(a->m.release.state);
    int usb = 0;
    if (jk_support_bundle(&in, a->m.bundle_dir, sizeof a->m.bundle_dir, &usb) == 0) {
        a->m.bundle_state = 1;
        a->m.bundle_usb = usb;
        event(a, "bundle saved %s%s", a->m.bundle_dir, usb ? " (and USB)" : "");
    } else {
        a->m.bundle_state = -1;
        event(a, "bundle failed", NULL, NULL);
    }
}

static void do_export(JkApp *a)
{
    a->busy = 1;
    char stamp[64], why[200];
    stamp_of(a, stamp, sizeof stamp);
    read_saves(a);
    if (jk_saves_export(a->s->saves_dir, a->m.usb ? a->s->usb_dir : NULL, stamp[0] ? stamp : NULL,
                        a->m.saves_msg, sizeof a->m.saves_msg, why, sizeof why) == 0) {
        a->m.saves_state = 1;
        event(a, "saves exported %s", a->m.saves_msg, NULL);
    } else {
        a->m.saves_state = -1;
        snprintf(a->m.saves_msg, sizeof a->m.saves_msg, "%s", why);
        event(a, "saves export failed: %s", why, NULL);
    }
}

/* Square on SAVE GAMES: look on the USB drive and show what is there. */
static void find_saves(JkApp *a)
{
    a->busy = 1;
    JkModel *m = &a->m;
    char why[200];
    read_saves(a);
    if (!m->usb) { m->saves_state = -1; snprintf(m->saves_msg, sizeof m->saves_msg, "No USB drive found. Plug one in and try again."); return; }
    if (jk_saves_find(a->s->saves_dir, a->s->usb_dir, a->s->data_root, &m->saves_found, why, sizeof why) < 0) {
        m->saves_state = -1;
        snprintf(m->saves_msg, sizeof m->saves_msg, "%s", why);
        return;
    }
    char n[16];
    snprintf(n, sizeof n, "%d", m->saves_found.n);
    event(a, "saves found on USB: %s", n, m->saves_found.rejected ? " (some rejected)" : "");
    if (!m->saves_found.n) {
        m->saves_state = -1;
        if (m->saves_found.rejected)
            snprintf(m->saves_msg, sizeof m->saves_msg, "%s %s is not a Most Wanted save.", JK_S_G_NOTHING, m->saves_found.rejected_name);
        else
            snprintf(m->saves_msg, sizeof m->saves_msg, "%s", JK_S_G_NOTHING);
        return;
    }
    m->dialog = JK_DLG_IMPORT;
}

static void do_import(JkApp *a, int keep_both)
{
    a->busy = 1;
    a->m.dialog = JK_DLG_NONE;
    char stamp[64], why[200];
    stamp_of(a, stamp, sizeof stamp);
    read_saves(a);
    if (jk_saves_import_found(a->s->saves_dir, &a->m.saves_found, keep_both, a->s->data_root, stamp[0] ? stamp : NULL,
                              a->m.saves_msg, sizeof a->m.saves_msg, why, sizeof why) == 0) {
        a->m.saves_state = 2;
        event(a, keep_both ? "saves imported, kept both (backup %s)" : "saves imported (backup %s)",
              a->m.saves_msg[0] ? a->m.saves_msg : "none", NULL);
    } else {
        a->m.saves_state = -1;
        snprintf(a->m.saves_msg, sizeof a->m.saves_msg, "%s", why);
        event(a, "saves import failed: %s", why, NULL);
    }
    read_saves(a);
}

static void do_mods(JkApp *a)
{
    if (refuse_two(a, "mods")) {
        a->m.mods_state = -1;
        snprintf(a->m.mods_msg, sizeof a->m.mods_msg, "%s", JK_S_TWO_REFUSED);
        return;
    }
    a->busy = 1;
    char why[300];
    log_free(a);
    a->m.phase = JK_PH_INSTALLING;
    a->m.prog_done = 0;
    a->m.prog_total = 0;
    a->m.prog_file[0] = 0;
    present(a);
    int done = 0;
    a->m.mods.npristine_needed = 0;                    /* only this PATCH's own scan counts */
    int r = apply_mods(a, 0, &done, why, sizeof why);
    int needed = a->m.mods.npristine_needed;
    if (r == JK_MS_PENDING) snprintf(a->pending_why, sizeof a->pending_why, "%s", why);
    a->m.phase = JK_PH_IDLE;
    scan_mods(a);
    if (r == 0) {
        a->m.mods_state = 1;
        a->m.mods_done = done;
        char n[16];
        snprintf(n, sizeof n, "%d", done);
        event(a, "mods installed %s", n, NULL);
    } else {
        a->m.mods_state = -1;
        /* JK_MS_PENDING says itself what happens next; anything else changed nothing */
        snprintf(a->m.mods_msg, sizeof a->m.mods_msg, "%s%s%s", why, r == JK_MS_PENDING ? "" : ". ",
                 r == JK_MS_PENDING ? "" : JK_S_M_FAILED_SAME);
        event(a, "mods failed: %s", why, NULL);
        if (needed > 0 && r != JK_MS_PENDING) {       /* "MODS CAN'T BE CHANGED": the Patch failure, with Help */
            a->m.screen = JK_SCR_PATCH;
            recheck(a);
            a->m.phase = JK_PH_FAILED;
            a->m.fail_kind = JK_FAIL_MODS;
            a->m.no_pristine = 1;
            snprintf(a->m.msg, sizeof a->m.msg, "%s", why);
        }
    }
}

/* ✕ / ← → on a Mods row: ON <-> OFF, saved at once; PATCH applies it. Any
 * subset of the three mods is a choice. */
static void toggle_mod(JkApp *a)
{
    JkPaths p = paths_of(a);
    int k = a->m.mods_focus, on = jk_modset_choice(&p), bit = JK_MS_BIT(k);
    if (a->s->require_x360 && k == JK_MOD_X) return;     /* required: stays ON */
    int next = on ^ bit;
    a->m.mods_done = 0;
    if (jk_modset_set_choice(&p, next) < 0) {
        a->m.mods_state = -1;
        snprintf(a->m.mods_msg, sizeof a->m.mods_msg, "%s", JK_S_M_SAVE_FAILED);
        return;
    }
    char v[8];
    snprintf(v, sizeof v, "%d", next);
    event(a, "mods choice %s (%s)", v, jk_modset_name(k));
    a->m.mods_state = 2;
    a->m.mods_msg[0] = 0;
    scan_mods(a);
}

static void press_mods(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (m->phase == JK_PH_INSTALLING) return;
    if (b == JK_PAD_CIRCLE) { go(a, a->mods_back); return; }
    if (b == JK_PAD_UP) { m->mods_focus = (m->mods_focus + JK_MOD_N - 1) % JK_MOD_N; return; }
    if (b == JK_PAD_DOWN) { m->mods_focus = (m->mods_focus + 1) % JK_MOD_N; return; }
    if (b == JK_PAD_CROSS || b == JK_PAD_LEFT || b == JK_PAD_RIGHT) { toggle_mod(a); return; }
    if (b == JK_PAD_TRIANGLE) { open_help(a, JK_HELP_TROUBLE); return; }   /* the reasons point to Troubleshooting */
    if (b == JK_PAD_SQUARE) do_mods(a);
}

/* Saves the settings (<data_root>/state/launcher.ini) and composes the ini
 * and the profile again. 0 ok; the model is left as it was on a failure. */
static int save_settings(JkApp *a, const JkSettings *next, const char *what)
{
    char sd[1024];
    if (state_dir(a, sd, sizeof sd) == 0 && jk_settings_save(sd, next) == 0) {
        a->set = *next;
        a->m.pace = next->pace;
        a->m.fps_hud = next->fps_hud;
        a->m.logs_on = next->logs;
        event(a, "settings: %s", what, NULL);
        compose(a);                                   /* every settings change rewrites the ini and the profile */
        return 0;
    }
    event(a, "settings: %s not saved", what, NULL);
    return -1;
}

static void set_pace(JkApp *a, JkPace pace)
{
    if (pace == a->m.pace) return;
    JkSettings next = a->set;
    next.pace = pace;
    char what[32];
    snprintf(what, sizeof what, "pace %s", jk_pace_key(pace));
    save_settings(a, &next, what);
    check_now(a);
}

static void set_fps_hud(JkApp *a, int on)
{
    if (on == a->m.fps_hud) return;
    JkSettings next = a->set;
    next.fps_hud = on;
    save_settings(a, &next, on ? "fps_hud on" : "fps_hud off");
}

static void set_logs(JkApp *a, int on)
{
    if (on == a->m.logs_on) return;
    JkSettings next = a->set;
    next.logs = on;
    a->m.bundle_state = 0;
    if (save_settings(a, &next, on ? "logs on" : "logs off") == 0) {
        snprintf(a->m.logs_msg, sizeof a->m.logs_msg, "%s", on ? JK_S_L_LOGS_ON : "");
        a->m.logs_msg_bad = 0;
    } else {
        snprintf(a->m.logs_msg, sizeof a->m.logs_msg, "%s", JK_S_SET_SAVE_FAILED);
        a->m.logs_msg_bad = 1;
    }
}

static void do_clear_logs(JkApp *a)
{
    a->busy = 1;
    a->m.bundle_state = 0;
    int n = jk_logs_clear(a->s->logs_dir);
    char num[16];
    snprintf(num, sizeof num, "%d", n);
    event(a, "logs cleared: %s", num, NULL);
    a->m.logs_msg_bad = n < 0;
    if (n < 0) snprintf(a->m.logs_msg, sizeof a->m.logs_msg, "%s", JK_S_L_CLEAR_FAILED);
    else if (n == 0) snprintf(a->m.logs_msg, sizeof a->m.logs_msg, "%s", JK_S_L_CLEARED_NONE);
    else if (n == 1) snprintf(a->m.logs_msg, sizeof a->m.logs_msg, "%s", JK_S_L_CLEARED_ONE);
    else snprintf(a->m.logs_msg, sizeof a->m.logs_msg, JK_S_L_CLEARED, n);
    count_logs(a);
}

static void finish(JkApp *a, JkAction action)
{
    if (action == JK_START_GAME && refuse_two(a, "play")) return;
    if (action == JK_START_GAME && a->pending) {     /* half-swapped mod files: never started on */
        event(a, "play refused: a Patch waits to be undone", NULL, NULL);
        go(a, JK_SCR_PATCH);
        return;
    }
    a->done = 1;
    a->action = action;
    if (action == JK_START_GAME) {
        compose(a);                                   /* the game reads the ini and the profile as written now */
        event(a, "start pace=%s", jk_pace_key(a->m.pace), NULL);
    }
}

/* One button: the highest-priority newly pressed one. */
static uint32_t pick(uint32_t pressed)
{
    static const uint32_t order[] = { JK_PAD_CIRCLE, JK_PAD_CROSS, JK_PAD_TRIANGLE, JK_PAD_SQUARE,
                                      JK_PAD_UP, JK_PAD_DOWN, JK_PAD_LEFT, JK_PAD_RIGHT };
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++)
        if (pressed & order[i]) return order[i];
    return 0;
}

static void press_home(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (b == JK_PAD_UP) m->focus = (m->focus + JK_HOME_COUNT - 1) % JK_HOME_COUNT;
    else if (b == JK_PAD_DOWN) m->focus = (m->focus + 1) % JK_HOME_COUNT;
    else if (b == JK_PAD_SQUARE) open_help(a, jk_two_folders(m) ? JK_HELP_TROUBLE : JK_HELP_FOLDER);
    else if (b == JK_PAD_CROSS) {
        switch (m->focus) {
        case JK_HOME_PLAY:
            recheck(a);                                 /* the folder may have changed since it opened */
            if (m->check.state == JK_ST_READY && !jk_two_folders(m) && !a->pending) finish(a, JK_START_GAME);
            else go(a, JK_SCR_PATCH);
            break;
        case JK_HOME_PATCH: go(a, JK_SCR_PATCH); break;
        default: go(a, JK_SCR_SETTINGS); break;
        }
    }
}

static void press_settings(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (b == JK_PAD_CIRCLE) { go(a, JK_SCR_HOME); m->focus = JK_HOME_SETTINGS; return; }
    if (b == JK_PAD_UP) { m->set_focus = (m->set_focus + JK_SET_COUNT - 1) % JK_SET_COUNT; return; }
    if (b == JK_PAD_DOWN) { m->set_focus = (m->set_focus + 1) % JK_SET_COUNT; return; }
    if (m->set_focus == JK_SET_FRAMERATE) {
        if (!jk_pace_enabled(m)) return;
        if (b == JK_PAD_LEFT || b == JK_PAD_RIGHT) set_pace(a, jk_pace_step(m->pace, b == JK_PAD_RIGHT ? 1 : -1));
        else if (b == JK_PAD_CROSS) set_pace(a, m->pace == JK_PACE_60 ? JK_PACE_30 : JK_PACE_60);
        return;
    }
    if (m->set_focus == JK_SET_FPS_HUD) {             /* OFF | ON, like JkFlow's segments */
        if (b == JK_PAD_LEFT) set_fps_hud(a, 0);
        else if (b == JK_PAD_RIGHT) set_fps_hud(a, 1);
        else if (b == JK_PAD_CROSS) set_fps_hud(a, !m->fps_hud);
        return;
    }
    if (b != JK_PAD_CROSS) return;
    switch (m->set_focus) {
    case JK_SET_MODS: a->mods_back = JK_SCR_SETTINGS; go(a, JK_SCR_MODS); break;
    case JK_SET_CONTROLS: go(a, JK_SCR_CONTROLS); break;
    case JK_SET_SAVES: go(a, JK_SCR_SAVES); break;
    case JK_SET_LOGS: a->logs_back = JK_SCR_SETTINGS; go(a, JK_SCR_LOGS); break;
    default: go(a, JK_SCR_ABOUT); break;
    }
}

/* Back from Logs & Support: Settings as it was, or the Patch failure it came from. */
static void logs_back(JkApp *a)
{
    if (a->logs_back == JK_SCR_PATCH) {
        JkPatchPhase ph = a->m.phase;
        char msg[sizeof a->m.msg];
        memcpy(msg, a->m.msg, sizeof msg);
        go(a, JK_SCR_PATCH);
        a->m.phase = ph;
        memcpy(a->m.msg, msg, sizeof msg);
    } else {
        a->m.screen = JK_SCR_SETTINGS;
        a->m.dialog = JK_DLG_NONE;
    }
}

static void press_logs(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (b == JK_PAD_CIRCLE) { logs_back(a); return; }
    if (b == JK_PAD_UP) { m->logs_focus = (m->logs_focus + JK_LOG_COUNT - 1) % JK_LOG_COUNT; return; }
    if (b == JK_PAD_DOWN) { m->logs_focus = (m->logs_focus + 1) % JK_LOG_COUNT; return; }
    switch (m->logs_focus) {
    case JK_LOG_ENABLE:
        if (b == JK_PAD_LEFT) set_logs(a, 0);
        else if (b == JK_PAD_RIGHT) set_logs(a, 1);
        else if (b == JK_PAD_CROSS) set_logs(a, !m->logs_on);
        break;
    case JK_LOG_CLEAR:
        if (b == JK_PAD_CROSS) do_clear_logs(a);
        break;
    default:
        if (b == JK_PAD_CROSS) { m->logs_msg[0] = 0; do_bundle(a); count_logs(a); }
        break;
    }
}

static void press_help(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (b == JK_PAD_LEFT) m->help_page = (m->help_page + JK_HELP_COUNT - 1) % JK_HELP_COUNT;
    else if (b == JK_PAD_RIGHT) m->help_page = (m->help_page + 1) % JK_HELP_COUNT;
    else if (b == JK_PAD_CIRCLE) { m->screen = a->help_back; m->dialog = JK_DLG_NONE; }   /* as it was left */
}

static void press_interrupted(JkApp *a, uint32_t b)
{
    /* ○: Home, or "The game didn't start" when that happened too (both are said) */
    if (b == JK_PAD_CIRCLE) { if (a->m.failure) a->m.screen = JK_SCR_FAILED; else go(a, JK_SCR_HOME); return; }
    if (b == JK_PAD_SQUARE) { open_help(a, JK_HELP_TROUBLE); return; }
    if (b != JK_PAD_CROSS) return;
    go(a, JK_SCR_PATCH);                               /* Patch again: the same PATCH, from the start */
    JkPatchAction act = jk_patch_primary(&a->m);
    if (act == JK_ACT_INSTALL || act == JK_ACT_START) do_install(a);
}

/* Back to Settings with its focus and state kept (no recheck needed). */
static void back_to_settings(JkApp *a)
{
    a->m.screen = JK_SCR_SETTINGS;
    a->m.dialog = JK_DLG_NONE;
}

/* The keyboard's OK: <old>/<old> becomes the new name (backup first). */
static void do_rename(JkApp *a)
{
    JkModel *m = &a->m;
    a->busy = 1;
    m->dialog = JK_DLG_NONE;
    const char *old = m->save_names[m->save_pick], *nw = jk_keyboard_text(&m->kb);
    if (!strcmp(old, nw)) return;
    char stamp[64], why[200];
    stamp_of(a, stamp, sizeof stamp);
    if (jk_saves_rename(a->s->saves_dir, old, nw, a->s->data_root, stamp[0] ? stamp : NULL, m->saves_msg, sizeof m->saves_msg,
                        why, sizeof why) == 0) {
        m->saves_state = 3;
        event(a, "profile renamed to %s (backup %s)", nw, m->saves_msg);
    } else {
        m->saves_state = -1;
        snprintf(m->saves_msg, sizeof m->saves_msg, "%s", why);
        event(a, "profile rename failed: %s", why, NULL);
    }
    read_saves(a);
}

static void press_saves(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (m->dialog == JK_DLG_IMPORT) {
        if (b == JK_PAD_CROSS) {
            if (m->saves_found.collisions) m->dialog = JK_DLG_COLLIDE;      /* ask before anything changes */
            else do_import(a, 0);
        } else if (b == JK_PAD_CIRCLE) m->dialog = JK_DLG_NONE;
        return;
    }
    if (m->dialog == JK_DLG_COLLIDE) {
        if (b == JK_PAD_CROSS) do_import(a, 0);
        else if (b == JK_PAD_SQUARE) do_import(a, 1);
        else if (b == JK_PAD_CIRCLE) m->dialog = JK_DLG_NONE;
        return;
    }
    if (m->dialog == JK_DLG_PICK) {
        if (b == JK_PAD_UP && m->save_pick > 0) m->save_pick--;
        else if (b == JK_PAD_DOWN && m->save_pick + 1 < m->save_count) m->save_pick++;
        else if (b == JK_PAD_CROSS) {
            jk_keyboard_init(&m->kb, JK_S_R_KB, m->save_names[m->save_pick], JK_ALIAS_MAX, jk_alias_char);
            m->dialog = JK_DLG_KEYBOARD;
        } else if (b == JK_PAD_CIRCLE) m->dialog = JK_DLG_NONE;
        return;
    }
    if (m->dialog == JK_DLG_KEYBOARD) {
        int r = jk_keyboard_input(&m->kb, b);
        if (r == JK_KB_CANCEL) m->dialog = JK_DLG_NONE;
        if (r == JK_KB_DONE) do_rename(a);
        return;
    }
    if (b == JK_PAD_CIRCLE) back_to_settings(a);
    else if (b == JK_PAD_CROSS) do_export(a);
    else if (b == JK_PAD_SQUARE) find_saves(a);
    else if (b == JK_PAD_TRIANGLE) {
        m->save_count = jk_saves_list(a->s->saves_dir, m->save_names, JK_SAVES_CAND_MAX);
        m->save_pick = 0;
        if (!m->save_count) {
            m->saves_state = -1;
            snprintf(m->saves_msg, sizeof m->saves_msg, "%s", JK_S_R_NONE);
        } else if (m->save_count == 1) {
            jk_keyboard_init(&m->kb, JK_S_R_KB, m->save_names[0], JK_ALIAS_MAX, jk_alias_char);
            m->dialog = JK_DLG_KEYBOARD;
        } else {
            m->dialog = JK_DLG_PICK;
        }
    }
}

static void press_patch(JkApp *a, uint32_t b)
{
    JkModel *m = &a->m;
    if (m->phase == JK_PH_INSTALLING) return;
    if (m->dialog == JK_DLG_RESTORE) {
        if (b == JK_PAD_CROSS) do_restore(a);
        else if (b == JK_PAD_CIRCLE) m->dialog = JK_DLG_NONE;
        return;
    }
    if (b == JK_PAD_CIRCLE) { go(a, JK_SCR_HOME); return; }
    if (b == JK_PAD_TRIANGLE && jk_patch_can_restore(m)) { m->dialog = JK_DLG_RESTORE; return; }
    if (b == JK_PAD_SQUARE && jk_patch_square_help(m)) { open_help(a, JK_HELP_TROUBLE); return; }
    if (b == JK_PAD_SQUARE && (m->phase == JK_PH_FAILED || (m->phase == JK_PH_IDLE && m->check.state == JK_ST_BAD_PACKAGE))) {
        a->logs_back = JK_SCR_PATCH;                  /* the support bundle, from the failure */
        go(a, JK_SCR_LOGS);
        a->m.logs_focus = JK_LOG_BUNDLE;
        return;
    }
    if (b == JK_PAD_SQUARE && jk_patch_can_mods(m)) { a->mods_back = JK_SCR_PATCH; go(a, JK_SCR_MODS); return; }
    if (b != JK_PAD_CROSS) return;
    switch (jk_patch_primary(m)) {
    case JK_ACT_CHECK_AGAIN: m->phase = JK_PH_IDLE; recheck(a); break;
    case JK_ACT_INSTALL: do_install(a); break;
    case JK_ACT_START: finish(a, JK_START_GAME); break;
    default: break;
    }
}

int jk_app_press(JkApp *a, uint32_t pressed)
{
    if (!a || a->done) return 0;
    uint32_t b = pick(pressed);
    if (!b) return 0;
    switch (a->m.screen) {
    case JK_SCR_PATCH: press_patch(a, b); break;
    case JK_SCR_SETTINGS: press_settings(a, b); break;
    case JK_SCR_CONTROLS:
        if (b == JK_PAD_CIRCLE) back_to_settings(a);
        break;
    case JK_SCR_ABOUT:
        if (b == JK_PAD_CIRCLE) back_to_settings(a);
        else if (b == JK_PAD_SQUARE) go(a, JK_SCR_CREDITS);
        break;
    case JK_SCR_CREDITS:
        if (b == JK_PAD_CIRCLE) go(a, JK_SCR_ABOUT);
        else if (b == JK_PAD_DOWN && a->m.credits_top + 12 < jk_credits_lines(a->m.credits)) a->m.credits_top += 4;
        else if (b == JK_PAD_UP) a->m.credits_top = a->m.credits_top > 4 ? a->m.credits_top - 4 : 0;
        break;
    case JK_SCR_SAVES: press_saves(a, b); break;
    case JK_SCR_MODS: press_mods(a, b); break;
    case JK_SCR_LOGS: press_logs(a, b); break;
    case JK_SCR_HELP: press_help(a, b); break;
    case JK_SCR_INTERRUPTED: press_interrupted(a, b); break;
    case JK_SCR_FAILED:
        if (b == JK_PAD_SQUARE) do_bundle(a);
        else if (b == JK_PAD_TRIANGLE) open_help(a, JK_HELP_TROUBLE);
        else if (b == JK_PAD_CROSS) {
            a->m.bundle_state = 0;
            go(a, JK_SCR_HOME);
        }
        break;
    default: press_home(a, b); break;
    }
    return 1;
}

/* ---- run loop ---- */

/* Cross-fade of the focus bar: red|blue to blue|red and back, once per 2 s. */
static int siren_at(uint64_t ms)
{
    int t = (int)(ms % 2000u);
    /* hold each side for 700 ms, fade over 300 ms */
    if (t < 700) return 0;
    if (t < 1000) return (t - 700) * 256 / 300;
    if (t < 1700) return 256;
    return 256 - (t - 1700) * 256 / 300;
}

static const JkModel *running;
const JkModel *jk_frontend_model(void) { return running; }

int jk_frontend_run(const JkServices *s, JkDecision *out)
{
    if (!s || !out || !s->present || !s->now_ns || !s->sleep_us || !s->game_dir || !s->data_root || !s->files_dir) return -1;
    memset(out, 0, sizeof *out);
    JkFrame frame = { malloc((size_t)JK_W * JK_H * 4), JK_W, JK_H, JK_W * 4 };
    if (!frame.pixels) return -1;
    static JkApp a;
    jk_app_init(&a, s, &frame);
    running = &a.m;
    if (jk_app_setup(&a) != 0) a.busy = 0;              /* r5: the prefix at open, once */
    report_screen(&a);
    jk_screen_draw(&frame, &a.m);
    int rc = 0;
    if (s->present(s->ctx, &frame) < 0) rc = -1;
    uint64_t t0 = s->now_ns(s->ctx), last_anim = 0;
    while (rc == 0 && !a.done) {
        if (s->should_quit && s->should_quit(s->ctx)) { a.done = 1; a.action = JK_LEAVE_HOME; break; }
        uint32_t held = 0, pressed = 0;
        if (s->pad && s->pad(s->ctx, &held, &pressed) < 0) pressed = 0;
        int dirty = jk_app_press(&a, pressed);
        if (a.done) break;
        if (a.busy) {                                   /* drop what was pressed while it ran */
            a.busy = 0;
            if (s->pad) s->pad(s->ctx, &held, &pressed);
        }
        report_screen(&a);
        uint64_t ms = (s->now_ns(s->ctx) - t0) / 1000000u;
        if ((a.m.screen == JK_SCR_HOME || a.m.screen == JK_SCR_SETTINGS || a.m.screen == JK_SCR_LOGS || a.m.screen == JK_SCR_HELP)
            && ms - last_anim >= 66) {
            int sir = siren_at(ms);
            if (sir != a.m.siren) { a.m.siren = sir; dirty = 1; }
            last_anim = ms;
        }
        if (dirty) {
            jk_screen_draw(&frame, &a.m);
            if (s->present(s->ctx, &frame) < 0) { rc = -1; break; }
        } else {
            s->sleep_us(s->ctx, 16000);
        }
    }
    running = NULL;
    free(frame.pixels);
    if (rc < 0) return rc;
    out->action = a.action ? a.action : JK_LEAVE_HOME;
    out->pace = a.m.pace;
    return 0;
}

void jk_frontend_release(void)
{
    jk_art_release();
    jk_fs_free_fn = NULL;
    jk_fs_free_ctx = NULL;
}
