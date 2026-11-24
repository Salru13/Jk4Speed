/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The launcher-phase console test hook (devtools/jk_devhook.h): off without
 * the enable file; with it, PAD/RELEASE/SNAP/STATE over the protocol-v1 files. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_devhook.h"
#include "jk_frontend.h"
#include "jk_fs.h"
#include "jk_inflate.h"
#include "jk_sha256.h"

typedef struct Fake {
    uint64_t t;
    int pad_rc;                 /* what the real pad returns */
    uint32_t real_held, real_pressed;
    int presents;
    /* full run: commands the "PC" sends, one after the previous ack */
    const char *const *cmds; int cmd_n, cmd_i, polls, quit_polls;
    JkDevHook *h;
    char screens[32][16]; int screens_n;
} Fake;

static int f_present(void *ctx, const JkFrame *f) { Fake *k = ctx; (void)f; k->presents++; return 0; }
static uint64_t f_now(void *ctx) { Fake *k = ctx; return k->t; }
static void f_sleep(void *ctx, uint32_t us) { Fake *k = ctx; k->t += (uint64_t)us * 1000u; }
static void f_screen(void *ctx, const char *name)
{
    Fake *k = ctx;
    if (k->screens_n < 32) snprintf(k->screens[k->screens_n++], 16, "%s", name);
}
static int f_quit(void *ctx) { Fake *k = ctx; return k->quit_polls && k->polls >= k->quit_polls; }

static char *slurp(const char *dir, const char *leaf)
{
    char p[700];
    snprintf(p, sizeof p, "%s/%s", dir, leaf);
    return jk_fs_read(p, 16 * 1024 * 1024, NULL);
}

static void send_cmd(JkDevHook *h, uint32_t seq, const char *verb)
{
    char line[256];
    snprintf(line, sizeof line, "PWCTRL1 %s %u %s\n", h->session, seq, verb);
    tmp_put(h->dir, "command.txt", line);
}

static int acked(JkDevHook *h, uint32_t seq, const char *result)
{
    char *a = slurp(h->dir, "ack.json"), want[160];
    snprintf(want, sizeof want, "\"seq\":%u,\"result\":\"%s\"", seq, result);
    int ok = a && strstr(a, want) && strstr(a, h->session);
    free(a);
    return ok;
}

static int f_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    Fake *k = ctx;
    *held = k->real_held; *pressed = k->real_pressed;
    k->real_pressed = 0;
    k->polls++;
    /* the PC side: the next command once the previous one is acknowledged */
    if (k->h && k->cmd_i < k->cmd_n && (k->cmd_i == 0 || acked(k->h, (uint32_t)k->cmd_i, "accepted"))) {
        send_cmd(k->h, (uint32_t)k->cmd_i + 1, k->cmds[k->cmd_i]);
        k->cmd_i++;
    }
    k->t += 20000000u;   /* each poll 20 ms later */
    return k->pad_rc;
}

typedef struct Env { char root[256], data[300], game[300], pkg[300]; } Env;

static void env_new(Env *e, Fake *k, JkServices *s, int enable)
{
    memset(e, 0, sizeof *e); memset(k, 0, sizeof *k); memset(s, 0, sizeof *s);
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->data, sizeof e->data, "%s/Jk4Speed", e->root);
    snprintf(e->game, sizeof e->game, "%s/Jk4Speed/NFSMW-X360", e->root);
    snprintf(e->pkg, sizeof e->pkg, "%s/pkg", e->root);
    mkdir(e->data, 0755);
    tmp_put(e->pkg, "manifest.txt", "exe speed.exe 0000000000000000000000000000000000000000000000000000000000000000\n");
    if (enable) tmp_put(e->data, "devtools/enable", "");
    k->t = 1000000000u;
    s->ctx = k; s->present = f_present; s->pad = f_pad; s->now_ns = f_now; s->sleep_us = f_sleep;
    s->screen = f_screen; s->should_quit = f_quit;
    s->game_dir = e->game; s->data_root = e->data; s->files_dir = e->pkg;
}

static void off_without_enable_file(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 0);
    JkServices before = s;
    CHECK_EQ(jk_devhook_attach(&h, &s), 0);
    CHECK(memcmp(&before, &s, sizeof s) == 0);
    char p[400]; struct stat st;
    snprintf(p, sizeof p, "%s/devtools", e.data);
    CHECK(stat(p, &st) != 0);                      /* nothing written for a player */
    tmp_rm_rf(e.root);
}

