/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * JK4SPEED title: the launcher's host services (launcher/INTEGRATION.md),
 * included by prospero/native/wine64_main.c of the bench title tree
 * native298-package277 (title-jk4speed.patch). It uses that file's statics:
 * now_ns, leave_to_home, restart_title, launch, runtime_roots, pad_open_map,
 * PS5LOG_LOG. Same pattern as JkFlow's BlurHost (JkFlow-export/title,
 * blur_launcher_screen).
 *
 * r4: everything lives under /data/Jk4Speed (jk_layout.h), like JkFlow's
 * /data/JkFlow. The Wine library root is /data/Jk4Speed/System (profiles/,
 * prefix/, cache/, control/), the session logs go to /data/Jk4Speed/Logs, and
 * nothing is written under /data/prospero-win. PLAY restarts the title into
 * the nfsmw profile (System/profiles/nfsmw.profile, composed by the launcher;
 * "prefix = default" is System/prefix, seeded at PATCH); when the game ends
 * Wine exits and the title comes back here (on_exit_report).
 */
#include "jk_frontend.h"
#include "jk_host.h"
#include "jk_layout.h"
#include "jk_strings.h"
/* Production package (default): no console test hook, no user ini.
 * build_title.sh JK_DEV=1 builds the bench title with both. */
#ifndef JK_TITLE_PROD
#define JK_TITLE_PROD 1
#endif
#if !JK_TITLE_PROD
#include "jk_devhook.h"
#endif
#include "jk_release.h"
#include "jk_release_key.h"
/* JK_TITLE_PRODUCT_ROOT, JK_TITLE_DATA_ROOT and JK_TITLE_PROFILE_ID come from
 * the patch's top block (the loader hooks earlier in the file use them too). */

/* ---- folders ------------------------------------------------------------ */

static JkLayout jk_layout;
static char jk_layout_note[256];

/* Every title start, right after the /data grant and before the session log
 * opens (open_library): r3's lower-case state folders under /data/Jk4Speed
 * move aside (jk_layout_migrate_r3, before anything is made: a volume that
 * ignores letter case would otherwise see r3's logs/ as our Logs/), then the
 * r4 folders are made. The game folder is never made. */
static void jk_title_layout_log(void *ctx, const char *line)
{
    (void)ctx;
    PS5LOG_LOG("JK4SPEED %s", line);
    if (!jk_layout_note[0]) snprintf(jk_layout_note, sizeof(jk_layout_note), "%s", line);
}

static void jk_title_layout(void)
{
    jk_layout_init(&jk_layout, JK_TITLE_PRODUCT_ROOT);
    int moved = jk_layout_migrate_r3(&jk_layout, jk_title_layout_log, NULL);
    int made = jk_layout_make(&jk_layout);
    PS5LOG_LOG("JK4SPEED layout root=%s migrated=%d made=%d", jk_layout.root, moved, made);
}

/* ---- services ----------------------------------------------------------- */

typedef struct JkHost {
    PwVideoOutPs5 video;
    PwPadPs5 pad;
    PwPadPs5Ops pad_ops;
    int pad_status, ops_ok;
    uint64_t pad_tried;
} JkHost;

static int jk_host_present(void *ctx, const JkFrame *f)
{
    JkHost *h = ctx;
    const PwPresentView view = { f->pixels, f->width, f->height, f->stride, f->stride * f->height };
    int rc = pw_videoout_ps5_present(&h->video, &view);
    (void)sceGnmSubmitDone();
    return rc == PW_OK ? 0 : -1;
}

static int jk_host_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    JkHost *h = ctx;
    *held = *pressed = 0;
    if (h->pad_status != PW_OK) {
        uint64_t now = now_ns();
        /* No controller signed in yet: try again every 2 s. */
        if (!h->ops_ok || now - h->pad_tried < 2000000000ull) return -1;
        h->pad_tried = now;
        h->pad_status = pw_pad_ps5_open(&h->pad, &h->pad_ops, pad_open_map,
                                        sizeof(pad_open_map) / sizeof(pad_open_map[0]));
        if (h->pad_status != PW_OK) return -1;
    }
    if (pw_pad_ps5_read(&h->pad) != PW_OK) return -1;
    *held = h->pad.core.previous_buttons;
    *pressed = h->pad.core.pressed_edges;
    return 0;
}

