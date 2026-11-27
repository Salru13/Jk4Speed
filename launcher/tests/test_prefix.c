/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* 1.1: the prefix's Personal folder per game (jk_prefix.c). */
#include <sys/stat.h>
#include <time.h>
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_prefix.h"

#define SH "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders] 1700000000\n"
#define USH "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\User Shell Folders] 1700000000\n"
#define BE "Z:\\\\data\\\\Jk4Speed\\\\Saves"
#define NFS "Z:\\\\data\\\\Jk4Speed\\\\Saves-NFS"

static const char *HEAD = "WINE REGISTRY Version 2\n;; All keys relative to \\\\User\\\\S-1-5-21\n\n#arch=win64\n\n"
    "[Software\\\\Wine\\\\Other] 1700000000\n\"Personal\"=\"keep me\"\n\n";

static void put(const char *dir, const char *body)
{
    char all[4096];
    snprintf(all, sizeof all, "%s%s", HEAD, body);
    tmp_put(dir, "user.reg", all);
}

static void personal_rewrite(void)
{
    char d[256], b[4096], want[4096];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Desktop\"=\"x\"\n\"Personal\"=\"" BE "\"\n\"Zed\"=\"z\"\n\n" USH "\"Personal\"=str(2):\"" BE "\"\n\"Zed\"=str(2):\"z\"\n");
    char why[200];
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), 0);
    snprintf(want, sizeof want, "%s%s", HEAD, SH "\"Desktop\"=\"x\"\n\"Personal\"=\"" NFS "\"\n\"Zed\"=\"z\"\n\n" USH "\"Personal\"=str(2):\"" NFS "\"\n\"Zed\"=str(2):\"z\"\n");
    CHECK(tmp_get(d, "user.reg", b, sizeof b) > 0 && !strcmp(b, want));
    CHECK(!tmp_exists(d, "user.reg.jk4new"));
    tmp_rm_rf(d);
}

static void personal_same(void)
{
    char d[256], p[300], why[200];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Personal\"=\"" NFS "\"\n\n" USH "\"Personal\"=str(2):\"" NFS "\"\n");
    snprintf(p, sizeof p, "%s/user.reg", d);
    struct timespec old[2] = { { 1000, 0 }, { 1000, 0 } };
    utimensat(0, p, old, 0);
    struct stat a, c;
    stat(p, &a);
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), 1);
    stat(p, &c);
    CHECK(a.st_mtime == c.st_mtime && c.st_mtime == 1000);
    tmp_rm_rf(d);
}

static void personal_missing(void)
{
    char d[256], b[4096], b2[4096], why[200];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Personal\"=\"" BE "\"\n\n" USH "\"Desktop\"=str(2):\"x\"\n");
    tmp_get(d, "user.reg", b, sizeof b);
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), -1);
    CHECK(why[0]);
    tmp_get(d, "user.reg", b2, sizeof b2);
    CHECK(!strcmp(b, b2));
    tmp_rm_rf(d);
    /* no file at all */
    tmp_dir(d, sizeof d);
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\x", why, sizeof why), -1);
    CHECK(why[0]);
    tmp_rm_rf(d);
    /* no Personal in either section */
    tmp_dir(d, sizeof d);
    put(d, SH "\"Desktop\"=\"x\"\n");
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\x", why, sizeof why), -1);
    tmp_rm_rf(d);
}

static void personal_strtype(void)
{
    char d[256], b[4096], why[200];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Personal\"=str(2):\"" BE "\"\n\n" USH "\"Personal\"=str(2):\"" BE "\"\n");
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), 0);
    tmp_get(d, "user.reg", b, sizeof b);
    CHECK(strstr(b, "\"Personal\"=str(2):\"" NFS "\""));
    CHECK(strstr(b, "\"Personal\"=\"keep me\""));
    /* User Shell Folders without str(2) is refused, as make_seed.py does */
    put(d, SH "\"Personal\"=\"" BE "\"\n\n" USH "\"Personal\"=\"" BE "\"\n");
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\x", why, sizeof why), -1);
    tmp_rm_rf(d);
}

/* I-1: many short or duplicated Personal lines must never overflow the output */
static void personal_many_lines(void)
{
    char d[256], b[4096], b2[4096], why[200], body[2048] = SH;
    tmp_dir(d, sizeof d);
    for (int i = 0; i < 30; i++) strcat(body, "\"Personal\"=\"\"\n");
    strcat(body, "\n" USH);
    for (int i = 0; i < 30; i++) strcat(body, "\"Personal\"=str(2):\"\"\n");
    put(d, body);
    tmp_get(d, "user.reg", b, sizeof b);
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), -1);
    CHECK(why[0]);
    tmp_get(d, "user.reg", b2, sizeof b2);
    CHECK(!strcmp(b, b2));
    put(d, SH "\"Personal\"=\"a\"\n\"Personal\"=\"a\"\n\"Personal\"=\"a\"\n\"Personal\"=\"a\"\n\"Personal\"=\"a\"\n\n" USH "\"Personal\"=str(2):\"a\"\n");
    CHECK_EQ(jk_prefix_set_personal(d, "Z:\\data\\Jk4Speed\\Saves-NFS", why, sizeof why), -1);
    tmp_rm_rf(d);
}

static void personal_long_value(void)
{
    char d[256], b[8192], why[200], win[1100];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Personal\"=\"\"\n\n" USH "\"Personal\"=str(2):\"\"\n");
    strcpy(win, "Z:\\");
    while (strlen(win) < 500) strcat(win, "dir\\");
    CHECK_EQ(jk_prefix_set_personal(d, win, why, sizeof why), 0);
    tmp_get(d, "user.reg", b, sizeof b);
    CHECK(strstr(b, "dir\\\\dir"));
    tmp_rm_rf(d);
}