static void on_publishes_launcher_status(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    CHECK_EQ(jk_devhook_attach(&h, &s), 1);
    CHECK(s.ctx == &h);
    CHECK(h.inner.ctx == &k);
    uint32_t held, pressed;
    CHECK_EQ(s.pad(s.ctx, &held, &pressed), 0);
    char *st = slurp(h.dir, "status.json");
    CHECK(st && strstr(st, "\"version\":1"));
    CHECK(st && strstr(st, "\"phase\":\"launcher\""));
    CHECK(st && strstr(st, "\"active\":true"));
    CHECK(st && strstr(st, h.session));
    CHECK(h.session[0] != 0);
    free(st);
    tmp_rm_rf(e.root);
}

/* r4: data_root is /data/Jk4Speed/System, the hook folder stays
 * /data/Jk4Speed/devtools (product_root); a System/devtools/enable is not it. */
static void uses_product_root_devtools(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 0);
    char sys[400], want[400];
    snprintf(sys, sizeof sys, "%s/System", e.data);
    tmp_put(sys, "devtools/enable", "");
    s.data_root = sys;
    s.product_root = e.data;
    CHECK_EQ(jk_devhook_attach(&h, &s), 0);           /* System/devtools/enable: not the switch */
    tmp_put(e.data, "devtools/enable", "");
    CHECK_EQ(jk_devhook_attach(&h, &s), 1);
    snprintf(want, sizeof want, "%s/devtools", e.data);
    CHECK(!strcmp(h.dir, want));
    tmp_rm_rf(e.root);
}

static void maps_xinput_to_launcher_pad(void)
{
    CHECK_EQ(jk_devhook_buttons(4096), JK_PAD_CROSS);
    CHECK_EQ(jk_devhook_buttons(8192), JK_PAD_CIRCLE);
    CHECK_EQ(jk_devhook_buttons(16384), JK_PAD_SQUARE);
    CHECK_EQ(jk_devhook_buttons(32768), JK_PAD_TRIANGLE);
    CHECK_EQ(jk_devhook_buttons(1 | 2 | 4 | 8), JK_PAD_UP | JK_PAD_DOWN | JK_PAD_LEFT | JK_PAD_RIGHT);
    CHECK_EQ(jk_devhook_buttons(16), JK_PAD_OPTIONS);
    CHECK_EQ(jk_devhook_buttons(32), 0);           /* back/Share: nothing in the launcher */
    CHECK_EQ(jk_devhook_buttons(64 | 128), JK_PAD_L3 | JK_PAD_R3);
    CHECK_EQ(jk_devhook_buttons(256 | 512), JK_PAD_L1 | JK_PAD_R1);
    CHECK_EQ(jk_devhook_stick(0, 32767), JK_PAD_UP);
    CHECK_EQ(jk_devhook_stick(0, -32768), JK_PAD_DOWN);
    CHECK_EQ(jk_devhook_stick(-20000, 0), JK_PAD_LEFT);
    CHECK_EQ(jk_devhook_stick(20000, 0), JK_PAD_RIGHT);
    CHECK_EQ(jk_devhook_stick(8000, -8000), 0);    /* inside half travel */
}

static void pad_injects_one_edge_then_holds_until_expiry(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    s.pad(s.ctx, &held, &pressed);                 /* opens the session */
    send_cmd(&h, 1, "PAD 2 0 0 0 0 0 0 100");      /* XInput down, 100 ms */
    k.t += 200000000u;
    CHECK_EQ(s.pad(s.ctx, &held, &pressed), 0);
    CHECK_EQ(pressed, JK_PAD_DOWN);
    CHECK_EQ(held, JK_PAD_DOWN);
    CHECK(acked(&h, 1, "accepted"));
    k.t += 50000000u;                              /* still held: no second edge */
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    CHECK_EQ(held, JK_PAD_DOWN);
    k.t += 200000000u;                             /* expired: released */
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(held, 0);
    CHECK_EQ(pressed, 0);
    tmp_rm_rf(e.root);
}

static void pad_works_without_a_real_controller(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    k.pad_rc = -1;                                 /* nobody signed in */
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    CHECK_EQ(s.pad(s.ctx, &held, &pressed), -1);   /* nothing injected: the real result */
    send_cmd(&h, 1, "PAD 4096 0 0 0 0 0 0 100");
    k.t += 200000000u;
    CHECK_EQ(s.pad(s.ctx, &held, &pressed), 0);
    CHECK_EQ(pressed, JK_PAD_CROSS);
    tmp_rm_rf(e.root);
}

static void real_pad_still_works_alongside(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    k.real_held = k.real_pressed = JK_PAD_CIRCLE;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, JK_PAD_CIRCLE);
    tmp_rm_rf(e.root);
}

