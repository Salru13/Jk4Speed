/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The release config. See jk_config.h. */
#include "jk_config.h"
#include "jk_fs.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BASE_MAX (64 * 1024)
#define OUT_MAX (96 * 1024)
#define PACE_KEY "DXVK_PS4_NFS_PACE"

const char *const JK_BENCH_KEYS[] = {
    "DXVK_PS4_AUTOTEST", "DXVK_PS4_FRAME_LOG", "DXVK_PS4_NFS_TIMING", "DXVK_PS4_NFS_GTPROF",
    "DXVK_PS4_CS_PROFILE", "DXVK_PS4_RACE_LOCK", "DXVK_PS4_NATIVE_PIN_STAMP", NULL
};

static void say(char *why, size_t cap, const char *msg)
{
    if (why && cap) snprintf(why, cap, "%s", msg);
}

/* ------------------------------------------------------------------ */
/* Settings                                                           */
/* ------------------------------------------------------------------ */

static int settings_path(const char *state_dir, char *out, size_t cap)
{
    return state_dir ? jk_fs_join(out, cap, state_dir, "launcher.ini") : -1;
}

int jk_settings_load(const char *state_dir, JkSettings *out)
{
    if (!out) return -1;
    out->pace = JK_PACE_30;
    out->fps_hud = 0;
    out->logs = 0;
    char path[1024];
    if (settings_path(state_dir, path, sizeof path) < 0) return -1;
    char *text = jk_fs_read(path, 4096, NULL);
    if (!text) return 0;
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        char *e = k + strlen(k);
        while (e > k && isspace((unsigned char)e[-1])) *--e = 0;
        while (*v == ' ' || *v == '\t') v++;
        e = v + strlen(v);
        while (e > v && isspace((unsigned char)e[-1])) *--e = 0;
        if (!strcmp(k, "pace")) {
            if (!strcmp(v, "60")) out->pace = JK_PACE_60;
            else if (!strcmp(v, "30")) out->pace = JK_PACE_30;
        } else if (!strcmp(k, "fps_hud") || !strcmp(k, "logs")) {
            int *dst = k[0] == 'f' ? &out->fps_hud : &out->logs;
            if (!strcmp(v, "1")) *dst = 1;
            else if (!strcmp(v, "0")) *dst = 0;
        }
    }
    free(text);
    return 0;
}

int jk_active_load(const char *state_dir)
{
    char path[1024];
    if (settings_path(state_dir, path, sizeof path) < 0) return -1;
    char *text = jk_fs_read(path, 4096, NULL);
    if (!text) return -1;
    int r = -1;
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "active", 6)) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (v[0] == '0' || v[0] == '1') r = v[0] - '0';
    }
    free(text);
    return r;
}

int jk_settings_save(const char *state_dir, const JkSettings *s)
{
    char path[1024], text[96];
    if (!s || settings_path(state_dir, path, sizeof path) < 0 || jk_fs_mkdir_p(state_dir) < 0) return -1;
    int n = snprintf(text, sizeof text, "pace=%s\nfps_hud=%d\nlogs=%d\n", s->pace == JK_PACE_60 ? "60" : "30",
                     s->fps_hud ? 1 : 0, s->logs ? 1 : 0);
    int act = jk_active_load(state_dir);                    /* 1.1: the remembered game side stays */
    if (act >= 0) n += snprintf(text + n, sizeof text - (size_t)n, "active=%d\n", act);
    return jk_fs_write_atomic(path, text, (size_t)n);
}

int jk_active_save(const char *state_dir, int active)
{
    char path[1024];
    if (settings_path(state_dir, path, sizeof path) < 0 || jk_fs_mkdir_p(state_dir) < 0) return -1;
    char *old = jk_fs_read(path, 4096, NULL);
    char *out = malloc((old ? strlen(old) : 0) + 32);
    if (!out) { free(old); return -1; }
    size_t n = 0;
    for (char *line = old, *next; line && *line; line = next) {      /* every line but an old active= */
        next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        next = next ? next + 1 : NULL;
        const char *k = line;
        while (*k == ' ' || *k == '\t') k++;
        if (!strncmp(k, "active", 6) && strchr(line, '=') && (size_t)(strchr(line, '=') - line) < len) continue;
        memcpy(out + n, line, len);
        n += len;
        out[n++] = '\n';
    }
    n += (size_t)sprintf(out + n, "active=%d\n", active ? 1 : 0);
    int r = jk_fs_write_atomic(path, out, n);
    free(out);
    free(old);
    return r;
}

