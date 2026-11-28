/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The release config: settings file, composed jk4speed-dev.ini and nfsmw.profile, Clear logs. */
#include <sys/stat.h>
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_config.h"
#include "jk_controls.h"
#include "jk_fs.h"

#define BASE_DIR JK_ROOT_DIR "/title/config"

static void cfg_in(JkConfigIn *in, const char *root, const char *files)
{
    static char game[300], prof[300], cache[300], logs[300];
    snprintf(game, sizeof game, "%s", "/data/Jk4Speed/NFSMW");
    snprintf(prof, sizeof prof, "%s/profiles", root);
    snprintf(cache, sizeof cache, "%s", "/data/Jk4Speed/System/cache");
    snprintf(logs, sizeof logs, "%s", "/data/Jk4Speed/Logs");
    memset(in, 0, sizeof *in);
    in->files_dir = files;
    in->game_dir = game;
    in->profiles_dir = prof;
    in->cache_dir = cache;
    in->logs_dir = logs;
}

/* A config input whose game and profiles folders really exist under root. */
static void cfg_real(JkConfigIn *in, const char *root, const char *files)
{
    static char game[300];
    snprintf(game, sizeof game, "%s/game", root);
    mkdir(game, 0755);
    cfg_in(in, root, files);
    in->game_dir = game;
    mkdir(in->profiles_dir, 0755);
}

