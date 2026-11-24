/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Console test hook for the launcher phase (jk_devhook.h). The parser and the
 * status/ack documents follow the bench remote-control runtime byte
 * for byte, so the same PC client logic drives the launcher and the game.
 */
#include "jk_devhook.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "jk_fs.h"

enum { DH_NONE, DH_PAD, DH_RELEASE, DH_SNAP, DH_STATE, DH_WRONG_PHASE };

/* ---- buttons ---- */

uint32_t jk_devhook_buttons(unsigned x)
{
    static const struct { unsigned xinput; uint32_t pad; } map[] = {
        { 1, JK_PAD_UP }, { 2, JK_PAD_DOWN }, { 4, JK_PAD_LEFT }, { 8, JK_PAD_RIGHT },
        { 16, JK_PAD_OPTIONS }, { 64, JK_PAD_L3 }, { 128, JK_PAD_R3 },
        { 256, JK_PAD_L1 }, { 512, JK_PAD_R1 },
        { 4096, JK_PAD_CROSS }, { 8192, JK_PAD_CIRCLE }, { 16384, JK_PAD_SQUARE }, { 32768, JK_PAD_TRIANGLE },
    };
    uint32_t out = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (x & map[i].xinput) out |= map[i].pad;
    return out;
}

uint32_t jk_devhook_stick(int lx, int ly)
{
    uint32_t out = 0;
    if (ly > 16384) out |= JK_PAD_UP;
    if (ly < -16384) out |= JK_PAD_DOWN;
    if (lx < -16384) out |= JK_PAD_LEFT;
    if (lx > 16384) out |= JK_PAD_RIGHT;
    return out;
}

/* ---- PNG (RGB, stored deflate: no compressor on the console) ---- */

static uint32_t crc_table[256];