/* ------------------------------------------------------------------ */
/* Composing                                                          */
/* ------------------------------------------------------------------ */

typedef struct Buf { char *p; size_t n, cap; int bad; } Buf;

static void put(Buf *b, const char *s, const char *eol)
{
    size_t a = strlen(s), c = strlen(eol);
    if (b->bad || b->n + a + c + 1 > b->cap) { b->bad = 1; return; }
    memcpy(b->p + b->n, s, a); b->n += a;
    memcpy(b->p + b->n, eol, c); b->n += c;
}

/* Appends text as it is (it carries its own line ends). */
static void put_raw(Buf *b, const char *t)
{
    size_t a = strlen(t);
    if (b->bad || b->n + a + 1 > b->cap) { b->bad = 1; return; }
    memcpy(b->p + b->n, t, a);
    b->n += a;
}

/* The base file's text, or NULL with a reason. */
static char *read_base(const char *files_dir, const char *name, char *why, size_t cap)
{
    char path[1024];
    if (!files_dir || jk_fs_join(path, sizeof path, files_dir, name) < 0) { say(why, cap, "no package folder"); return NULL; }
    char *t = jk_fs_read(path, BASE_MAX, NULL);
    if (!t) snprintf(why, cap, "the package has no %s", name);
    return t;
}

/* Trims a line in place (both ends) and returns it. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* Key of "key = value" (the text before '='), trimmed, in a scratch copy; NULL when the line has none. */
static const char *key_of(const char *line, char *scratch, size_t cap)
{
    const char *eq = strchr(line, '=');
    if (!eq || (size_t)(eq - line) >= cap) return NULL;
    memcpy(scratch, line, (size_t)(eq - line));
    scratch[eq - line] = 0;
    return trim(scratch);
}

static int is_bench_key(const char *k)
{
    if (!strcasecmp(k, PACE_KEY)) return 1;
    for (int i = 0; JK_BENCH_KEYS[i]; i++)
        if (!strcasecmp(k, JK_BENCH_KEYS[i])) return 1;
    return 0;
}

static void ini_keys(Buf *b, const JkConfigIn *in)
{
    static const char CRLF[] = "\r\n";
    char line[96];
    snprintf(line, sizeof line, PACE_KEY "=%s", in->set.pace == JK_PACE_60 ? "60" : "30");
    put(b, line, CRLF);
    for (int i = 0; JK_BENCH_KEYS[i]; i++) {
        int on = (!strcmp(JK_BENCH_KEYS[i], "DXVK_PS4_AUTOTEST") && in->devtools)
              || (!strcmp(JK_BENCH_KEYS[i], "DXVK_PS4_FRAME_LOG") && in->set.logs);
        snprintf(line, sizeof line, "%s=%d", JK_BENCH_KEYS[i], on);
        put(b, line, CRLF);
    }
}

/* r5: the user file's "KEY=VALUE" lines (no bench keys, no pace). */
#define USER_MAX 64
typedef struct UserKv { char key[96]; char line[256]; int used; } UserKv;
static int load_user(const char *path, UserKv *kv)
{
    int n = 0;
    char *t = path ? jk_fs_read(path, 64 * 1024, NULL) : NULL;
    if (!t) return 0;
    for (char *line = t, *next; line && *line && n < USER_MAX; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = trim(line), scratch[128];
        if (!*s || *s == ';' || *s == '#' || *s == '[') continue;
        const char *k = key_of(s, scratch, sizeof scratch);
        if (!k || !*k || is_bench_key(k) || strlen(s) >= sizeof kv[n].line) continue;
        snprintf(kv[n].key, sizeof kv[n].key, "%s", k);
        snprintf(kv[n].line, sizeof kv[n].line, "%s", s);
        kv[n].used = 0;
        n++;
    }
    free(t);
    return n;
}