static int has_line(const char *text, const char *line)
{
    size_t n = strlen(line);
    for (const char *p = text; *p;) {
        const char *e = strpbrk(p, "\r\n");
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, line, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static int read_into(const char *dir, const char *name, char *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t len = 0;
    char *t = jk_fs_read(path, 65536, &len);
    if (!t) return -1;
    snprintf(buf, cap, "%s", t);
    free(t);
    return (int)len;
}

static int read_ini(const JkConfigIn *in, char *buf, size_t cap) { return read_into(in->game_dir, "jk4speed-dev.ini", buf, cap); }
static int read_profile(const JkConfigIn *in, char *buf, size_t cap) { return read_into(in->profiles_dir, "nfsmw.profile", buf, cap); }

static void settings_defaults_off(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    JkSettings s = { JK_PACE_60, 1, 1 };
    CHECK_EQ(jk_settings_load(d, &s), 0);
    CHECK_EQ(s.pace, JK_PACE_30);
    CHECK_EQ(s.fps_hud, 0);
    CHECK_EQ(s.logs, 0);
    JkSettings t = { JK_PACE_60, 1, 1 };
    CHECK_EQ(jk_settings_load("/nonexistent/folder", &t), 0);    /* a missing folder is the same as a missing file */
    CHECK_EQ(t.pace, JK_PACE_30);
    CHECK_EQ(t.fps_hud, 0);
    tmp_put(d, "launcher.ini", "pace=45\nfps_hud=7\nlogs=yes\n");
    CHECK_EQ(jk_settings_load(d, &s), 0);                        /* nonsense falls back to the defaults */
    CHECK_EQ(s.pace, JK_PACE_30);
    CHECK_EQ(s.fps_hud, 0);
    CHECK_EQ(s.logs, 0);
    tmp_rm_rf(d);
}

static void settings_roundtrip(void)
{
    char d[256], sub[300];
    tmp_dir(d, sizeof d);
    snprintf(sub, sizeof sub, "%s/state", d);                    /* the state folder may not exist yet */
    JkSettings a = { JK_PACE_60, 1, 1 }, b = { JK_PACE_30, 0, 0 };
    CHECK_EQ(jk_settings_save(sub, &a), 0);
    CHECK_EQ(jk_settings_load(sub, &b), 0);
    CHECK_EQ(b.pace, JK_PACE_60);
    CHECK_EQ(b.fps_hud, 1);
    CHECK_EQ(b.logs, 1);
    JkSettings c = { JK_PACE_30, 1, 0 }, e = { JK_PACE_60, 0, 1 };
    CHECK_EQ(jk_settings_save(sub, &c), 0);
    CHECK_EQ(jk_settings_load(sub, &e), 0);
    CHECK_EQ(e.pace, JK_PACE_30);
    CHECK_EQ(e.fps_hud, 1);
    CHECK_EQ(e.logs, 0);
    tmp_rm_rf(d);
}

static void release_ini_has_every_bench_key_zero(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    char why[200] = "";
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    int n = 0;
    for (int i = 0; JK_BENCH_KEYS[i]; i++, n++) {
        char line[96];
        snprintf(line, sizeof line, "%s=0", JK_BENCH_KEYS[i]);
        CHECK(has_line(buf, line));
    }
    CHECK_EQ(n, 7);
    CHECK(has_line(buf, "DXVK_PS4_NFS_PACE=30"));
    CHECK(has_line(buf, "[jk4speed]"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_WORLD_LOD=1"));            /* the base's own keys stay */
    CHECK(strstr(buf, "DXVK_PS4_DIAG") == NULL);
    CHECK(strstr(buf, "\r\n") != NULL);
    for (const char *p = buf; (p = strchr(p, '\n')); p++) CHECK(p > buf && p[-1] == '\r');   /* CRLF only */
    tmp_rm_rf(d);
}

static void ini_pace_60(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.set.pace = JK_PACE_60;
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_NFS_PACE=60"));
    CHECK(!has_line(buf, "DXVK_PS4_NFS_PACE=30"));
    tmp_rm_rf(d);
}

/* r5: <product_root>/jk4speed-user.ini (owner's test file): a key there wins
 * over the base, a new key is added, bench keys and the pace stay the
 * launcher's; the composed ini is 0666 (FTP can replace it). */
static void user_ini_overrides(void)
{
    char d[256], buf[8192], u[400];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    tmp_put(d, "user/jk4speed-user.ini", "; my tests\r\n[x]\r\ndxvk_ps4_nfs_shadows_nv = 1\r\nDXVK_PS4_NEW_KEY=7\r\n"
            "DXVK_PS4_AUTOTEST=1\r\nDXVK_PS4_NFS_PACE=60\r\n");
    snprintf(u, sizeof u, "%s/user/jk4speed-user.ini", d);
    in.user_ini = u;
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "dxvk_ps4_nfs_shadows_nv = 1"));
    CHECK(!has_line(buf, "DXVK_PS4_NFS_SHADOWS_NV=0"));
    CHECK(has_line(buf, "DXVK_PS4_NEW_KEY=7"));
    CHECK(has_line(buf, "DXVK_PS4_AUTOTEST=0"));             /* bench key: the launcher's */
    CHECK(has_line(buf, "DXVK_PS4_NFS_PACE=30"));            /* the pace: SETTINGS */
    char path[400];
    struct stat st;
    snprintf(path, sizeof path, "%s/jk4speed-dev.ini", in.game_dir);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0666);
    in.user_ini = NULL;                                      /* no file: the base as it is */
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_NFS_SHADOWS_NV=0"));
    tmp_rm_rf(d);
}

/* 1.11 VISUALS > Shadow detail: the base's DXVK_PS4_NFS_SHADOW_DETAIL line gets the
 * slider's value; NULL (and the default "2") leave the ini exactly as before. */
static void visuals_shadow_detail_in_ini(void)
{
    char d[256], buf[8192], plain[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    int n0 = read_ini(&in, plain, sizeof plain);
    CHECK(n0 > 0 && has_line(plain, "DXVK_PS4_NFS_SHADOW_DETAIL=2"));
    in.shadow_detail = "2";
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) == n0 && !strcmp(buf, plain));
    in.shadow_detail = "0";
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_NFS_SHADOW_DETAIL=0"));
    CHECK(!has_line(buf, "DXVK_PS4_NFS_SHADOW_DETAIL=2"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_SHADOWS_NV=0"));             /* the other keys as they were */
    CHECK(has_line(buf, "envmap_faces = 1"));
    /* VISUALS > Car reflection update: the base's envmap_faces line; "1" = as before */
    in.shadow_detail = NULL;
    in.envmap_faces = "1";
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) == n0 && !strcmp(buf, plain));
    in.envmap_faces = "6";
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "envmap_faces = 6"));
    CHECK(!has_line(buf, "envmap_faces = 1"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_SHADOW_DETAIL=2"));
    tmp_rm_rf(d);
}

/* CONTROLS remap: the profile's [input] binds and Xidi.ini follow the
 * player's layout; the defaults give today's release files exactly. */
static void controls_compose_profile_and_xidi(void)
{
    char d[256], buf[8192], path[600];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    JkControls c;
    jk_controls_default(&c);
    in.controls = &c;
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    snprintf(path, sizeof path, "%s/nfsmw.profile", in.profiles_dir);
    FILE *f = fopen(path, "rb"); size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
    CHECK(strstr(buf, "[input]\nmode = xinput\nl1 = ctrl\nr1 = shift\nr3 = c\ntouchpad = 2\n") != NULL);
    CHECK_EQ(jk_config_write_xidi(&in, why, sizeof why), 0);
    snprintf(path, sizeof path, "%s/Xidi.ini", in.game_dir);
    char sha[65];
    CHECK(jk_fs_sha256(path, sha, NULL) == 0 && !strcmp(sha, "3c03a044672bff5af4860c9332dc94e2428a8ab983c38ac0d1029f58bbc46d54"));
    jk_controls_assign(&c, JK_ACT_SHIFT_UP, JK_CB_SQUARE);      /* shift up on square; nitro left empty */
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    snprintf(path, sizeof path, "%s/nfsmw.profile", in.profiles_dir);
    f = fopen(path, "rb"); n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
    CHECK(strstr(buf, "\nsquare = shift\n") && !strstr(buf, "\nr1 = shift\n"));
    CHECK_EQ(jk_config_write_xidi(&in, why, sizeof why), 0);
    snprintf(path, sizeof path, "%s/Xidi.ini", in.game_dir);
    f = fopen(path, "rb"); n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
    CHECK(strstr(buf, "ButtonX = Null\n") && strstr(buf, "ButtonRB = Button(12)\n"));   /* R1 free; nitro left empty */
    CHECK(strstr(buf, "StickRightY = Split(Button(5), Null)") != NULL);      /* fixed lines kept */
    CHECK(strstr(buf, "ButtonStart = Button(8)") != NULL);
    tmp_rm_rf(d);
}

static void ini_logs_on_sets_frame_log_only(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.set.logs = 1;
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_FRAME_LOG=1"));
    CHECK(!has_line(buf, "DXVK_PS4_FRAME_LOG=0"));
    CHECK(has_line(buf, "DXVK_PS4_AUTOTEST=0"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_TIMING=0"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_GTPROF=0"));
    CHECK(has_line(buf, "DXVK_PS4_CS_PROFILE=0"));
    CHECK(has_line(buf, "DXVK_PS4_RACE_LOCK=0"));
    CHECK(has_line(buf, "DXVK_PS4_NATIVE_PIN_STAMP=0"));
    tmp_rm_rf(d);
}

static void ini_devtools_sets_autotest(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.devtools = 1;
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_AUTOTEST=1"));
    CHECK(!has_line(buf, "DXVK_PS4_AUTOTEST=0"));
    CHECK(has_line(buf, "DXVK_PS4_FRAME_LOG=0"));
    tmp_rm_rf(d);
}

static void ini_strips_bench_key_from_base(void)
{
    char d[256], files[300], buf[8192];
    tmp_dir(d, sizeof d);
    snprintf(files, sizeof files, "%s/files", d);
    tmp_put(files, "jk4speed.ini.base",
            "[jk4speed]\r\nDXVK_PS4_AUTOTEST=1\r\nKEEP=1\r\ndxvk_ps4_nfs_timing = 1\r\nDXVK_PS4_NFS_PACE=60\r\nDXVK_PS4_NATIVE_PIN_STAMP=1\r\n");
    JkConfigIn in;
    cfg_real(&in, d, files);
    char why[200];
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_ini(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "DXVK_PS4_AUTOTEST=0"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_TIMING=0"));
    CHECK(has_line(buf, "DXVK_PS4_NATIVE_PIN_STAMP=0"));
    CHECK(has_line(buf, "DXVK_PS4_NFS_PACE=30"));
    CHECK(has_line(buf, "KEEP=1"));
    const char *p = buf;
    int ones = 0;
    while ((p = strstr(p, "=1\r\n"))) { ones++; p++; }
    CHECK_EQ(ones, 1);                                           /* only KEEP is left at 1 */
    CHECK(strstr(buf, "=60") == NULL);
    CHECK(strstr(buf, "dxvk_ps4_nfs_timing") == NULL);
    tmp_rm_rf(d);
}

static void ini_without_base_fails_and_writes_nothing(void)
{
    char d[256], why[200] = "";
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, "/nonexistent/files");
    CHECK(jk_config_write_ini(&in, why, sizeof why) < 0);
    CHECK(why[0] != 0);
    CHECK(!tmp_exists(in.game_dir, "jk4speed-dev.ini"));
    tmp_rm_rf(d);
}

static void profile_release_has_no_diag_no_trace(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_profile(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "[debug]"));
    CHECK(has_line(buf, "winedebug = -all"));
    CHECK(has_line(buf, "env = DXVK_PS4_FPS_HUD=0"));
    CHECK(has_line(buf, "env = DXVK_LOG_LEVEL=none"));
    CHECK(has_line(buf, "env = PW_NATIVE_ALERT_UMTX=1"));
    CHECK(has_line(buf, "env = DXVK_PS4_NATIVE_SERVER_CPU=38"));
    CHECK(has_line(buf, "env = DXVK_STATE_CACHE_PATH=/data/Jk4Speed/System/cache"));
    CHECK(strstr(buf, "DXVK_PS4_DIAG") == NULL);
    CHECK(strstr(buf, "DXVK_PS4_LOG_DIR") == NULL);
    CHECK(strstr(buf, "prospero") == NULL);
    CHECK(has_line(buf, "id = nfsmw"));
    CHECK(has_line(buf, "name = NFS Most Wanted"));
    CHECK(has_line(buf, "prefix = default"));
    CHECK(has_line(buf, "r3 = c"));
    CHECK(strchr(buf, '\r') == NULL);
    tmp_rm_rf(d);
}