static uint64_t jk_host_now(void *ctx) { (void)ctx; return now_ns(); }
static void jk_host_sleep(void *ctx, uint32_t us) { (void)ctx; usleep(us); }

static int jk_host_wall(void *ctx, char *out, size_t cap)
{
    struct timespec ts;
    (void)ctx;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
    return jk_host_stamp((long long)ts.tv_sec, out, cap);
}

/* Free space of the volume holding path (jk_host_free_probe: fstatfs on a
 * directory fd, then statfs, then the parent folder; the first failure logs
 * call, rc, errno and the raw head once). <0 unknown.
 * r3 on the console always answered unknown with the same code JkFlow ships.
 * The title runs in the sandbox /mnt/sandbox/<TITLEID>_000 where /data is a
 * separate mount, and Sony's sandbox refuses some syscalls (lstat, dup2 EPERM
 * in our logs), so the kernel refusing fstatfs there is the leading suspect;
 * the probe line proves or clears it.
 * libkernel exports _fstatfs only; the SDK libc's statfs (libc.a, no new
 * import) is open + _fstatfs and returns the fd, not 0, so the probe reads its
 * buffer for any rc >= 0. Its prototype is not taken from a header (it would
 * want the SDK's 2344-byte struct statfs). */
extern int _fstatfs(int fd, void *buf);
extern int jk_libc_statfs(const char *path, void *buf) __asm__("statfs");
static int jk_host_fstatfs_dir(void *ctx, const char *path, void *buf, size_t cap, int *err)
{
    int fd, rc;
    (void)ctx; (void)cap;
    errno = 0;
    if ((fd = open(path, O_RDONLY | O_DIRECTORY)) < 0) { *err = errno; return -1; }
    rc = _fstatfs(fd, buf);
    *err = rc == 0 ? 0 : errno;
    close(fd);
    return rc;
}
static int jk_host_statfs_path(void *ctx, const char *path, void *buf, size_t cap, int *err)
{
    int rc;
    (void)ctx; (void)cap;
    errno = 0;
    rc = jk_libc_statfs(path, buf);
    *err = errno;
    return rc;
}
static void jk_host_log(void *ctx, const char *line);
static long long jk_host_free_bytes(void *ctx, const char *path)
{
    static JkFreeProbe probe = { jk_host_fstatfs_dir, jk_host_statfs_path, jk_host_log, NULL, 0 };
    probe.ctx = ctx;
    return jk_host_free_probe(&probe, path);
}

static void jk_host_log(void *ctx, const char *line)
{
    (void)ctx;
    PS5LOG_LOG("JK4SPEED %s", line);
}

static void jk_host_screen(void *ctx, const char *name)
{
    (void)ctx;
    PS5LOG_LOG("JK4SPEED screen=%s", name);
}

/* Runs the JK4SPEED launcher until PLAY (restarts into the game) or close
 * (leaves to the console's home). Does not return. */
