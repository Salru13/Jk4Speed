/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* 1.11 SETTINGS > VISUALS (jk_visuals.h): colour, light and shadow sliders for the
 * Xbox 360 look. Eight are keys of the X360 Stuff Pack's scripts/X360Stuff.ini, one is
 * our DXVK_PS4_NFS_SHADOW_DETAIL. The game folder's ini is the pinned file whenever the
 * launcher runs (defaults written back), and carries the player's values only from PLAY,
 * so the mod check never sees a changed file. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_visuals.h"
#include "jk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PINNED JK_ROOT_DIR "/../bench/x360/X360Stuff.ini"     /* the file PATCH installs (sha 8436599b) */

/* The mod's own file is never in the public repository (no mod files): the checks that
 * need it are skipped there and run where the file is (the project's tree). */
static char *pinned(size_t *n)
{
    char *t = jk_fs_read(PINNED, 64 * 1024, n);
    if (!t) printf("  skip: no %s (a mod file, not in the public repository)\n", PINNED);
    return t;
}

static void defaults_are_the_installed_values(void)
{
    JkVisuals v;
    jk_visuals_default(&v);
    CHECK(jk_visuals_is_default(&v));
    CHECK_EQ(JK_VIS_N, 23);
    static const char *const want[JK_VIS_N][2] = {
        { "MiddleGrayValue", "0.19" }, { "ForcedLuminance", "0.0" }, { "UseColourBloomCurve", "1" },
        { "NicerBlackBloom", "0" }, { "CarShaperLightType", "0" }, { "CurrentGenSunFlare", "0" },
        { "Xbox360ShadowAngle", "1" }, { "MinimumShadowAngleHeight", "0.7" }, { "DXVK_PS4_NFS_SHADOW_DETAIL", "2" },
        { "envmap_faces", "1" }, { "EnvMapFarPlane", "15000.0" }, { "MirrorFarPlane", "400.0" },
        { "CubemapRVM", "0" }, { "CarsInAllCubemapFaces", "0" }, { "CarSelfShadow", "0" }, { "MotionBlur", "0" },
        { "DirectionalMotionBlurStrength", "1.0" }, { "AngularMotionBlurStrength", "0.0" }, { "DOF", "0" },
        { "NicerDOF", "0" }, { "GrassShader", "0" }, { "DoParallax", "0" }, { "UseFullLODShaderOnTrafficAndCops", "0" },
    };
    for (int i = 0; i < JK_VIS_N; i++) {
        CHECK(!strcmp(jk_visuals_key(i), want[i][0]));
        CHECK(!strcmp(jk_visuals_value(&v, i), want[i][1]));
        CHECK(jk_visuals_label(i)[0] && jk_visuals_shown(&v, i)[0] && jk_visuals_about(i)[0]);
    }
    CHECK(!strcmp(jk_visuals_shadow_detail(&v), "2"));
    CHECK(!strcmp(jk_visuals_envmap_faces(&v), "1"));
}

/* Car reflection update: the client's envmap_faces, 1, 2, 3 or all 6 faces per frame
 * (client envfaces.inc parses exactly these); above 1 the help says 30 FPS on Slim/Fat. */
static void reflection_update_values(void)
{
    JkVisuals v;
    jk_visuals_default(&v);
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_REFLECTIONS, -1), 0);            /* 1 is the bottom */
    const char *want[] = { "2", "3", "6" };
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(jk_visuals_step(&v, JK_VIS_REFLECTIONS, +1), 1);
        CHECK(!strcmp(jk_visuals_envmap_faces(&v), want[i]));
    }
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_REFLECTIONS, +1), 0);
    CHECK(strstr(jk_visuals_about(JK_VIS_REFLECTIONS), "30 FPS") != NULL);
    CHECK(strstr(jk_visuals_about(JK_VIS_REFLECTIONS), "Slim") != NULL);
}