static void profile_fps_on(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.set.fps_hud = 1;
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_profile(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "env = DXVK_PS4_FPS_HUD=fps"));
    CHECK(!has_line(buf, "env = DXVK_PS4_FPS_HUD=0"));
    tmp_rm_rf(d);
}

static void profile_logs_on(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.set.logs = 1;
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_profile(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "env = DXVK_PS4_LOG_DIR=/data/Jk4Speed/Logs"));
    CHECK(strstr(buf, "DXVK_LOG_LEVEL") == NULL);                 /* info is DXVK's default: no line needed */
    CHECK(has_line(buf, "winedebug = +tid,err+all,+loaddll"));
    CHECK(strstr(buf, "DXVK_PS4_DIAG") == NULL);
    tmp_rm_rf(d);
}

/* The runtime refuses a profile with more than 5 [debug] env lines (pw_game_profile.c
 * PW_GAME_DEBUG_ENV_MAX = 5: "profile refused ... status=malformed", the game never starts).
 * 1.00/1.01 wrote 6 with logs on (console 2026-11-27). Every settings mix stays within 5. */
static void profile_env_lines_within_runtime_limit(void)
{
    for (int mix = 0; mix < 8; mix++) {
        char d[256], buf[8192];
        tmp_dir(d, sizeof d);
        JkConfigIn in;
        cfg_real(&in, d, BASE_DIR);
        in.set.logs = mix & 1;
        in.set.fps_hud = (mix >> 1) & 1;
        in.set.pace = (mix >> 2) & 1 ? JK_PACE_60 : JK_PACE_30;
        char why[200];
        CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
        CHECK(read_profile(&in, buf, sizeof buf) > 0);
        int n = 0;
        for (const char *p = buf; (p = strstr(p, "\nenv = ")) != NULL; p++) n++;
        CHECK(n >= 1 && n <= 5);
        tmp_rm_rf(d);
    }
}

