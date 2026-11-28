/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * 1.11 SETTINGS > VISUALS (the Xbox 360 look only): 23 sliders. 21 are the
 * look keys of the X360 Stuff Pack's scripts/X360Stuff.ini (colour, light, bloom,
 * shadows, reflections, blur, DOF, grass ...; not its scaling, resolution or
 * fix keys), two are ours in jk4speed-dev.ini: DXVK_PS4_NFS_SHADOW_DETAIL and
 * envmap_faces (car reflection cube faces re-rendered per frame). Each
 * slider has a fixed list of values; the defaults are the values the pinned
 * X360Stuff.ini (sha JK_VIS_X360_SHA) and jk4speed.ini.base carry.
 *
 * Saved as <state_dir>/visuals.txt ("<key> <value>" lines). The mod check
 * (jk_modset) only ever sees the pinned ini: the launcher writes the defaults
 * back when it starts and the player's values only at PLAY, right before the
 * game runs (jk_visuals_write_x360).
 */
#ifndef JK_VISUALS_H
#define JK_VISUALS_H

#include <stddef.h>
#include <stdint.h>

#define JK_VIS_X360_SHA "8436599baf4dbd02f7382801b778d4fa49f6137343da470bb92c0c1e676f166f"

typedef enum JkVisItem {
    JK_VIS_BRIGHTNESS = 0,      /* MiddleGrayValue */
    JK_VIS_LUMINANCE,           /* ForcedLuminance */
    JK_VIS_BLOOM_CURVE,         /* UseColourBloomCurve */
    JK_VIS_SOFT_BLOOM,          /* NicerBlackBloom */
    JK_VIS_CAR_LIGHT,           /* CarShaperLightType */
    JK_VIS_SUN_FLARE,           /* CurrentGenSunFlare */
    JK_VIS_SUNSET_SHADOWS,      /* Xbox360ShadowAngle */
    JK_VIS_SHADOW_HEIGHT,       /* MinimumShadowAngleHeight */
    JK_VIS_SHADOW_DETAIL,       /* DXVK_PS4_NFS_SHADOW_DETAIL (jk4speed-dev.ini) */
    JK_VIS_REFLECTIONS,         /* envmap_faces (jk4speed-dev.ini): car reflection faces per frame */
    JK_VIS_ENV_DISTANCE,        /* EnvMapFarPlane */
    JK_VIS_MIRROR_DISTANCE,     /* MirrorFarPlane */
    JK_VIS_MIRROR_REFLECT,      /* CubemapRVM */
    JK_VIS_CARS_IN_REFLECT,     /* CarsInAllCubemapFaces */
    JK_VIS_CAR_SELF_SHADOW,     /* CarSelfShadow */
    JK_VIS_MOTION_BLUR,         /* MotionBlur */
    JK_VIS_BLUR_STRENGTH,       /* DirectionalMotionBlurStrength */
    JK_VIS_ANGULAR_BLUR,        /* AngularMotionBlurStrength */
    JK_VIS_DOF,                 /* DOF */
    JK_VIS_SOFT_DOF,            /* NicerDOF */
    JK_VIS_GRASS,               /* GrassShader */
    JK_VIS_PARALLAX,            /* DoParallax */
    JK_VIS_TRAFFIC_DETAIL,      /* UseFullLODShaderOnTrafficAndCops */
    JK_VIS_N
} JkVisItem;

typedef struct JkVisuals { uint8_t idx[JK_VIS_N]; } JkVisuals;   /* slider -> index in its value list */

void jk_visuals_default(JkVisuals *v);
int jk_visuals_is_default(const JkVisuals *v);
/* One step left (dir < 0) or right. 1 changed, 0 at the end or bad args. */
int jk_visuals_step(JkVisuals *v, int item, int dir);

const char *jk_visuals_key(int item);                       /* "MiddleGrayValue" ... */
const char *jk_visuals_label(int item);                     /* "Brightness" ... */
const char *jk_visuals_about(int item);                     /* one line of help */
const char *jk_visuals_value(const JkVisuals *v, int item); /* as written: "0.19" */
const char *jk_visuals_shown(const JkVisuals *v, int item); /* as shown: "0.19 (PC)" */
/* The DXVK_PS4_NFS_SHADOW_DETAIL value ("0", "1", "2"). */
const char *jk_visuals_shadow_detail(const JkVisuals *v);
/* The envmap_faces value ("1", "2", "3", "6"). */
const char *jk_visuals_envmap_faces(const JkVisuals *v);

/* <state_dir>/visuals.txt. A missing file, an unknown key or a value a slider
 * does not have gives that slider's default. 0 ok, -1 bad args. */
int jk_visuals_load(const char *state_dir, JkVisuals *out);
int jk_visuals_save(const char *state_dir, const JkVisuals *v);

/* The X360Stuff.ini text with the eight ini sliders' values: the value after
 * "<key> = " on each key's line; every other byte as it was. -1 when a key is
 * not on exactly one line or cap is too small; else the length. */
int jk_visuals_rewrite(const char *in, size_t n, const JkVisuals *v, char *out, size_t cap);

/* <game_dir>/scripts/X360Stuff.ini gets v's values, but only when the file,
 * set back to the defaults, is the pinned ini, and only when bytes change.
 * 0: done, or no file (mods off); 1: not the pinned ini, left alone; -1 error. */
int jk_visuals_write_x360(const char *game_dir, const JkVisuals *v, char *why, size_t cap);

#endif