/* Owner: a slider that can cost frame rate says so in its name, "(FPS)". */
static void fps_costly_sliders_are_marked(void)
{
    static const int costly[] = { JK_VIS_SOFT_BLOOM, JK_VIS_SHADOW_DETAIL, JK_VIS_REFLECTIONS, JK_VIS_ENV_DISTANCE,
                                  JK_VIS_MIRROR_DISTANCE, JK_VIS_MIRROR_REFLECT, JK_VIS_CARS_IN_REFLECT, JK_VIS_MOTION_BLUR,
                                  JK_VIS_DOF, JK_VIS_SOFT_DOF, JK_VIS_GRASS, JK_VIS_TRAFFIC_DETAIL };
    int mark[JK_VIS_N] = { 0 };
    for (size_t i = 0; i < sizeof costly / sizeof costly[0]; i++) mark[costly[i]] = 1;
    for (int i = 0; i < JK_VIS_N; i++)
        CHECK_EQ(strstr(jk_visuals_label(i), "(FPS)") != NULL, mark[i]);
}

static void step_clamps_at_the_ends(void)
{
    JkVisuals v;
    jk_visuals_default(&v);
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_SHADOW_DETAIL, +1), 0);          /* 2 is the top */
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_SHADOW_DETAIL, -1), 1);
    CHECK(!strcmp(jk_visuals_shadow_detail(&v), "1"));
    CHECK(!jk_visuals_is_default(&v));
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_SHADOW_DETAIL, -1), 1);
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_SHADOW_DETAIL, -1), 0);          /* 0 is the bottom */
    CHECK(!strcmp(jk_visuals_shadow_detail(&v), "0"));
    CHECK_EQ(jk_visuals_step(&v, -1, 1), 0);
    CHECK_EQ(jk_visuals_step(&v, JK_VIS_N, 1), 0);
}

static void defaults_rewrite_the_pinned_file_byte_for_byte(void)
{
    size_t n = 0;
    char *t = pinned(&n);
    if (!t) return;
    JkVisuals v;
    jk_visuals_default(&v);
    static char out[16 * 1024];
    int r = jk_visuals_rewrite(t, n, &v, out, sizeof out);
    CHECK_EQ(r, (long long)n);
    CHECK(r > 0 && !memcmp(out, t, n));
    free(t);
}

static void chosen_values_change_only_their_lines(void)
{
    size_t n = 0;
    char *t = pinned(&n);
    if (!t) return;
    JkVisuals v;
    jk_visuals_default(&v);
    while (jk_visuals_step(&v, JK_VIS_BRIGHTNESS, -1)) {}                 /* 0.10 */
    jk_visuals_step(&v, JK_VIS_CAR_LIGHT, +1);                            /* 1 (OPM) */
    jk_visuals_step(&v, JK_VIS_DOF, +1);                                  /* DOF = 1 */
    jk_visuals_step(&v, JK_VIS_ENV_DISTANCE, -1);                         /* 1000.0 */
    while (jk_visuals_step(&v, JK_VIS_SHADOW_DETAIL, -1)) {}              /* not in this file */
    static char out[16 * 1024], back[16 * 1024];
    int r = jk_visuals_rewrite(t, n, &v, out, sizeof out);
    CHECK(r > 0);
    CHECK(strstr(out, "\r\nMiddleGrayValue = 0.10\r\n") != NULL);
    CHECK(strstr(out, "\r\nCarShaperLightType = 1\r\n") != NULL);
    CHECK(strstr(out, "MiddleGrayValue = 0.19") == NULL);
    CHECK(strstr(out, "\r\nDOF = 1\r\n") != NULL);
    CHECK(strstr(out, "\r\nEnvMapFarPlane = 1000.0\r\n") != NULL);
    CHECK(strstr(out, "\r\nForcedLuminance = 0.0\r\n") != NULL);          /* the others as they were */
    CHECK(strstr(out, "\r\nNicerDOF = 0\r\n") != NULL);                   /* DOF is not NicerDOF's line */
    /* the defaults again give the pinned file back */
    JkVisuals d;
    jk_visuals_default(&d);
    int k = jk_visuals_rewrite(out, (size_t)r, &d, back, sizeof back);
    CHECK_EQ(k, (long long)n);
    CHECK(k > 0 && !memcmp(back, t, n));
    free(t);
}

static void a_file_without_the_keys_is_refused(void)
{
    JkVisuals v;
    jk_visuals_default(&v);
    char out[256];
    const char *t = "[VIDEO]\r\nGrassShader = 0\r\n";
    CHECK_EQ(jk_visuals_rewrite(t, strlen(t), &v, out, sizeof out), -1);
    /* a key twice is not the pinned file either */
    size_t n = 0;
    char *p = pinned(&n);
    if (!p) return;
    static char twice[20 * 1024];
    memcpy(twice, p, n);
    memcpy(twice + n, "MiddleGrayValue = 0.19\r\n", 24);
    static char o2[20 * 1024];
    CHECK_EQ(jk_visuals_rewrite(twice, n + 24, &v, o2, sizeof o2), -1);
    free(p);
}