/* 1.1: the original's game settings in HKCU (the Widescreen Fix reads HKCU, not HKLM): written
 * once, with 1080p (the packed 1920x1080 value the fix itself saves) and the Black Edition's
 * values, so the first start is no "first run" at 640x480. */
#define GKEY "[Software\\\\EA Games\\\\Need for Speed Most Wanted]"

static void game_key_added_once(void)
{
    char d[256], b[8192], before[4096], why[200];
    tmp_dir(d, sizeof d);
    put(d, SH "\"Personal\"=\"" NFS "\"\n");
    tmp_get(d, "user.reg", before, sizeof before);
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), 0);
    CHECK(tmp_get(d, "user.reg", b, sizeof b) > 0);
    CHECK(!strncmp(b, before, strlen(before)));                 /* everything before stays as it was */
    char *k = strstr(b, GKEY " ");
    CHECK(k != NULL);
    if (k) {
        CHECK(strstr(k, "\n\"FirstTime\"=dword:00000001\n"));
        CHECK(strstr(k, "\n\"g_RacingResolution\"=dword:87800438\n"));
        CHECK(strstr(k, "\n\"g_VSyncOn\"=dword:00000001\n"));
        CHECK(strstr(k, "\n\"g_WorldLodLevel\"=dword:00000003\n"));
        CHECK(strstr(k, "\n\"Install Dir\"=\"Z:\\\\data\\\\Jk4Speed\\\\NFS\"\n"));
        CHECK(strstr(k, "\n\"Language\"=\"English US\"\n"));
    }
    CHECK(!tmp_exists(d, "user.reg.jk4new"));
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), 1);   /* once only */
    char again[8192];
    CHECK(tmp_get(d, "user.reg", again, sizeof again) > 0 && !strcmp(again, b));
    tmp_rm_rf(d);
}

/* Our seed's own key, once written (the marker), is the player's from then on. */
static void game_key_kept(void)
{
    char d[256], b[4096], b2[4096], why[200];
    tmp_dir(d, sizeof d);
    put(d, GKEY " 1700000000\n\"JK4SPEED\"=dword:00000001\n\"g_RacingResolution\"=dword:00000005\n\n"
           "[Software\\\\Wine\\\\After] 1700000000\n\"a\"=\"b\"\n");     /* the player chose another mode */
    tmp_get(d, "user.reg", b, sizeof b);
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), 1);
    CHECK(tmp_get(d, "user.reg", b2, sizeof b2) > 0 && !strcmp(b, b2));
    tmp_rm_rf(d);
}

/* Console 2026-11-27: the package's prefix seed (made from the bench prefix) already holds this key
 * from the bench's Widescreen Fix days: FirstTime 0, mode 0, another game folder. It is replaced
 * once, so 1080p and the Black Edition's values are there at the first start; other keys stay. */
static void game_key_stale_replaced(void)
{
    char d[256], b[8192], why[200];
    tmp_dir(d, sizeof d);
    put(d, GKEY " 1700000000\n#time=1dd7e6d90eccccc\n\"FirstTime\"=dword:00000000\n\"g_RacingResolution\"=dword:00000000\n"
           "\"g_VSyncOn\"=dword:00000000\n\"Install Dir\"=\"Z:\\\\data\\\\Jk4Speed\\\\NFSMW\\\\\"\n\n"
           "[Software\\\\Wine\\\\After] 1700000000\n\"a\"=\"b\"\n");
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), 0);
    CHECK(tmp_get(d, "user.reg", b, sizeof b) > 0);
    CHECK(strstr(b, "[Software\\\\Wine\\\\Other] 1700000000\n\"Personal\"=\"keep me\"\n"));
    CHECK(strstr(b, "[Software\\\\Wine\\\\After] 1700000000\n\"a\"=\"b\"\n"));
    char *k = strstr(b, GKEY " ");
    CHECK(k != NULL && strstr(k + 1, GKEY) == NULL);                /* one key, not two */
    CHECK(strstr(b, "\"g_RacingResolution\"=dword:00000000") == NULL);
    CHECK(strstr(b, "\"FirstTime\"=dword:00000000") == NULL);
    CHECK(strstr(b, "NFSMW") == NULL);
    if (k) {
        CHECK(strstr(k, "\n\"JK4SPEED\"=dword:00000001\n"));
        CHECK(strstr(k, "\n\"g_RacingResolution\"=dword:87800438\n"));
        CHECK(strstr(k, "\n\"g_VSyncOn\"=dword:00000001\n"));
        CHECK(strstr(k, "\n\"FirstTime\"=dword:00000001\n"));
    }
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), 1);   /* then never again */
    tmp_rm_rf(d);
}

static void game_key_no_reg(void)
{
    char d[256], why[200] = "";
    tmp_dir(d, sizeof d);
    CHECK_EQ(jk_prefix_seed_game_key(d, "Z:\\data\\Jk4Speed\\NFS", why, sizeof why), -1);
    CHECK(why[0]);
    CHECK(!tmp_exists(d, "user.reg"));
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(game_key_added_once);
    RUN(game_key_kept);
    RUN(game_key_stale_replaced);
    RUN(game_key_no_reg);
    RUN(personal_many_lines);
    RUN(personal_long_value);
    RUN(personal_rewrite);
    RUN(personal_same);
    RUN(personal_missing);
    RUN(personal_strtype);
    return jk_test_report("test_prefix");
}