/* The composed ini for in's settings (malloc'd, b->bad when it overflowed). 0 ok. */
static int build_ini(const JkConfigIn *in, Buf *out, char *why, size_t cap)
{
    char *base = read_base(in->files_dir, "jk4speed.ini.base", why, cap);
    if (!base) return -1;
    static UserKv user[USER_MAX];
    int nuser = load_user(in->user_ini, user);
    Buf b = { malloc(OUT_MAX), 0, OUT_MAX, 0 };
    if (!b.p) { free(base); say(why, cap, "out of memory"); return -1; }
    int header = 0;
    for (char *line = base, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = trim(line), scratch[128];
        if (!*s) continue;
        if (!header) {                            /* our keys go right under the first header */
            if (*s != '[') put(&b, "[jk4speed]", "\r\n");
            else { put(&b, s, "\r\n"); s = NULL; }
            ini_keys(&b, in);
            header = 1;
            if (!s) continue;
        }
        const char *k = key_of(s, scratch, sizeof scratch);
        if (k && is_bench_key(k)) continue;
        int over = -1;
        for (int i = 0; k && i < nuser; i++) if (!strcasecmp(k, user[i].key)) over = i;
        if (over >= 0) { put(&b, user[over].line, "\r\n"); user[over].used = 1; continue; }
        put(&b, s, "\r\n");
    }
    if (!header) { put(&b, "[jk4speed]", "\r\n"); ini_keys(&b, in); }
    for (int i = 0; i < nuser; i++) if (!user[i].used) put(&b, user[i].line, "\r\n");   /* new keys */
    free(base);
    *out = b;
    return 0;
}

/* 1 when text is what we compose for some pace / logs / devtools choice. */
static int is_our_ini(const JkConfigIn *in, const char *text, size_t n)
{
    for (int k = 0; k < 8; k++) {
        JkConfigIn v = *in;
        char ignored[8];
        Buf b;
        v.set.pace = k & 1 ? JK_PACE_60 : JK_PACE_30;
        v.set.logs = (k >> 1) & 1;
        v.devtools = (k >> 2) & 1;
        if (build_ini(&v, &b, ignored, sizeof ignored) < 0) return 0;
        int same = !b.bad && b.n == n && !memcmp(b.p, text, n);
        free(b.p);
        if (same) return 1;
    }
    return 0;
}

/* ---- composed.txt: the hash of what the launcher last wrote ---- */

static int composed_path(const char *state_dir, char *out, size_t cap)
{
    return state_dir ? jk_fs_join(out, cap, state_dir, "composed.txt") : -1;
}

int jk_config_composed_sha(const char *state_dir, const char *which, char out[65])
{
    char path[1024];
    out[0] = 0;
    if (!which || composed_path(state_dir, path, sizeof path) < 0) return -1;
    char *text = jk_fs_read(path, 4096, NULL);
    if (!text) return -1;
    size_t wl = strlen(which);
    int rc = -1;
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strncmp(line, which, wl) && line[wl] == ' ' && sscanf(line + wl + 1, "%64s", out) == 1 && strlen(out) == 64) rc = 0;
    }
    free(text);
    if (rc < 0) out[0] = 0;
    return rc;
}

void jk_config_composed_forget(const char *state_dir)
{
    char path[1024];
    if (composed_path(state_dir, path, sizeof path) == 0) jk_fs_remove(path);
}

/* Notes the hash of the file just written (the other line is kept). */
static void note_composed(const char *state_dir, const char *which, const char *file)
{
    char path[1024], sha[65], other[65], text[256];
    if (!state_dir || composed_path(state_dir, path, sizeof path) < 0 || jk_fs_sha256(file, sha, NULL) < 0) return;
    const char *o = !strcmp(which, "ini") ? "profile" : "ini";
    int n = jk_config_composed_sha(state_dir, o, other) == 0
            ? snprintf(text, sizeof text, "%s %s\n%s %s\n", which, sha, o, other)
            : snprintf(text, sizeof text, "%s %s\n", which, sha);
    if (jk_fs_mkdir_p(state_dir) == 0) jk_fs_write_atomic(path, text, (size_t)n);
}