static uint32_t crc32_of(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* Streams the PNG to f, one row at a time (no 6 MB buffer). */
int jk_devhook_png(const char *path, const JkFrame *fr)
{
    if (!path || !fr || !fr->pixels || !fr->width || !fr->height) return -1;
    char tmp[600];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    FILE *o = fopen(tmp, "wb");
    if (!o) return -1;
    const size_t row = (size_t)fr->width * 3 + 1, raw_n = row * fr->height;
    const size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *line = malloc(row);
    int ok = line != NULL && z_n < 0x7fffffffu;
    uint8_t hdr[8 + 25];
    memcpy(hdr, "\x89PNG\r\n\x1a\n", 8);
    be32(hdr + 8, 13); memcpy(hdr + 12, "IHDR", 4);
    be32(hdr + 16, fr->width); be32(hdr + 20, fr->height);
    hdr[24] = 8; hdr[25] = 2; hdr[26] = hdr[27] = hdr[28] = 0;
    be32(hdr + 29, crc32_of(0, hdr + 12, 17));
    ok = ok && fwrite(hdr, 1, sizeof hdr, o) == sizeof hdr;
    uint8_t ih[10];
    be32(ih, (uint32_t)z_n); memcpy(ih + 4, "IDAT", 4);
    ih[8] = 0x78; ih[9] = 0x01;                       /* zlib header, no compression */
    ok = ok && fwrite(ih, 1, 10, o) == 10;
    uint32_t crc = crc32_of(0, ih + 4, 6), s1 = 1, s2 = 0;
    size_t in_block = 65535, left = raw_n;            /* bytes left in the current stored block */
    for (uint32_t y = 0; ok && y < fr->height; y++) {
        const uint32_t *src = (const uint32_t *)(const void *)(fr->pixels + (size_t)y * fr->stride);
        uint8_t *d = line;
        *d++ = 0;
        for (uint32_t x = 0; x < fr->width; x++) {
            *d++ = (uint8_t)(src[x] >> 16); *d++ = (uint8_t)(src[x] >> 8); *d++ = (uint8_t)src[x];
        }
        for (size_t at = 0; ok && at < row;) {
            if (in_block == 65535) {                  /* start a stored block */
                size_t n = left < 65535 ? left : 65535;
                uint8_t bh[5] = { (uint8_t)(n == left), (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)~n, (uint8_t)(~n >> 8) };
                ok = fwrite(bh, 1, 5, o) == 5;
                crc = crc32_of(crc, bh, 5);
                in_block = 65535 - n;                 /* counts up to 65535 */
            }
            size_t take = 65535 - in_block;
            if (take > row - at) take = row - at;
            ok = ok && fwrite(line + at, 1, take, o) == take;
            crc = crc32_of(crc, line + at, take);
            for (size_t i = 0; i < take; i++) { s1 = (s1 + line[at + i]) % 65521u; s2 = (s2 + s1) % 65521u; }
            at += take; in_block += take; left -= take;
        }
    }
    uint8_t tail[4 + 4 + 12];
    be32(tail, s2 << 16 | s1);
    crc = crc32_of(crc, tail, 4);
    be32(tail + 4, crc);
    be32(tail + 8, 0); memcpy(tail + 12, "IEND", 4); be32(tail + 16, crc32_of(0, (const uint8_t *)"IEND", 4));
    ok = ok && fwrite(tail, 1, sizeof tail, o) == sizeof tail;
    free(line);
    if (fclose(o) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

/* ---- protocol files ---- */

static int path_of(const JkDevHook *h, char *out, size_t cap, const char *leaf)
{
    int n = snprintf(out, cap, "%s/%s", h->dir, leaf);
    return n > 0 && (size_t)n < cap;
}

/* JSON string body: quotes, backslashes and control characters escaped. */
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static FILE *open_tmp(const JkDevHook *h, const char *leaf, char *path, char *tmp)
{
    if (!path_of(h, path, 600, leaf)) return NULL;
    snprintf(tmp, 610, "%s.tmp", path);
    return fopen(tmp, "wb");
}

static int close_tmp(FILE *f, int ok, const char *path, const char *tmp)
{
    if (fclose(f) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return 0; }
    return 1;
}

static int publish_status(JkDevHook *h)
{
    char path[600], tmp[610];
    FILE *f = open_tmp(h, "status.json", path, tmp);
    if (!f) return 0;
    int ok = fprintf(f, "{\"version\":1,\"session\":\"%s\",\"phase\":\"launcher\",\"profile\":\"\",\"last_seq\":%u,"
                        "\"result\":\"%s\",\"active\":%s,\"held\":%s,\"screen\":\"%s\",\"frames\":%u}\n",
                     h->session, h->seq, h->result, h->enabled ? "true" : "false", h->expires ? "true" : "false",
                     h->screen, h->frames) >= 0;
    return close_tmp(f, ok, path, tmp);
}

static int ack(JkDevHook *h, const char *result)
{
    char path[600], tmp[610];
    h->result = result;
    FILE *f = open_tmp(h, "ack.json", path, tmp);
    int ok = 0;
    if (f) {
        ok = fprintf(f, "{\"version\":1,\"session\":\"%s\",\"seq\":%u,\"result\":\"%s\"", h->session, h->seq, result) >= 0;
        if (h->ack_file[0]) ok = ok && fprintf(f, ",\"file\":\"%s\"", h->ack_file) >= 0;
        ok = ok && fprintf(f, "}\n") >= 0;
        ok = close_tmp(f, ok, path, tmp);
    }
    h->ack_file[0] = 0;
    (void)publish_status(h);
    if (h->inner.log) {
        char line[128];
        snprintf(line, sizeof line, "devtools seq=%u result=%s", h->seq, result);
        h->inner.log(h->inner.ctx, line);
    }
    return ok;
}

static const char *const home_names[] = { "play", "patch", "settings" };

static int write_state(JkDevHook *h)
{
    char path[600], tmp[610];
    FILE *f = open_tmp(h, "state.json", path, tmp);
    if (!f) return 0;
    const JkModel *m = jk_frontend_model();
    int ok = fprintf(f, "{\"version\":1,\"session\":\"%s\",\"seq\":%u,\"screen\":\"%s\",\"frames\":%u",
                     h->session, h->seq, h->screen, h->frames) >= 0;
    if (m) {
        int focused = m->screen == JK_SCR_HOME ? m->focus : m->screen == JK_SCR_SETTINGS ? m->set_focus
                    : m->screen == JK_SCR_MODS ? m->mods_focus : m->screen == JK_SCR_SAVES ? m->save_pick : -1;
        fprintf(f, ",\"screen_id\":%d,\"dialog\":%d,\"focus\":{\"home\":%d,\"settings\":%d,\"mods\":%d,\"save_pick\":%d,"
                   "\"credits_top\":%d},\"focused\":",
                (int)m->screen, m->dialog, m->focus, m->set_focus, m->mods_focus, m->save_pick, m->credits_top);
        if (m->screen == JK_SCR_HOME && m->focus >= 0 && m->focus < 3) json_str(f, home_names[m->focus]);
        else fprintf(f, "%d", focused);
        fprintf(f, ",\"pace\":%d,\"patch_phase\":%d,\"check\":%d,\"usb\":%d,\"msg\":", (int)m->pace, (int)m->phase,
                (int)m->check.state, m->usb);
        json_str(f, m->msg);
        fputs(",\"mods_msg\":", f); json_str(f, m->mods_msg);
        fputs(",\"saves_msg\":", f); json_str(f, m->saves_msg);
    }
    fputs(",\"messages\":[", f);
    unsigned n = h->line_n < JK_DEVHOOK_LINES ? h->line_n : JK_DEVHOOK_LINES;
    for (unsigned i = 0; i < n; i++) {
        if (i) fputc(',', f);
        json_str(f, h->lines[(h->line_n - n + i) % JK_DEVHOOK_LINES]);
    }
    ok = fprintf(f, "]}\n") >= 0 && ok;
    return close_tmp(f, ok, path, tmp);
}

/* ---- command parsing (the bench runtime's rules: no state change on bad input) ---- */

static int number(const char *s, long lo, long hi, long *v)
{
    char *end;
    if (!*s || *s == '+' || (lo >= 0 && *s == '-')) return 0;
    errno = 0;
    long n = strtol(s, &end, 0);
    if (errno || *end || n < lo || n > hi) return 0;
    *v = n;
    return 1;
}

static int parse(JkDevHook *h, char *line, uint64_t now)
{
    char *tok[13], *save = NULL, *s = strtok_r(line, " \t\r\n", &save);
    unsigned n = 0;
    while (s && n < 13) { tok[n++] = s; s = strtok_r(NULL, " \t\r\n", &save); }
    long seq, v[8];
    if (s || n < 4 || strcmp(tok[0], "PWCTRL1") || strcmp(tok[1], h->session) ||
        !number(tok[2], 1, 4294967295L, &seq) || (uint32_t)seq <= h->seq) return DH_NONE;
    int action = DH_NONE;
    if (n == 4 && !strcmp(tok[3], "RELEASE")) action = DH_RELEASE;
    else if (n == 4 && !strcmp(tok[3], "SNAP")) action = DH_SNAP;
    else if (n == 4 && !strcmp(tok[3], "STATE")) action = DH_STATE;
    else if ((n == 4 && !strcmp(tok[3], "CLOSE")) || (n == 5 && !strcmp(tok[3], "LAUNCH"))) action = DH_WRONG_PHASE;
    else if (n == 12 && !strcmp(tok[3], "PAD")) {
        for (unsigned i = 0; i < 8; i++) {
            long lo = i >= 3 && i <= 6 ? -32768 : i == 7 ? 1 : 0;
            long hi = i == 0 ? 65535 : i < 3 ? 255 : i == 7 ? 5000 : 32767;
            if (!number(tok[4 + i], lo, hi, &v[i])) return DH_NONE;
        }
        if (v[0] & ~0xf3ffL) return DH_NONE;          /* Guide and the reserved bit */
        action = DH_PAD;
    }
    if (!action) return DH_NONE;
    h->seq = (uint32_t)seq;
    if (action == DH_PAD) {
        h->inj_held = jk_devhook_buttons((unsigned)v[0]) | jk_devhook_stick((int)v[3], (int)v[4]);
        h->expires = now + (uint64_t)v[7] * 1000000u;
        if (!h->expires) h->expires = 1;
    } else if (action == DH_RELEASE || action == DH_WRONG_PHASE) {
        h->inj_held = 0; h->expires = 0;
    }
    return action;
}

static int enable_present(const JkDevHook *h)
{
    char p[600];
    struct stat st;
    return path_of(h, p, sizeof p, "enable") && stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* One look at command.txt (at most every 100 ms). Returns the action taken. */
static int poll_commands(JkDevHook *h, uint64_t now)
{
    if (now < h->poll_at) return DH_NONE;
    h->poll_at = now + 100000000u;
    if (!enable_present(h)) {
        if (h->enabled) { h->enabled = 0; h->inj_held = 0; h->expires = 0; (void)publish_status(h); }
        return DH_NONE;
    }
    if (!h->enabled) { h->enabled = 1; h->status_at = 0; }
    if (now >= h->status_at) { (void)publish_status(h); h->status_at = now + 1000000000u; }
    char path[600], text[514];
    if (!path_of(h, path, sizeof path, "command.txt")) return DH_NONE;
    FILE *f = fopen(path, "rb");
    if (!f) return DH_NONE;
    size_t got = fread(text, 1, 513, f);
    int bad = ferror(f);
    fclose(f);
    if (bad || !got || got > 512 || memchr(text, 0, got)) return DH_NONE;
    text[got] = 0;
    int action = parse(h, text, now);
    switch (action) {
    case DH_PAD: case DH_RELEASE: (void)ack(h, "accepted"); break;
    case DH_WRONG_PHASE: (void)ack(h, "wrong_phase"); break;
    case DH_STATE: (void)ack(h, write_state(h) ? "accepted" : "write_failed"); break;
    case DH_SNAP:
        if (!h->frame) { (void)ack(h, "no_frame"); break; }
        snprintf(h->ack_file, sizeof h->ack_file, "snap-%u.png", h->seq);
        if (!path_of(h, path, sizeof path, h->ack_file) || jk_devhook_png(path, h->frame) != 0) {
            h->ack_file[0] = 0;
            (void)ack(h, "write_failed");
        } else {
            (void)ack(h, "accepted");
        }
        break;
    default: break;
    }
    return action;
}

/* ---- the wrapped callbacks ---- */

static int hook_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    JkDevHook *h = ctx;
    uint32_t rh = 0, rp = 0;
    int rc = h->inner.pad ? h->inner.pad(h->inner.ctx, &rh, &rp) : -1;
    if (rc < 0) rh = rp = 0;
    uint64_t now = h->inner.now_ns(h->inner.ctx);
    if (h->expires && now >= h->expires) { h->inj_held = 0; h->expires = 0; }
    (void)poll_commands(h, now);
    uint32_t inj = h->expires ? h->inj_held : 0;
    *held = rh | inj;
    *pressed = rp | (inj & ~h->inj_prev & ~rh);     /* a new edge, as the real pad gives */
    h->inj_prev = inj;
    if (rc < 0 && !inj) { *held = *pressed = 0; return rc; }
    return 0;
}

static int hook_present(void *ctx, const JkFrame *frame)
{
    JkDevHook *h = ctx;
    h->frame = frame;
    h->frames++;
    return h->inner.present(h->inner.ctx, frame);
}

static void hook_screen(void *ctx, const char *name)
{
    JkDevHook *h = ctx;
    snprintf(h->screen, sizeof h->screen, "%s", name ? name : "");
    if (h->inner.screen) h->inner.screen(h->inner.ctx, name);
}

static void hook_log(void *ctx, const char *line)
{
    JkDevHook *h = ctx;
    snprintf(h->lines[h->line_n % JK_DEVHOOK_LINES], sizeof h->lines[0], "%s", line ? line : "");
    h->line_n++;
    if (h->inner.log) h->inner.log(h->inner.ctx, line);
}

static int hook_quit(void *ctx) { JkDevHook *h = ctx; return h->inner.should_quit(h->inner.ctx); }
static uint64_t hook_now(void *ctx) { JkDevHook *h = ctx; return h->inner.now_ns(h->inner.ctx); }
static void hook_sleep(void *ctx, uint32_t us) { JkDevHook *h = ctx; h->inner.sleep_us(h->inner.ctx, us); }
static int hook_wall(void *ctx, char *out, size_t cap) { JkDevHook *h = ctx; return h->inner.wall_time(h->inner.ctx, out, cap); }
static long long hook_free(void *ctx, const char *path) { JkDevHook *h = ctx; return h->inner.free_bytes(h->inner.ctx, path); }

int jk_devhook_attach(JkDevHook *h, JkServices *s)
{
    if (!h || !s || !s->data_root || !s->present || !s->now_ns || !s->sleep_us) return -1;
    memset(h, 0, sizeof *h);
    /* r4: <product_root>/devtools (/data/Jk4Speed/devtools); data_root is System/ */
    const char *root = s->product_root ? s->product_root : s->data_root;
    if (snprintf(h->dir, sizeof h->dir, "%s/devtools", root) >= (int)sizeof h->dir) return -1;
    if (!enable_present(h)) { h->dir[0] = 0; return 0; }
    h->inner = *s;
    h->result = "ready";
    snprintf(h->session, sizeof h->session, "jk-%lu-%llu", (unsigned long)getpid(),
             (unsigned long long)s->now_ns(s->ctx));
    snprintf(h->screen, sizeof h->screen, "-");
    s->ctx = h;
    s->pad = hook_pad;
    s->present = hook_present;
    s->screen = hook_screen;
    s->log = hook_log;
    s->now_ns = hook_now;
    s->sleep_us = hook_sleep;
    s->should_quit = h->inner.should_quit ? hook_quit : NULL;
    s->wall_time = h->inner.wall_time ? hook_wall : NULL;
    s->free_bytes = h->inner.free_bytes ? hook_free : NULL;
    if (h->inner.log) {
        char line[700];
        snprintf(line, sizeof line, "devtools hook on: %s session=%s", h->dir, h->session);
        h->inner.log(h->inner.ctx, line);
    }
    return 1;
}