static void jk_title_launcher(void)
{
    static JkHost host;
    static char files_dir[200], game_dir[256], game_other[256], game_note[600], game_exe[300];
    static const char *const usb_roots[] = { "/mnt/usb0", "/mnt/usb1", NULL };
    static JkDecision decision;
    JkServices s;

    memset(&s, 0, sizeof(s));
    if (!jk_layout.root[0]) jk_title_layout();
    for (size_t i = 0; i < sizeof(runtime_roots) / sizeof(runtime_roots[0]) && !files_dir[0]; i++) {
        char probe[240];
        snprintf(probe, sizeof(probe), "%s/jk4speed-files/manifest.txt", runtime_roots[i]);
        if (access(probe, R_OK) == 0) snprintf(files_dir, sizeof(files_dir), "%s/jk4speed-files", runtime_roots[i]);
    }
    /* The game folder: /data/Jk4Speed/NFSMW in any letter case (two spellings
     * are both logged and shown: s.game_dir_other), or the devtools
     * clean-test folder. */
    int found = jk_host_game_dir(JK_TITLE_PRODUCT_ROOT, game_dir, sizeof(game_dir), game_other, sizeof(game_other),
                                 game_note, sizeof(game_note));
    int video_status = pw_videoout_ps5_open(&host.video);
    host.ops_ok = pw_pad_ps5_platform_ops(&host.pad_ops) == PW_OK;
    host.pad_status = host.ops_ok ? pw_pad_ps5_open(&host.pad, &host.pad_ops, pad_open_map,
                                                    sizeof(pad_open_map) / sizeof(pad_open_map[0]))
                                  : PW_ERR_STATE;
    host.pad_tried = now_ns();
    const char *usb = jk_host_first_dir(usb_roots);
    PS5LOG_LOG("JK4SPEED launcher open video=%s pad=%s files=%s usb=%s cycle=%u", pw_result_name(video_status),
               pw_result_name(host.pad_status), files_dir[0] ? files_dir : "-", usb ? usb : "-",
               (unsigned)launch.cycle);
    PS5LOG_LOG("JK4SPEED game folder=%s found=%d%s%s", game_dir, found, game_note[0] ? " " : "", game_note);
    if (jk_layout_note[0]) PS5LOG_LOG("JK4SPEED layout: %s", jk_layout_note);
    s.ctx = &host;
    s.present = jk_host_present;
    s.pad = jk_host_pad;
    s.now_ns = jk_host_now;
    s.sleep_us = jk_host_sleep;
    s.wall_time = jk_host_wall;
    s.screen = jk_host_screen;
    s.log = jk_host_log;
    s.free_bytes = jk_host_free_bytes;
    s.game_dir = game_dir;
    /* two spellings (NFSMW and nfsmw): the launcher names both and blocks
     * PLAY / PATCH until only one is left (no guessing) */
    s.game_dir_other = found == -1 && game_other[0] ? game_other : NULL;
    /* /data/Jk4Speed/System; a devtools clean-test run keeps its own state in
     * /data/Jk4Speed/clean-test/System (backup, modset, state, cache, the
     * launcher log), so a test PATCH never shares a record or a journal with
     * the player's folder. The Wine prefix and profiles/ stay shared. */
    static char data_root[300];
    if (jk_host_data_root(JK_TITLE_PRODUCT_ROOT, found, jk_layout.system, data_root, sizeof(data_root)) != 0)
        snprintf(data_root, sizeof(data_root), "%s", jk_layout.system);
    s.data_root = data_root;
    s.shared_root = jk_layout.system;                                   /* prefix/ (seeded at PATCH) */
    s.files_dir = files_dir[0] ? files_dir : "/app0/jk4speed-files";   /* missing: PATCH shows "app damaged" */
    s.logs_dir = jk_layout.logs;                                        /* /data/Jk4Speed/Logs */
    s.runtime_root = NULL;
    s.profiles_dir = jk_layout.profiles;                                /* the library root's profiles/ */
    s.override_dir = NULL;          /* the native server ships in sce_module/dxvknative.prx */
    s.mods_dir = jk_layout.mods;
    s.product_root = jk_layout.root;
    s.saves_dir = jk_layout.saves_game;                                 /* /data/Jk4Speed/Saves/NFS Most Wanted */
    /* r3's save folder on the bench prefix: read once and copied into saves_dir
     * (jk_saves_migrate_once); never written. */
    s.old_saves_dir = "/data/prospero-win/prefixes/nfsmw/drive_c/users/prospero/Documents/NFS Most Wanted";
    s.usb_dir = usb;
    s.build_id = "JK4SPEED 1.1";
    s.require_x360 = 1;            /* r5: the Xbox 360 Stuff Pack is required; HUD and roads optional */
    /* 1.1: two games. Slot 0 = NFSMW (Black Edition), exactly the 1.0 fields above;
     * slot 1 = NFS (the original): any speed.exe, no mods, own data root and saves. */
    static char nfs_dir[256], nfs_other[256];
    int found_nfs = jk_game_dir_find_leaf(JK_TITLE_PRODUCT_ROOT, "NFS", nfs_dir, sizeof(nfs_dir), nfs_other,
                                          sizeof(nfs_other));
    PS5LOG_LOG("JK4SPEED game folder nfs=%s found=%d", nfs_dir, found_nfs);
    s.games[0] = (JkGameSlot){ "nfsmw", JK_S_G_BE, "NFSMW", game_dir, s.game_dir_other, data_root,
                               jk_layout.saves_game, "Z:\\data\\Jk4Speed\\Saves", 0, 1, 1, NULL };
    /* the original's package folder: the Black Edition's files without the X360 parts, plus
     * ThirteenAG's Widescreen Fix (make_pkg.sh); missing: its PATCH says "app damaged" */
    static char nfs_files[220];
    snprintf(nfs_files, sizeof(nfs_files), "%s-nfs", s.files_dir);
    s.games[1] = (JkGameSlot){ "nfs", JK_S_G_OG, "NFS", nfs_dir,
                               found_nfs == -1 && nfs_other[0] ? nfs_other : NULL, jk_layout.system_nfs,
                               jk_layout.saves_nfs_game, "Z:\\data\\Jk4Speed\\Saves-NFS", 1, 0, 0, nfs_files };
    s.ngames = 2;
    s.prod = JK_TITLE_PROD;        /* production: no test hook, no user ini */
    /* r5: the badge, from the package's signed file list (jk_release.h);
     * never turns anything off */
    static JkRelease release;
    jk_release_verify("/app0", JK_RELEASE_PK, &release);
    s.release = &release;
    /* the last PLAY never reached the game's first frame: say so (jk_session.h) */
    static char failed_note[96];
    if (jk_session_take_failure(JK_TITLE_DATA_ROOT, failed_note, sizeof(failed_note))) {
        s.start_failure = "The game stopped before its first picture.";
        PS5LOG_LOG("JK4SPEED previous start failed (%s)", failed_note);
    }

    /* Console test hook (devtools/jk_devhook.h): attached only when
     * /data/Jk4Speed/devtools/enable exists; otherwise s is untouched. */
#if !JK_TITLE_PROD
    static JkDevHook devhook;
    if (jk_devhook_attach(&devhook, &s) == 1)
        PS5LOG_LOG("JK4SPEED devtools on dir=%s session=%s", devhook.dir, devhook.session);
#endif

    int rc = video_status == PW_OK ? jk_frontend_run(&s, &decision) : -1;
    if (host.pad_status == PW_OK) (void)pw_pad_ps5_close(&host.pad);
    if (video_status == PW_OK) (void)pw_videoout_ps5_close(&host.video);
    if (rc != 0) {
        PS5LOG_LOG("JK4SPEED launcher run=%d video=%s", rc, pw_result_name(video_status));
        leave_to_home("jk4speed-launcher-no-display");
    }
    if (decision.action != JK_START_GAME) leave_to_home("jk4speed-launcher-close");
    jk_loader_release();
    jk_frontend_release();
    const JkGameSlot *play = &s.games[decision.game == 1 ? 1 : 0];
    if (jk_host_game_exe(play->game_dir, game_exe, sizeof(game_exe)) != 0) {
        PS5LOG_LOG("JK4SPEED start refused: game folder path %s", play->game_dir);
        leave_to_home("jk4speed-game-path");
    }
    static PwWineApp game;
    game = (PwWineApp){ play->key, "JK4SPEED", "", game_exe };
    PS5LOG_LOG("JK4SPEED start profile=%s exe=%s pace=%d", game.id, game.executable, (int)decision.pace);
    {
        char note[64], stamp[32] = "";
        (void)jk_host_wall(NULL, stamp, sizeof(stamp));
        snprintf(note, sizeof(note), "pace=%s at %s", jk_pace_key(decision.pace), stamp[0] ? stamp : "?");
        if (jk_session_starting(JK_TITLE_DATA_ROOT, note) != 0) PS5LOG_LOG("JK4SPEED starting marker not written");
    }
    restart_title(&game, launch.cycle, "jk4speed-play");
    pw_diagnostics_close("jk4speed-launch-failed");
    _exit(1);
}