static void save_load_roundtrip_and_bad_file(void)
{
    char d[512];
    tmp_dir(d, sizeof d);
    JkVisuals v, w;
    jk_visuals_default(&v);
    jk_visuals_step(&v, JK_VIS_BRIGHTNESS, -1);
    jk_visuals_step(&v, JK_VIS_SUN_FLARE, +1);
    CHECK_EQ(jk_visuals_save(d, &v), 0);
    CHECK_EQ(jk_visuals_load(d, &w), 0);
    CHECK(!memcmp(&v, &w, sizeof v));
    /* a value the slider does not have, an unknown key, junk: that slider's default */
    tmp_put(d, "visuals.txt", "MiddleGrayValue 9.99\nNoSuchKey 1\njunk\nCurrentGenSunFlare 1\n");
    CHECK_EQ(jk_visuals_load(d, &w), 0);
    CHECK(!strcmp(jk_visuals_value(&w, JK_VIS_BRIGHTNESS), "0.19"));
    CHECK(!strcmp(jk_visuals_value(&w, JK_VIS_SUN_FLARE), "1"));
    tmp_rm_rf(d);
    CHECK_EQ(jk_visuals_load(d, &w), 0);                                  /* no file: the defaults */
    CHECK(jk_visuals_is_default(&w));
    CHECK_EQ(jk_visuals_load(NULL, &w), -1);
}

static void game_file(const char *game, char *out, size_t cap) { snprintf(out, cap, "%s/scripts/X360Stuff.ini", game); }

static void write_x360_only_touches_the_pinned_file(void)
{
    size_t n = 0;
    char *t = pinned(&n);
    if (!t) return;
    char g[512], path[1100], why[200];
    tmp_dir(g, sizeof g);
    game_file(g, path, sizeof path);
    JkVisuals v;
    jk_visuals_default(&v);
    /* no file (the mods are off): nothing written */
    CHECK_EQ(jk_visuals_write_x360(g, &v, why, sizeof why), 0);
    CHECK(!jk_fs_exists(path));
    /* the pinned file: the values in, then the defaults give it back byte for byte */
    CHECK_EQ(jk_fs_mkdir_parent(path), 0);
    CHECK_EQ(jk_fs_write_atomic(path, t, n), 0);
    jk_visuals_step(&v, JK_VIS_BRIGHTNESS, +1);
    CHECK_EQ(jk_visuals_write_x360(g, &v, why, sizeof why), 0);
    size_t m = 0;
    char *now = jk_fs_read(path, 64 * 1024, &m);
    CHECK(now && strstr(now, "\r\nMiddleGrayValue = 0.22\r\n"));
    free(now);
    char sha[65];
    jk_visuals_default(&v);
    CHECK_EQ(jk_visuals_write_x360(g, &v, why, sizeof why), 0);
    CHECK(jk_fs_sha256(path, sha, NULL) == 0 && !strcmp(sha, JK_VIS_X360_SHA));
    /* someone else's ini: left as it is */
    CHECK_EQ(jk_fs_write_atomic(path, "MiddleGrayValue = 0.5\r\n", 23), 0);
    jk_visuals_step(&v, JK_VIS_BRIGHTNESS, +1);
    CHECK_EQ(jk_visuals_write_x360(g, &v, why, sizeof why), 1);
    now = jk_fs_read(path, 64 * 1024, &m);
    CHECK(now && m == 23 && !memcmp(now, "MiddleGrayValue = 0.5\r\n", 23));
    free(now);
    tmp_rm_rf(g);
    free(t);
}

int main(void)
{
    RUN(defaults_are_the_installed_values);
    RUN(step_clamps_at_the_ends);
    RUN(reflection_update_values);
    RUN(fps_costly_sliders_are_marked);
    RUN(defaults_rewrite_the_pinned_file_byte_for_byte);
    RUN(chosen_values_change_only_their_lines);
    RUN(a_file_without_the_keys_is_refused);
    RUN(save_load_roundtrip_and_bad_file);
    RUN(write_x360_only_touches_the_pinned_file);
    return jk_test_report("test_visuals");
}