static void stale_foreign_and_bad_commands_are_ignored(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    s.pad(s.ctx, &held, &pressed);
    tmp_put(h.dir, "command.txt", "PWCTRL1 other-session 1 PAD 2 0 0 0 0 0 0 100\n");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    CHECK_EQ(h.seq, 0);
    send_cmd(&h, 1, "PAD 2 0 0 0 0 0 0 0");        /* duration 0 is out of range */
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    send_cmd(&h, 1, "PAD 65536 0 0 0 0 0 0 100"); /* mask out of range */
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    CHECK_EQ(h.seq, 0);
    send_cmd(&h, 1, "RELEASE");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK(acked(&h, 1, "accepted"));
    send_cmd(&h, 1, "PAD 2 0 0 0 0 0 0 100");      /* replayed seq */
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    send_cmd(&h, 2, "LAUNCH nfsmw-x360");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK(acked(&h, 2, "wrong_phase"));
    tmp_rm_rf(e.root);
}

static void removing_enable_stops_the_hook(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    s.pad(s.ctx, &held, &pressed);
    char p[700]; snprintf(p, sizeof p, "%s/enable", h.dir);
    unlink(p);
    send_cmd(&h, 1, "PAD 2 0 0 0 0 0 0 100");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK_EQ(pressed, 0);
    char *st = slurp(h.dir, "status.json");
    CHECK(st && strstr(st, "\"active\":false"));
    free(st);
    tmp_rm_rf(e.root);
}

/* decode our stored-deflate PNG back to RGB rows */
static unsigned char *png_rgb(const char *path, uint32_t *w, uint32_t *h)
{
    size_t n = 0;
    unsigned char *p = (unsigned char *)jk_fs_read(path, 32 * 1024 * 1024, &n);
    if (!p || n < 33 || memcmp(p, "\x89PNG\r\n\x1a\n", 8) || memcmp(p + 12, "IHDR", 4)) { free(p); return NULL; }
    *w = (uint32_t)p[16] << 24 | p[17] << 16 | p[18] << 8 | p[19];
    *h = (uint32_t)p[20] << 24 | p[21] << 16 | p[22] << 8 | p[23];
    size_t raw_n = ((size_t)*w * 3 + 1) * *h, at = 33, zn = 0;
    unsigned char *z = malloc(n), *raw = malloc(raw_n);
    while (at + 12 <= n) {
        uint32_t len = (uint32_t)p[at] << 24 | p[at + 1] << 16 | p[at + 2] << 8 | p[at + 3];
        if (!memcmp(p + at + 4, "IDAT", 4)) { memcpy(z + zn, p + at + 8, len); zn += len; }
        at += 12 + len;
    }
    size_t got = 0;
    int ok = zn > 6 && jk_inflate(z + 2, zn - 6, raw, raw_n, &got) == 0 && got == raw_n;
    free(z); free(p);
    if (!ok) { free(raw); return NULL; }
    return raw;
}

static void png_round_trips(void)
{
    char root[256], path[300];
    tmp_dir(root, sizeof root);
    snprintf(path, sizeof path, "%s/a.png", root);
    uint32_t px[5 * 3];
    for (int i = 0; i < 15; i++) px[i] = 0xff000000u | (uint32_t)(i * 16) << 16 | (uint32_t)(255 - i) << 8 | (uint32_t)i;
    JkFrame f = { (uint8_t *)px, 5, 3, 5 * 4 };
    CHECK_EQ(jk_devhook_png(path, &f), 0);
    uint32_t w = 0, h = 0;
    unsigned char *raw = png_rgb(path, &w, &h);
    CHECK(raw != NULL);
    CHECK_EQ(w, 5); CHECK_EQ(h, 3);
    if (raw) {
        CHECK_EQ(raw[0], 0);                                       /* filter none */
        CHECK_EQ(raw[1 + 3 * 2], 2 * 16); CHECK_EQ(raw[2 + 3 * 2], 253); CHECK_EQ(raw[3 + 3 * 2], 2);
        CHECK_EQ(raw[16 + 1 + 3 * 4], 9 * 16);                     /* row 1, pixel 4 = index 9 */
    }
    free(raw);
    tmp_rm_rf(root);
}

static void snap_before_any_frame_says_so(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    uint32_t held, pressed;
    s.pad(s.ctx, &held, &pressed);
    send_cmd(&h, 1, "SNAP");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    CHECK(acked(&h, 1, "no_frame"));
    tmp_rm_rf(e.root);
}

/* The PC drives the real launcher: Home -> DOWN -> CROSS opens PATCH; SNAP
 * saves what is on screen; STATE names the screen, focus and messages. */