static void profile_game_dir_in_windows_form(void)
{
    char d[256], buf[8192];
    tmp_dir(d, sizeof d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.game_dir = "/data/Jk4Speed/NFSMW";                        /* the profile only names it; nothing is written there */
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_profile(&in, buf, sizeof buf) > 0);
    CHECK(has_line(buf, "executable = Z:\\data\\Jk4Speed\\NFSMW\\speed.exe"));
    CHECK(has_line(buf, "working_directory = Z:\\data\\Jk4Speed\\NFSMW"));
    tmp_rm_rf(d);
}

static void profile_strips_launcher_owned_keys_from_base(void)
{
    char d[256], files[300], buf[8192];
    tmp_dir(d, sizeof d);
    snprintf(files, sizeof files, "%s/files", d);
    tmp_put(files, "nfsmw.profile.base",
            "[application]\nid = nfsmw\nname = NFS Most Wanted\nexecutable = C:\\old.exe\nworking_directory = C:\\old\nprefix = nfsmw\n"
            "[input]\nr3 = c\n[debug]\nwinedebug = +all\nenv = DXVK_PS4_DIAG=2\n");
    JkConfigIn in;
    cfg_real(&in, d, files);
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_profile(&in, buf, sizeof buf) > 0);
    CHECK(strstr(buf, "old") == NULL);
    CHECK(strstr(buf, "DXVK_PS4_DIAG") == NULL);
    CHECK(strstr(buf, "+all\n") == NULL);
    CHECK(has_line(buf, "winedebug = -all"));
    CHECK(has_line(buf, "prefix = default"));
    CHECK(has_line(buf, "r3 = c"));
    tmp_rm_rf(d);
}