/* Lead ruling: a jk4speed-dev.ini that is not ours goes to <backup_dir>/
 * before the first compose over it; when the first backup is there, a later
 * one goes to <backup_dir>/kept/jk4speed-dev.ini (.2, .3 ...). Never
 * overwritten. 0 ok (or nothing to do). */
static int backup_foreign_ini(const JkConfigIn *in, const char *path, char *why, size_t cap)
{
    char dst[1200], last[65], cur[65];
    if (!in->backup_dir || !jk_fs_exists(path)) return 0;
    if (jk_fs_sha256(path, cur, NULL) == 0 && jk_config_composed_sha(in->state_dir, "ini", last) == 0 && !strcmp(cur, last))
        return 0;                                         /* as we last wrote it */
    size_t n = 0;
    char *old = jk_fs_read(path, OUT_MAX, &n);
    int ours = old && is_our_ini(in, old, n);
    free(old);
    if (ours) return 0;
    if (jk_fs_join(dst, sizeof dst, in->backup_dir, "jk4speed-dev.ini") < 0) { say(why, cap, "backup path too long"); return -1; }
    if (jk_fs_exists(dst)) {                              /* the first backup stays; this one is kept aside */
        char kept[1024], alt[1100];
        if (jk_fs_join(kept, sizeof kept, in->backup_dir, "kept/jk4speed-dev.ini") < 0) { say(why, cap, "backup path too long"); return -1; }
        dst[0] = 0;
        for (int k = 1; k < 1000 && !dst[0]; k++) {
            if (k == 1) snprintf(alt, sizeof alt, "%s", kept);
            else snprintf(alt, sizeof alt, "%s.%d", kept, k);
            if (!jk_fs_exists(alt)) snprintf(dst, sizeof dst, "%s", alt);
        }
        if (!dst[0] || jk_fs_mkdir_parent(dst) < 0) { say(why, cap, "could not back up the old jk4speed-dev.ini"); return -1; }
    }
    if (jk_fs_copy_checked(path, dst, NULL) < 0) { say(why, cap, "could not back up the old jk4speed-dev.ini"); return -1; }
    return 0;
}

int jk_config_write_ini(const JkConfigIn *in, char *why, size_t cap)
{
    if (!in || !in->game_dir) { say(why, cap, "no game folder"); return -1; }
    Buf b;
    if (build_ini(in, &b, why, cap) < 0) return -1;
    char path[1024];
    int r = -1;
    if (b.bad) say(why, cap, "the ini base is too large");
    else if (jk_fs_join(path, sizeof path, in->game_dir, "jk4speed-dev.ini") < 0) say(why, cap, "game folder path too long");
    else if (backup_foreign_ini(in, path, why, cap) < 0) { /* why is set; the old file stays */ }
    else if (jk_fs_write_atomic(path, b.p, b.n) < 0) say(why, cap, "could not write jk4speed-dev.ini");
    else { r = 0; chmod(path, 0666); note_composed(in->state_dir, "ini", path); }   /* r5: FTP can replace it */
    free(b.p);
    return r;
}

/* /data/Jk4Speed/NFSMW -> Z:\data\Jk4Speed\NFSMW */
static void windows_path(char *out, size_t cap, const char *dir, const char *tail)
{
    size_t n = (size_t)snprintf(out, cap, "Z:%s%s%s", dir, tail ? "/" : "", tail ? tail : "");
    for (size_t i = 0; i < n && i < cap; i++)
        if (out[i] == '/') out[i] = '\\';
}

