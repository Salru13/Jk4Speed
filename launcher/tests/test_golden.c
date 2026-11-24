/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Golden images (spec 11): every screen and state is rendered and its pixel
 * hash compared with launcher/golden/goldens.txt. Previews are always written
 * to build/preview/<name>.png (uncompressed); tools/pack_goldens.py turns them
 * into the committed launcher/golden/<name>.png.
 * JK_UPDATE_GOLDEN=1 rewrites goldens.txt (after a deliberate change only).
 */
#include "jk_test.h"
#include "jk_art.h"
#include "jk_cases.h"
#include "jk_loader.h"
#include "jk_png.h"
#include "jk_sha256.h"

#include <stdlib.h>
#include <sys/stat.h>

#define GOLDENS JK_ROOT_DIR "/golden/goldens.txt"
#define PREVIEW JK_ROOT_DIR "/build/preview"

typedef struct Shot { char name[64]; char sha[65]; } Shot;

static int load(Shot *list, int cap)
{
    FILE *f = fopen(GOLDENS, "r");
    if (!f) return 0;
    int n = 0;
    while (n < cap && fscanf(f, "%63s %64s", list[n].name, list[n].sha) == 2) n++;
    fclose(f);
    return n;
}

static const char *find(const Shot *list, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i].name, name)) return list[i].sha;
    return NULL;
}

int main(void)
{
    const char *upd = getenv("JK_UPDATE_GOLDEN");
    int update = upd && !strcmp(upd, "1");
    mkdir(JK_ROOT_DIR "/build", 0755);
    mkdir(PREVIEW, 0755);
    Shot old[128], now[128];
    int n_old = load(old, 128), n = 0;

    JkFrame f = { calloc((size_t)JK_W * JK_H, 4), JK_W, JK_H, JK_W * 4 };
    static JkCase cases[JK_CASES_MAX];
    int nc = jk_cases(cases);
    for (int i = 0; i <= nc + 1; i++) {
        const char *name;
        if (i < nc) { jk_screen_draw(&f, &cases[i].m); name = cases[i].name; }
        else if (i == nc) { jk_loader_draw(&f, 0); name = "loader-0"; }
        else { jk_loader_draw(&f, 1500); name = "loader-1500"; }
        snprintf(now[n].name, sizeof now[n].name, "%s", name);
        jk_sha256_hex(f.pixels, (size_t)JK_W * JK_H * 4, now[n].sha);
        char png[512];
        snprintf(png, sizeof png, PREVIEW "/%s.png", name);
        jk_png_write(png, &f);
        const char *want = find(old, n_old, name);
        if (!update) {
            if (!want) fprintf(stderr, "    %s: no golden (run with JK_UPDATE_GOLDEN=1)\n", name);
            else if (strcmp(want, now[n].sha)) fprintf(stderr, "    %s: differs from golden (see %s)\n", name, png);
            CHECK(want && !strcmp(want, now[n].sha));
        }
        n++;
    }
    if (update) {
        FILE *o = fopen(GOLDENS, "w");
        CHECK(o != NULL);
        for (int i = 0; o && i < n; i++) fprintf(o, "%s %s\n", now[i].name, now[i].sha);
        if (o) fclose(o);
        fprintf(stderr, "    wrote %d goldens\n", n);
    } else {
        CHECK_EQ(n_old, n);                       /* no stale golden left behind */
    }
    free(f.pixels);
    jk_loader_release();
    jk_art_release();
    return jk_test_report("test_golden");
}