static void profile_key_nfs(void)
{
    char d[256], files[300], buf[8192];
    tmp_dir(d, sizeof d);
    snprintf(files, sizeof files, "%s/files", d);
    tmp_put(files, "nfsmw.profile.base",
            "[application]\nid = nfsmw\nname = NFS Most Wanted\nexecutable = C:\\old.exe\nprefix = nfsmw\n[debug]\nwinedebug = +all\n");
    JkConfigIn in;
    cfg_real(&in, d, files);
    static char game[300];
    snprintf(game, sizeof game, "/data/Jk4Speed/NFS");
    in.game_dir = game;
    in.profile_key = "nfs";
    char why[200];
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(!tmp_exists(in.profiles_dir, "nfsmw.profile"));
    CHECK(read_into(in.profiles_dir, "nfs.profile", buf, sizeof buf) > 0);
    CHECK(has_line(buf, "id = nfs"));
    CHECK(!has_line(buf, "id = nfsmw"));
    CHECK(has_line(buf, "executable = Z:\\data\\Jk4Speed\\NFS\\speed.exe"));
    in.profile_key = NULL;                              /* the default key is unchanged */
    CHECK_EQ(jk_config_write_profile(&in, why, sizeof why), 0);
    CHECK(read_into(in.profiles_dir, "nfsmw.profile", buf, sizeof buf) > 0);
    CHECK(has_line(buf, "id = nfsmw"));
    tmp_rm_rf(d);
}

/* Every occurrence of from in buf becomes to (buf is big enough). */
static void repl(char *buf, size_t cap, const char *from, const char *to)
{
    char *out = malloc(cap);
    size_t n = 0, fl = strlen(from);
    for (const char *p = buf; *p && n + 1 < cap;) {
        if (!strncmp(p, from, fl)) { n += (size_t)snprintf(out + n, cap - n, "%s", to); p += fl; }
        else out[n++] = *p++;
    }
    out[n] = 0;
    snprintf(buf, cap, "%s", out);
    free(out);
}

/* 1.1 (owner): the NFS slot gets every FPS and stability setting exactly as the NFSMW slot. Composed
 * with the same settings and controls, the ini, the profile and Xidi.ini differ only in the game's
 * name, folder and cache paths. */