static void profile_application(Buf *b, const JkConfigIn *in)
{
    char line[1200], win[1100];
    windows_path(win, sizeof win, in->game_dir, "speed.exe");
    snprintf(line, sizeof line, "executable = %s", win);
    put(b, line, "\n");
    windows_path(win, sizeof win, in->game_dir, NULL);
    snprintf(line, sizeof line, "working_directory = %s", win);
    put(b, line, "\n");
    put(b, "prefix = default", "\n");
}

static void profile_debug(Buf *b, const JkConfigIn *in)
{
    char line[1100];
    put(b, "[debug]", "\n");
    put(b, in->set.logs ? "winedebug = +tid,err+all,+loaddll" : "winedebug = -all", "\n");
    put(b, "env = PW_NATIVE_ALERT_UMTX=1", "\n");
    put(b, in->set.fps_hud ? "env = DXVK_PS4_FPS_HUD=fps" : "env = DXVK_PS4_FPS_HUD=0", "\n");
    /* the runtime takes at most 5 env lines (pw_game_profile.c PW_GAME_DEBUG_ENV_MAX; a sixth refuses
     * the whole profile and the game never starts): with logs on DXVK keeps its default level, info */
    if (!in->set.logs) put(b, "env = DXVK_LOG_LEVEL=none", "\n");
    snprintf(line, sizeof line, "env = DXVK_STATE_CACHE_PATH=%s", in->cache_dir);
    put(b, line, "\n");
    put(b, "env = DXVK_PS4_NATIVE_SERVER_CPU=38", "\n");
    if (in->set.logs) {
        snprintf(line, sizeof line, "env = DXVK_PS4_LOG_DIR=%s", in->logs_dir);
        put(b, line, "\n");
    }
}

static const char *profile_key_of(const JkConfigIn *in)
{
    return in->profile_key && in->profile_key[0] ? in->profile_key : "nfsmw";
}

int jk_config_write_profile(const JkConfigIn *in, char *why, size_t cap)
{
    if (!in || !in->game_dir || !in->profiles_dir || !in->cache_dir || (in->set.logs && !in->logs_dir)) {
        say(why, cap, "a folder is not set");
        return -1;
    }
    char *base = read_base(in->files_dir, "nfsmw.profile.base", why, cap);
    if (!base) return -1;
    Buf b = { malloc(OUT_MAX), 0, OUT_MAX, 0 };
    if (!b.p) { free(base); say(why, cap, "out of memory"); return -1; }
    int in_app = 0, in_debug = 0, app_done = 0, in_input = 0, binds_done = 0;
    JkControls dc;
    const JkControls *ctl = in->controls;
    if (!ctl) { jk_controls_default(&dc); ctl = &dc; }
    char binds[256];
    if (jk_controls_input_lines(ctl, binds, sizeof binds) < 0) binds[0] = 0;
    for (char *line = base, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = trim(line), scratch[128];
        if (!*s) continue;
        if (*s == '[') {
            if (in_input && !binds_done) { put_raw(&b, binds); binds_done = 1; }   /* [input] without a mode line */
            if (in_app && !app_done) { profile_application(&b, in); app_done = 1; }
            in_app = !strncasecmp(s, "[application]", 13);
            in_debug = !strncasecmp(s, "[debug]", 7);
            in_input = !strncasecmp(s, "[input]", 7);
            if (!in_debug) put(&b, s, "\n");
            continue;
        }
        if (in_debug) continue;                   /* the writer owns [debug] */
        const char *k = key_of(s, scratch, sizeof scratch);
        if (in_input && k && strcasecmp(k, "mode")) continue;   /* the binds come from CONTROLS */
        if (in_app && k && !strcasecmp(k, "id")) {
            char idl[128];
            snprintf(idl, sizeof idl, "id = %s", profile_key_of(in));
            put(&b, idl, "\n");
            continue;
        }
        if (in_app && k && (!strcasecmp(k, "executable") || !strcasecmp(k, "working_directory") || !strcasecmp(k, "prefix")))
            continue;
        put(&b, s, "\n");
        if (in_input && k && !strcasecmp(k, "mode") && !binds_done) { put_raw(&b, binds); binds_done = 1; }   /* after "mode = xinput" */
        if (in_app && !app_done && k && !strcasecmp(k, "name")) { profile_application(&b, in); app_done = 1; }
    }
    if (in_input && !binds_done) put_raw(&b, binds);
    if (in_app && !app_done) profile_application(&b, in);
    profile_debug(&b, in);
    free(base);
    char path[1024], pname[96];
    int r = -1;
    if (b.bad) say(why, cap, "the profile base is too large");
    else if (snprintf(pname, sizeof pname, "%s.profile", profile_key_of(in)) >= (int)sizeof pname
             || jk_fs_join(path, sizeof path, in->profiles_dir, pname) < 0) say(why, cap, "profiles folder path too long");
    else if (jk_fs_mkdir_p(in->profiles_dir) < 0) say(why, cap, "could not make the profiles folder");
    else if (jk_fs_write_atomic(path, b.p, b.n) < 0) say(why, cap, "could not write the profile");
    else { r = 0; chmod(path, 0666); note_composed(in->state_dir, "profile", path); }
    free(b.p);
    return r;
}

