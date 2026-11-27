/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "jk_prefix.h"
#include "jk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define REG_MAX (64u * 1024 * 1024)
#define EXPLORER "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\"

static const char *const SECTIONS[2] = { EXPLORER "Shell Folders]", EXPLORER "User Shell Folders]" };
static const char KEY[] = "\"Personal\"=";

static int fail(char *why, size_t cap, const char *msg)
{
    if (why && cap) snprintf(why, cap, "%s", msg);
    return -1;
}

/* Which section a header line opens: 0, 1, or -1 for any other. */
static int section_of(const char *line, size_t n)
{
    for (int i = 0; i < 2; i++) {
        size_t k = strlen(SECTIONS[i]);
        if (n >= k && !memcmp(line, SECTIONS[i], k) && (n == k || line[k] == ' ')) return i;
    }
    return -1;
}

int jk_prefix_set_personal(const char *prefix_dir, const char *win_path, char *why, size_t cap)
{
    if (why && cap) why[0] = 0;
    if (!prefix_dir || !win_path || !win_path[0]) return fail(why, cap, "no prefix folder or saves path");
    char reg[600];
    if (jk_fs_join(reg, sizeof reg, prefix_dir, "user.reg") != 0) return fail(why, cap, "the prefix path is too long");
    size_t len = 0;
    char *data = jk_fs_read(reg, REG_MAX, &len);
    if (!data) return fail(why, cap, "user.reg in the prefix is missing or unreadable");

    /* the new value: backslashes doubled, as in the registry file */
    size_t wl = strlen(win_path);
    char *value = malloc(wl * 2 + 1);
    if (!value) { free(data); return fail(why, cap, "out of memory"); }
    size_t vl = 0;
    for (size_t i = 0; i < wl; i++) {
        if (win_path[i] == '"' || win_path[i] == '\n') { free(value); free(data); return fail(why, cap, "the saves path has an invalid character"); }
        value[vl++] = win_path[i];
        if (win_path[i] == '\\') value[vl++] = '\\';
    }
    value[vl] = 0;

    char *out = malloc(len + 2 * (vl + 32) + 64);   /* at most one rewrite per section, two sections */
    if (!out) { free(value); free(data); return fail(why, cap, "out of memory"); }
    size_t ol = 0;
    int done[2] = { 0, 0 }, section = -1, changed = 0, bad_user = 0;
    size_t pos = 0;
    while (pos < len) {
        char *nl = memchr(data + pos, '\n', len - pos);
        size_t ll = nl ? (size_t)(nl - (data + pos)) : len - pos;
        const char *line = data + pos;
        size_t eol = nl ? 1 : 0;
        if (ll && line[0] == '[') {
            section = section_of(line, ll);
        } else if (section >= 0 && ll >= sizeof KEY - 1 && !memcmp(line, KEY, sizeof KEY - 1) && done[section] >= 1) {
            done[section]++;   /* a repeat is refused below; copied as is so the output never outgrows its buffer */
            if (memcmp(line + sizeof KEY - 1, "str(2):", 7) != 0 && section == 1) bad_user = 1;
            memcpy(out + ol, line, ll); ol += ll;
            if (nl) out[ol++] = '\n';
            pos += ll + eol;
            continue;
        } else if (section >= 0 && ll >= sizeof KEY - 1 && !memcmp(line, KEY, sizeof KEY - 1)) {
            const char *rest = line + sizeof KEY - 1;
            int str2 = ll - (sizeof KEY - 1) >= 7 && !memcmp(rest, "str(2):", 7);
            if (section == 1 && !str2) bad_user = 1;
            size_t cr = (ll && line[ll - 1] == '\r') ? 1 : 0;
            char fresh[1200];
            int fl = snprintf(fresh, sizeof fresh, "%s%s\"%s\"", KEY, str2 ? "str(2):" : "", value);
            if (fl < 0 || (size_t)fl >= sizeof fresh) { free(out); free(value); free(data); return fail(why, cap, "the saves path is too long"); }
            if ((size_t)fl + cr != ll || memcmp(fresh, line, (size_t)fl) != 0) changed = 1;
            memcpy(out + ol, fresh, (size_t)fl); ol += (size_t)fl;
            if (cr) out[ol++] = '\r';
            done[section]++;
            if (nl) out[ol++] = '\n';
            pos += ll + eol;
            continue;
        }
        memcpy(out + ol, line, ll); ol += ll;
        if (nl) out[ol++] = '\n';
        pos += ll + eol;
    }
    free(data);
    free(value);
    int rc;
    if (bad_user) rc = fail(why, cap, "User Shell Folders\\Personal in user.reg is not a str(2) value");
    else if (done[0] != 1) rc = fail(why, cap, done[0] ? "Shell Folders\\Personal appears more than once in user.reg" : "user.reg has no Shell Folders\\Personal");
    else if (done[1] != 1) rc = fail(why, cap, done[1] ? "User Shell Folders\\Personal appears more than once in user.reg" : "user.reg has no User Shell Folders\\Personal");
    else if (!changed) rc = 1;
    else rc = jk_fs_write_atomic(reg, out, ol) == 0 ? 0 : fail(why, cap, "user.reg could not be written");
    free(out);
    return rc;
}