static void slots_compose_equivalent(void)
{
    char d[256], files[300];
    tmp_dir(d, sizeof d);
    snprintf(files, sizeof files, "%s/files", d);
    mkdir(files, 0755);
    static const char *const names[] = { "jk4speed.ini.base", "nfsmw.profile.base", "Xidi.ini" };
    for (int i = 0; i < 3; i++) {
        char src[400], dst[400];
        snprintf(src, sizeof src, BASE_DIR "/%s", names[i]);
        snprintf(dst, sizeof dst, "%s/%s", files, names[i]);
        CHECK_EQ(jk_fs_copy_checked(src, dst, NULL), 0);
    }
    char prof[300], be[300], nfs[300], becache[300], nfscache[300], logs[300];
    snprintf(prof, sizeof prof, "%s/profiles", d);
    snprintf(be, sizeof be, "%s/Jk4Speed/NFSMW", d);
    snprintf(nfs, sizeof nfs, "%s/Jk4Speed/NFS", d);
    snprintf(becache, sizeof becache, "%s/Jk4Speed/System/cache", d);
    snprintf(nfscache, sizeof nfscache, "%s/Jk4Speed/System/nfs/cache", d);
    snprintf(logs, sizeof logs, "%s/Jk4Speed/Logs", d);
    jk_fs_mkdir_p(be); jk_fs_mkdir_p(nfs); jk_fs_mkdir_p(prof);
    JkControls ctl[2];
    jk_controls_default(&ctl[0]);
    jk_controls_default(&ctl[1]);
    jk_controls_assign(&ctl[1], JK_ACT_CHALLENGE, JK_CB_TOUCHPAD);
    jk_controls_assign(&ctl[1], JK_ACT_SHIFT_UP, JK_CB_SQUARE);
    int combos = 0;
    for (int c = 0; c < 2; c++)
    for (int pace = 0; pace < 2; pace++)
    for (int hud = 0; hud < 2; hud++)
    for (int lg = 0; lg < 2; lg++) {
        JkConfigIn a, b;
        memset(&a, 0, sizeof a);
        a.files_dir = files; a.profiles_dir = prof; a.logs_dir = logs;
        a.set = (JkSettings){ pace ? JK_PACE_60 : JK_PACE_30, hud, lg };
        a.controls = &ctl[c];
        b = a;
        a.game_dir = be; a.cache_dir = becache; a.profile_key = "nfsmw";
        b.game_dir = nfs; b.cache_dir = nfscache; b.profile_key = "nfs";
        char why[200];
        CHECK_EQ(jk_config_write_ini(&a, why, sizeof why), 0);
        CHECK_EQ(jk_config_write_ini(&b, why, sizeof why), 0);
        CHECK_EQ(jk_config_write_profile(&a, why, sizeof why), 0);
        CHECK_EQ(jk_config_write_profile(&b, why, sizeof why), 0);
        CHECK_EQ(jk_config_write_xidi(&a, why, sizeof why), 0);
        CHECK_EQ(jk_config_write_xidi(&b, why, sizeof why), 0);
        static char ia[16384], ib[16384], pa[16384], pb[16384], xa[16384], xb[16384];
        CHECK(read_into(be, "jk4speed-dev.ini", ia, sizeof ia) > 0 && read_into(nfs, "jk4speed-dev.ini", ib, sizeof ib) > 0);
        CHECK(read_into(prof, "nfsmw.profile", pa, sizeof pa) > 0 && read_into(prof, "nfs.profile", pb, sizeof pb) > 0);
        CHECK(read_into(be, "Xidi.ini", xa, sizeof xa) > 0 && read_into(nfs, "Xidi.ini", xb, sizeof xb) > 0);
        repl(pb, sizeof pb, "id = nfs\n", "id = nfsmw\n");              /* only these slot-specific tokens */
        repl(pb, sizeof pb, "Jk4Speed/NFS", "Jk4Speed/NFSMW");
        repl(pb, sizeof pb, "Jk4Speed\\NFS", "Jk4Speed\\NFSMW");
        repl(pb, sizeof pb, "System/nfs/cache", "System/cache");
        repl(ib, sizeof ib, "Jk4Speed/NFS", "Jk4Speed/NFSMW");
        repl(ib, sizeof ib, "Jk4Speed\\NFS", "Jk4Speed\\NFSMW");
        CHECK(!strcmp(ia, ib));
        CHECK(!strcmp(pa, pb));
        CHECK(!strcmp(xa, xb));
        /* the reference lines are there in both */
        CHECK(strstr(pb, "env = PW_NATIVE_ALERT_UMTX=1\n") && strstr(pb, "env = DXVK_PS4_NATIVE_SERVER_CPU=38\n"));
        CHECK(strstr(pb, "desktop = 1920x1080\n") && strstr(pb, "fast_clock = true\n") && strstr(pb, "cpu = native\n"));
        CHECK(strstr(ib, pace ? "DXVK_PS4_NFS_PACE=60" : "DXVK_PS4_NFS_PACE=30") != NULL);
        if (c == 0) CHECK(strstr(pb, "l1 = ctrl\n") && strstr(pb, "r1 = shift\n") && strstr(pb, "r3 = c\n") && strstr(pb, "touchpad = 2\n"));
        else CHECK(strstr(pb, "square = shift\n") != NULL);
        combos++;
    }
    CHECK_EQ(combos, 16);
    tmp_rm_rf(d);
}

