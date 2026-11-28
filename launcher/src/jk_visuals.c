/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* SETTINGS > VISUALS. See jk_visuals.h. */
#include "jk_visuals.h"
#include "jk_fs.h"
#include "jk_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXV 8
typedef struct Slider {
    const char *key, *label, *about;
    int n, def;
    const char *val[MAXV];
    const char *shown[MAXV];
} Slider;

/* The defaults are the pinned X360Stuff.ini's values (and the base's shadow detail 2). */
static const Slider S[JK_VIS_N] = {
    { "MiddleGrayValue", "Brightness", "How bright the picture adapts. Xbox 360: 0.14, PC: 0.19.",
      8, 4, { "0.10", "0.12", "0.14", "0.16", "0.19", "0.22", "0.25", "0.30" },
      { "0.10", "0.12", "0.14 (XBOX 360)", "0.16", "0.19 (PC)", "0.22", "0.25", "0.30" } },
    { "ForcedLuminance", "Fixed light level", "Off: the light adapts as you drive. A value keeps the light level fixed.",
      6, 0, { "0.0", "0.2", "0.4", "0.6", "0.8", "1.0" }, { "OFF", "0.2", "0.4", "0.6", "0.8", "1.0" } },
    { "UseColourBloomCurve", "Bloom colour", "Which curve the colour bloom uses: Xbox 360 or PC.",
      2, 1, { "0", "1" }, { "XBOX 360", "PC" } },
    { "NicerBlackBloom", "Soft bloom (FPS)", "A real blur for the dark bloom, softer than the Xbox 360.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "CarShaperLightType", "Car lighting", "How the cars are lit: Xbox 360, OPM, E3 or PC.",
      4, 0, { "0", "1", "2", "3" }, { "XBOX 360", "OPM", "E3", "PC" } },
    { "CurrentGenSunFlare", "Sun flare", "The Xbox 360 sun flare, or the bigger PC one.",
      2, 0, { "0", "1" }, { "XBOX 360", "PC" } },
    { "Xbox360ShadowAngle", "Sunset shadows", "ON: sunsets do not cover the world in shadow (Xbox 360).",
      2, 1, { "0", "1" }, { "OFF", "ON" } },
    { "MinimumShadowAngleHeight", "Shadow height", "How low the sun casts shadows. Xbox 360: 0.7.",
      6, 3, { "0.0", "0.3", "0.5", "0.7", "0.9", "1.0" }, { "0.0", "0.3", "0.5", "0.7 (XBOX 360)", "0.9", "1.0" } },
    { "DXVK_PS4_NFS_SHADOW_DETAIL", "Shadow detail (FPS)", "GAME: the game's own setting. HIGH costs the most frame time.",
      3, 2, { "0", "1", "2" }, { "GAME", "MEDIUM", "HIGH" } },
    { "envmap_faces", "Car reflection update (FPS)",
      "How fast car reflections follow the world. Above 1: 30 FPS recommended on PS4 Slim and Fat.",
      4, 0, { "1", "2", "3", "6" }, { "1 (60 FPS)", "2", "3", "6 (EVERY FRAME)" } },
    { "EnvMapFarPlane", "Reflection distance (FPS)", "How far car reflections see. Xbox 360: 100 m. Further costs frame time.",
      4, 3, { "100.0", "300.0", "1000.0", "15000.0" }, { "100 M (XBOX 360)", "300 M", "1000 M", "FULL (PC)" } },
    { "MirrorFarPlane", "Mirror distance (FPS)", "How far the rear-view mirror and rear reflection see. Xbox 360: 400 m.",
      3, 0, { "400.0", "1000.0", "15000.0" }, { "400 M (XBOX 360)", "1000 M", "FULL (PC)" } },
    { "CubemapRVM", "Mirror from reflection (FPS)", "The rear-view mirror uses the car's reflection and shows other cars.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "CarsInAllCubemapFaces", "Cars in all reflections (FPS)", "Other cars in every reflection, not only behind. Experimental, costs frame time.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "CarSelfShadow", "Car self-shadow", "Cars shadow themselves. Not how the game was made: may look wrong.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "MotionBlur", "Motion blur (FPS)", "Blur at speed. FAST skips the depth mask. Costs frame time.",
      3, 0, { "0", "1", "2" }, { "OFF", "ON", "FAST" } },
    { "DirectionalMotionBlurStrength", "Motion blur strength", "How strongly speed blurs the picture (motion blur on).",
      4, 1, { "0.5", "1.0", "1.5", "2.0" }, { "0.5", "1.0", "1.5", "2.0" } },
    { "AngularMotionBlurStrength", "Turning blur", "Blur when the camera turns (motion blur on). Unused on Xbox 360.",
      3, 0, { "0.0", "250.0", "500.0" }, { "OFF", "250", "500" } },
    { "DOF", "Depth of field (FPS)", "Blurs the background in cutscenes and some camera moments.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "NicerDOF", "Soft depth of field (FPS)", "A real blur for the depth of field (depth of field on).",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "GrassShader", "Grass (FPS)", "The custom grass shader. Costs frame time.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "DoParallax", "Parallax surfaces", "Some surfaces get depth when you move.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
    { "UseFullLODShaderOnTrafficAndCops", "Traffic and cop detail (FPS)", "Full detail shader on traffic and cop cars. Costs frame time.",
      2, 0, { "0", "1" }, { "OFF", "ON" } },
};

void jk_visuals_default(JkVisuals *v)
{
    if (!v) return;
    for (int i = 0; i < JK_VIS_N; i++) v->idx[i] = (uint8_t)S[i].def;
}

int jk_visuals_is_default(const JkVisuals *v)
{
    for (int i = 0; v && i < JK_VIS_N; i++)
        if (v->idx[i] != S[i].def) return 0;
    return v != NULL;
}

int jk_visuals_step(JkVisuals *v, int item, int dir)
{
    if (!v || item < 0 || item >= JK_VIS_N || !dir) return 0;
    int k = v->idx[item] + (dir < 0 ? -1 : 1);
    if (k < 0 || k >= S[item].n) return 0;
    v->idx[item] = (uint8_t)k;
    return 1;
}

static int ok_item(int item) { return item >= 0 && item < JK_VIS_N; }
static int idx_of(const JkVisuals *v, int item) { return v && v->idx[item] < S[item].n ? v->idx[item] : S[item].def; }

const char *jk_visuals_key(int item) { return ok_item(item) ? S[item].key : ""; }
const char *jk_visuals_label(int item) { return ok_item(item) ? S[item].label : ""; }
const char *jk_visuals_about(int item) { return ok_item(item) ? S[item].about : ""; }
const char *jk_visuals_value(const JkVisuals *v, int item) { return ok_item(item) ? S[item].val[idx_of(v, item)] : ""; }
const char *jk_visuals_shown(const JkVisuals *v, int item) { return ok_item(item) ? S[item].shown[idx_of(v, item)] : ""; }
const char *jk_visuals_shadow_detail(const JkVisuals *v) { return jk_visuals_value(v, JK_VIS_SHADOW_DETAIL); }
const char *jk_visuals_envmap_faces(const JkVisuals *v) { return jk_visuals_value(v, JK_VIS_REFLECTIONS); }
/* the sliders of X360Stuff.ini (the others are jk4speed-dev.ini keys) */
static int in_x360(int item) { return item != JK_VIS_SHADOW_DETAIL && item != JK_VIS_REFLECTIONS; }

static int file_of(const char *state_dir, char *out, size_t cap)
{
    return state_dir ? jk_fs_join(out, cap, state_dir, "visuals.txt") : -1;
}

int jk_visuals_load(const char *state_dir, JkVisuals *out)
{
    char path[1024];
    if (!out) return -1;
    jk_visuals_default(out);
    if (file_of(state_dir, path, sizeof path) < 0) return -1;
    char *t = jk_fs_read(path, 16 * 1024, NULL);
    if (!t) return 0;
    for (char *line = t, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char key[64], val[32];
        if (sscanf(line, "%63s %31s", key, val) != 2) continue;
        for (int i = 0; i < JK_VIS_N; i++) {
            if (strcmp(key, S[i].key)) continue;
            for (int k = 0; k < S[i].n; k++)
                if (!strcmp(val, S[i].val[k])) out->idx[i] = (uint8_t)k;
        }
    }
    free(t);
    return 0;
}

int jk_visuals_save(const char *state_dir, const JkVisuals *v)
{
    char path[1024], buf[2048];
    if (!v || file_of(state_dir, path, sizeof path) < 0) return -1;
    size_t n = 0;
    for (int i = 0; i < JK_VIS_N; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s %s\n", S[i].key, jk_visuals_value(v, i));
    if (jk_fs_mkdir_parent(path) < 0) return -1;
    return jk_fs_write_atomic(path, buf, n) < 0 ? -1 : 0;
}

/* The start of the value on key's line ("<key> = " at a line start), or NULL;
 * *count = how many lines carry the key. */
static const char *find_value(const char *in, size_t n, const char *key, int *count)
{
    size_t kl = strlen(key);
    const char *hit = NULL;
    *count = 0;
    for (size_t i = 0; i < n; ) {
        size_t e = i;
        while (e < n && in[e] != '\n') e++;
        if (e - i > kl + 3 && !memcmp(in + i, key, kl) && !memcmp(in + i + kl, " = ", 3)) {
            (*count)++;
            hit = in + i + kl + 3;
        }
        i = e + 1;
    }
    return hit;
}

int jk_visuals_rewrite(const char *in, size_t n, const JkVisuals *v, char *out, size_t cap)
{
    if (!in || !v || !out) return -1;
    const char *at[JK_VIS_N];
    for (int i = 0; i < JK_VIS_N; i++) {
        at[i] = NULL;
        if (!in_x360(i)) continue;                               /* jk4speed-dev.ini, not this file */
        int count = 0;
        at[i] = find_value(in, n, S[i].key, &count);
        if (count != 1) return -1;
    }
    size_t o = 0;
    const char *p = in, *end = in + n;
    while (p < end) {
        int item = -1;
        for (int i = 0; i < JK_VIS_N; i++)
            if (at[i] == p) item = i;
        if (item < 0) {
            if (o + 1 > cap) return -1;
            out[o++] = *p++;
            continue;
        }
        const char *e = p;                                        /* the old value: up to \r or \n */
        while (e < end && *e != '\r' && *e != '\n') e++;
        const char *nv = jk_visuals_value(v, item);
        size_t l = strlen(nv);
        if (o + l > cap) return -1;
        memcpy(out + o, nv, l);
        o += l;
        p = e;
    }
    return (int)o;
}

int jk_visuals_write_x360(const char *game_dir, const JkVisuals *v, char *why, size_t cap)
{
    char path[1100], sha[65];
    if (why && cap) why[0] = 0;
    if (!game_dir || !v || jk_fs_join(path, sizeof path, game_dir, "scripts/X360Stuff.ini") < 0) return -1;
    if (!jk_fs_exists(path)) return 0;                            /* the mods are off */
    size_t n = 0;
    char *t = jk_fs_read(path, 64 * 1024, &n);
    if (!t) { if (why) snprintf(why, cap, "X360Stuff.ini not read"); return -1; }
    static char base[64 * 1024], want[64 * 1024];
    JkVisuals d;
    jk_visuals_default(&d);
    int bn = jk_visuals_rewrite(t, n, &d, base, sizeof base);
    jk_sha256_hex(base, bn < 0 ? 0 : (size_t)bn, sha);
    if (bn < 0 || strcmp(sha, JK_VIS_X360_SHA)) {                /* not the pinned file: someone else's */
        free(t);
        if (why) snprintf(why, cap, "X360Stuff.ini is not the installed one: left as it is");
        return 1;
    }
    int wn = jk_visuals_rewrite(t, n, v, want, sizeof want);
    int same = wn >= 0 && (size_t)wn == n && !memcmp(want, t, n);
    free(t);
    if (wn < 0) { if (why) snprintf(why, cap, "X360Stuff.ini values not set"); return -1; }
    if (same) return 0;
    if (jk_fs_write_atomic(path, want, (size_t)wn) < 0) { if (why) snprintf(why, cap, "X360Stuff.ini not written"); return -1; }
    return 0;
}