/* The Black Edition's game settings (bench/nfsmw.reg, HKLM), with 1920x1080 as the Widescreen Fix
 * itself saves it: packed 0x80000000|w<<16|h = 0x87800438. Proven on the Slim 2026-11-27 (the
 * owner picked 1080p, the fix saved this value, the next start came up in 1920x1080); a plain index
 * (14) was only inferred from the mode list (research/widescreen-001.md section 2). */
/* the registry path the game itself reads (a technical name, never shown): test_no_branding allows this line only */
static const char GAME_KEY[] = "[Software\\\\EA Games\\\\Need for Speed Most Wanted]";   /* registry-path */
static const char GAME_MARK[] = "\"JK4SPEED\"=dword:00000001";   /* written by us once; the game ignores it */
static const char GAME_VALUES[] =
    "\"JK4SPEED\"=dword:00000001\n"
    "\"FirstTime\"=dword:00000001\n"
    "\"SwapSize\"=\"73400320\"\n"
    "\"Language\"=\"English US\"\n"
    "\"g_RacingResolution\"=dword:87800438\n"
    "\"g_VSyncOn\"=dword:00000001\n"
    "\"g_FSAALevel\"=dword:00000000\n"
    "\"g_PerformanceLevel\"=dword:00000001\n"
    "\"g_WorldLodLevel\"=dword:00000003\n"
    "\"g_CarLodLevel\"=dword:00000001\n"
    "\"g_CarEnvironmentMapEnable\"=dword:00000003\n"
    "\"g_CarEnvironmentMapUpdateData\"=dword:00000001\n"
    "\"g_RoadReflectionEnable\"=dword:00000003\n"
    "\"g_MotionBlurEnable\"=dword:00000000\n"
    "\"g_ParticleSystemEnable\"=dword:00000001\n"
    "\"g_OverBrightEnable\"=dword:00000001\n"
    "\"g_RainEnable\"=dword:00000001\n"
    "\"g_TextureFiltering\"=dword:00000002\n"
    "\"g_ShadowDetail\"=dword:00000002\n"
    "\"g_VisualTreatment\"=dword:00000001\n";

int jk_prefix_seed_game_key(const char *prefix_dir, const char *win_game_dir, char *why, size_t cap)
{
    if (why && cap) why[0] = 0;
    if (!prefix_dir || !win_game_dir || !win_game_dir[0]) return fail(why, cap, "no prefix folder or game path");
    char reg[600];
    if (jk_fs_join(reg, sizeof reg, prefix_dir, "user.reg") != 0) return fail(why, cap, "the prefix path is too long");
    size_t len = 0;
    char *data = jk_fs_read(reg, REG_MAX, &len);
    if (!data) return fail(why, cap, "user.reg in the prefix is missing or unreadable");
    /* Our key (it carries the marker) is the player's: never changed. Any other copy of the key
     * (the package's prefix seed holds a stale one from the bench, console 2026-11-27) is dropped
     * and ours is appended. Wine key names ignore letter case. */
    size_t kl = sizeof GAME_KEY - 1, ml = sizeof GAME_MARK - 1;
    size_t key_at = len, key_end = len;
    int in_key = 0, marked = 0;
    for (size_t pos = 0; pos < len;) {
        char *nl = memchr(data + pos, '\n', len - pos);
        size_t ll = nl ? (size_t)(nl - (data + pos)) : len - pos;
        size_t next = pos + ll + (nl ? 1 : 0);
        if (ll && data[pos] == '[') {
            if (in_key) { key_end = pos; in_key = 0; }
            if (key_at == len && ll >= kl && !strncasecmp(data + pos, GAME_KEY, kl)
                && (ll == kl || data[pos + kl] == ' ' || data[pos + kl] == '\r')) { key_at = pos; in_key = 1; }
        } else if (in_key && ll >= ml && !memcmp(data + pos, GAME_MARK, ml)) {
            marked = 1;
        }
        pos = next;
    }
    if (marked) { free(data); return 1; }
    if (key_at < len) {   /* drop the stale key: its header up to the next key */
        memmove(data + key_at, data + key_end, len - key_end);
        len -= key_end - key_at;
        while (len >= 2 && data[len - 1] == '\n' && data[len - 2] == '\n') len--;
    }
    char dir[600];
    size_t dl = 0;
    for (const char *c = win_game_dir; *c; c++) {
        if (*c == '"' || *c == '\n' || dl + 3 >= sizeof dir) { free(data); return fail(why, cap, "the game path is invalid or too long"); }
        dir[dl++] = *c;
        if (*c == '\\') dir[dl++] = '\\';
    }
    dir[dl] = 0;
    size_t cap_out = len + sizeof GAME_KEY + sizeof GAME_VALUES + 2 * dl + 128;
    char *out = malloc(cap_out);
    if (!out) { free(data); return fail(why, cap, "out of memory"); }
    memcpy(out, data, len);
    size_t ol = len;
    if (ol && out[ol - 1] != '\n') out[ol++] = '\n';
    int n = snprintf(out + ol, cap_out - ol, "\n%s %ld\n\"Install Dir\"=\"%s\"\n\"CD Drive\"=\"%s\"\n%s", GAME_KEY,
                     (long)time(NULL), dir, dir, GAME_VALUES);
    free(data);
    if (n < 0 || (size_t)n >= cap_out - ol) { free(out); return fail(why, cap, "out of memory"); }
    ol += (size_t)n;
    int rc = jk_fs_write_atomic(reg, out, ol) == 0 ? 0 : fail(why, cap, "user.reg could not be written");
    free(out);
    return rc;
}