/* ------------------------------------------------------------------ */
/* Xidi.ini (SETTINGS > CONTROLS)                                     */
/* ------------------------------------------------------------------ */

int jk_config_write_xidi(const JkConfigIn *in, char *why, size_t cap)
{
    if (!in || !in->game_dir) { say(why, cap, "no game folder"); return -1; }
    char *base = read_base(in->files_dir, "Xidi.ini", why, cap);
    if (!base) return -1;
    JkControls dc;
    const JkControls *ctl = in->controls;
    if (!ctl) { jk_controls_default(&dc); ctl = &dc; }
    char lines[1024];
    if (jk_controls_xidi_lines(ctl, lines, sizeof lines) < 0) { free(base); say(why, cap, "controls"); return -1; }
    Buf b = { malloc(OUT_MAX), 0, OUT_MAX, 0 };
    if (!b.p) { free(base); say(why, cap, "out of memory"); return -1; }
    for (char *line = base, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!next && !*line) break;                       /* the end of the file */
        char scratch[128];
        const char *k = key_of(line, scratch, sizeof scratch);
        const char *hit = NULL;
        if (k && *k) {                                     /* one of the 10 remappable buttons: ours */
            char want[140];
            snprintf(want, sizeof want, "%s = ", k);
            for (const char *l = lines; *l; l = strchr(l, '\n') + 1)
                if (!strncmp(l, want, strlen(want))) { hit = l; break; }
        }
        if (hit) {
            const char *e = strchr(hit, '\n');
            char one[160];
            snprintf(one, sizeof one, "%.*s", (int)(e - hit), hit);
            put(&b, one, "\n");
        } else {
            put(&b, line, "\n");
        }
    }
    free(base);
    char path[1024];
    int r = -1;
    if (b.bad) say(why, cap, "Xidi.ini base is too large");
    else if (jk_fs_join(path, sizeof path, in->game_dir, "Xidi.ini") < 0) say(why, cap, "game folder path too long");
    else if (jk_fs_write_atomic(path, b.p, b.n) < 0) say(why, cap, "could not write Xidi.ini");
    else { r = 0; chmod(path, 0666); }
    free(b.p);
    return r;
}

/* ------------------------------------------------------------------ */
/* Clear logs                                                         */
/* ------------------------------------------------------------------ */

/* Files directly in dir, and (recurse) everything below it; folders below
 * dir go once empty. Links are removed, never followed. */
static int clear_dir(const char *dir, int recurse)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1024];
        struct stat st;
        if (jk_fs_join(p, sizeof p, dir, e->d_name) < 0 || lstat(p, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recurse) { n += clear_dir(p, 1); rmdir(p); }
        } else if (unlink(p) == 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

int jk_logs_clear(const char *logs_dir)
{
    if (!logs_dir || !*logs_dir) return -1;
    char support[1024];
    int n = clear_dir(logs_dir, 0);
    if (jk_fs_join(support, sizeof support, logs_dir, "support") == 0) n += clear_dir(support, 1);
    return n;
}