static void drives_the_launcher_end_to_end(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    static const char *const cmds[] = {
        "SNAP",
        "PAD 2 0 0 0 0 0 0 100",       /* down: focus PATCH */
        "STATE",
        "PAD 4096 0 0 0 0 0 0 100",    /* cross: open PATCH */
        "SNAP",
        "STATE",
    };
    k.cmds = cmds; k.cmd_n = 6; k.h = &h; k.quit_polls = 400;
    CHECK_EQ(jk_devhook_attach(&h, &s), 1);
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&s, &d), 0);
    CHECK_EQ(k.cmd_i, 6);
    CHECK(acked(&h, 6, "accepted"));
    int saw_patch = 0;
    for (int i = 0; i < k.screens_n; i++) saw_patch |= !strcmp(k.screens[i], "patch");
    CHECK(saw_patch);                              /* the title's own screen callback still runs */
    char *st = slurp(h.dir, "state.json");
    CHECK(st && strstr(st, "\"screen\":\"patch\""));
    CHECK(st && strstr(st, "\"seq\":6"));
    CHECK(st && strstr(st, "\"messages\":["));
    CHECK(st && strstr(st, "\"focus\":"));
    free(st);
    /* the two snaps differ (home vs patch) and are full launcher frames */
    char p1[700], p5[700];
    snprintf(p1, sizeof p1, "%s/snap-1.png", h.dir);
    snprintf(p5, sizeof p5, "%s/snap-5.png", h.dir);
    uint32_t w = 0, hh = 0;
    unsigned char *a = png_rgb(p1, &w, &hh);
    CHECK(a != NULL); CHECK_EQ(w, JK_W); CHECK_EQ(hh, JK_H);
    unsigned char *b = png_rgb(p5, &w, &hh);
    CHECK(b != NULL);
    if (a && b) CHECK(memcmp(a, b, ((size_t)JK_W * 3 + 1) * JK_H) != 0);
    free(a); free(b);
    jk_frontend_release();
    tmp_rm_rf(e.root);
}

static void state_records_focus_after_down(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    static const char *const cmds[] = { "PAD 2 0 0 0 0 0 0 100", "STATE" };
    k.cmds = cmds; k.cmd_n = 2; k.h = &h; k.quit_polls = 200;
    jk_devhook_attach(&h, &s);
    JkDecision d;
    CHECK_EQ(jk_frontend_run(&s, &d), 0);
    char *st = slurp(h.dir, "state.json");
    CHECK(st && strstr(st, "\"screen\":\"home\""));
    CHECK(st && strstr(st, "\"focused\":\"patch\""));   /* Home: PLAY -> PATCH */
    free(st);
    jk_frontend_release();
    tmp_rm_rf(e.root);
}

static void state_keeps_last_log_lines(void)
{
    Env e; Fake k; JkServices s; static JkDevHook h;
    env_new(&e, &k, &s, 1);
    jk_devhook_attach(&h, &s);
    CHECK(s.log != NULL);                          /* the hook listens even when the title gave none */
    for (int i = 0; i < 20; i++) {
        char line[64]; snprintf(line, sizeof line, "line \"%d\"", i);
        s.log(s.ctx, line);
    }
    uint32_t held, pressed;
    s.pad(s.ctx, &held, &pressed);
    send_cmd(&h, 1, "STATE");
    k.t += 200000000u;
    s.pad(s.ctx, &held, &pressed);
    char *st = slurp(h.dir, "state.json");
    CHECK(st && strstr(st, "line \\\"19\\\""));    /* escaped */
    CHECK(st && strstr(st, "line \\\"4\\\""));     /* the last 16 */
    CHECK(st && !strstr(st, "line \\\"3\\\""));
    free(st);
    tmp_rm_rf(e.root);
}

int main(void)
{
    RUN(off_without_enable_file);
    RUN(on_publishes_launcher_status);
    RUN(uses_product_root_devtools);
    RUN(maps_xinput_to_launcher_pad);
    RUN(pad_injects_one_edge_then_holds_until_expiry);
    RUN(pad_works_without_a_real_controller);
    RUN(real_pad_still_works_alongside);
    RUN(stale_foreign_and_bad_commands_are_ignored);
    RUN(removing_enable_stops_the_hook);
    RUN(png_round_trips);
    RUN(snap_before_any_frame_says_so);
    RUN(drives_the_launcher_end_to_end);
    RUN(state_records_focus_after_down);
    RUN(state_keeps_last_log_lines);
    return jk_test_report("test_devhook");
}