static void logs_clear_only_logs_dir(void)
{
    char d[256], logs[300], buf[16];
    tmp_dir(d, sizeof d);
    snprintf(logs, sizeof logs, "%s/Logs", d);
    tmp_put(logs, "session-1.log", "x");
    tmp_put(logs, "session-2.log", "y");
    tmp_put(logs, "support/2026-11-17_2105/MANIFEST.txt", "m");
    tmp_put(logs, "support/2026-11-17_2105/deep/a.log", "a");
    tmp_put(d, "System/state/launcher.log", "keep");
    tmp_put(d, "Saves/save.dat", "keep");
    tmp_put(d, "NFSMW/speed.exe", "keep");
    CHECK_EQ(jk_logs_clear(logs), 4);
    CHECK(!tmp_exists(logs, "session-1.log"));
    CHECK(!tmp_exists(logs, "session-2.log"));
    CHECK(!tmp_exists(logs, "support/2026-11-17_2105/MANIFEST.txt"));
    CHECK(!tmp_exists(logs, "support/2026-11-17_2105/deep/a.log"));
    CHECK(tmp_exists(logs, "support"));                          /* the folders Logs and Logs/support stay */
    CHECK(tmp_get(d, "System/state/launcher.log", buf, sizeof buf) > 0);
    CHECK(tmp_get(d, "Saves/save.dat", buf, sizeof buf) > 0);
    CHECK(tmp_get(d, "NFSMW/speed.exe", buf, sizeof buf) > 0);
    CHECK_EQ(jk_logs_clear(logs), 0);                            /* nothing left */
    CHECK_EQ(jk_logs_clear("/nonexistent/Logs"), 0);
    CHECK_EQ(jk_logs_clear(NULL), -1);
    tmp_rm_rf(d);
}

/* Lead ruling (Task 8): a jk4speed-dev.ini that was there before us (the
 * bench's) is copied once to <backup_dir>/jk4speed-dev.ini before the first
 * compose; a file we composed ourselves is never backed up, and an existing
 * backup is never replaced. */
static void foreign_ini_backed_up_once(void)
{
    char d[256], buf[8192], backup[300];
    tmp_dir(d, sizeof d);
    snprintf(backup, sizeof backup, "%s/System/backup", d);
    JkConfigIn in;
    cfg_real(&in, d, BASE_DIR);
    in.backup_dir = backup;
    char why[200] = "";
    tmp_put(in.game_dir, "jk4speed-dev.ini", "[jk4speed]\r\nDXVK_PS4_DIAG=2\r\n");   /* the bench's own ini */
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_into(backup, "jk4speed-dev.ini", buf, sizeof buf) > 0);
    CHECK(!strcmp(buf, "[jk4speed]\r\nDXVK_PS4_DIAG=2\r\n"));
    CHECK(read_ini(&in, buf, sizeof buf) > 0 && has_line(buf, "DXVK_PS4_NFS_PACE=30"));
    /* a second compose with other settings: the backup keeps the bench's bytes */
    in.set.pace = JK_PACE_60;
    in.set.logs = 1;
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_into(backup, "jk4speed-dev.ini", buf, sizeof buf) > 0);
    CHECK(!strcmp(buf, "[jk4speed]\r\nDXVK_PS4_DIAG=2\r\n"));
    tmp_rm_rf(d);

    /* no ini before us: our own composes never make a backup */
    tmp_dir(d, sizeof d);
    snprintf(backup, sizeof backup, "%s/System/backup", d);
    cfg_real(&in, d, BASE_DIR);
    in.backup_dir = backup;
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    in.set.pace = JK_PACE_60;
    in.devtools = 1;
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    in.set.pace = JK_PACE_30;
    in.devtools = 0;
    CHECK_EQ(jk_config_write_ini(&in, why, sizeof why), 0);
    CHECK(read_into(backup, "jk4speed-dev.ini", buf, sizeof buf) < 0);
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(settings_defaults_off);
    RUN(settings_roundtrip);
    RUN(release_ini_has_every_bench_key_zero);
    RUN(ini_pace_60);
    RUN(ini_logs_on_sets_frame_log_only);
    RUN(ini_devtools_sets_autotest);
    RUN(ini_strips_bench_key_from_base);
    RUN(ini_without_base_fails_and_writes_nothing);
    RUN(foreign_ini_backed_up_once);
    RUN(user_ini_overrides);
    RUN(visuals_shadow_detail_in_ini);
    RUN(controls_compose_profile_and_xidi);
    RUN(profile_release_has_no_diag_no_trace);
    RUN(profile_fps_on);
    RUN(profile_logs_on);
    RUN(profile_env_lines_within_runtime_limit);
    RUN(profile_game_dir_in_windows_form);
    RUN(profile_strips_launcher_owned_keys_from_base);
    RUN(profile_key_nfs);
    RUN(slots_compose_equivalent);
    RUN(logs_clear_only_logs_dir);
    return jk_test_report("test_config");
}
