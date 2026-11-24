/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* LZMA / LZMA2 decoder. See jk_lzma.h. */
#include "jk_lzma.h"

#include <stdlib.h>
#include <string.h>

#define SEG_BITS 22                      /* 4 MB dictionary pieces */
#define SEG_SIZE ((size_t)1 << SEG_BITS)
#define PROB_INIT 1024
#define NUM_STATES 12
#define POS_BITS_MAX 4

typedef uint16_t Prob;

typedef struct Dec {
    /* input */
    JkLzmaRead rd; void *rctx;
    unsigned char ibuf[65536]; size_t ipos, ilen;
    uint64_t in_left;                     /* LZMA2: bytes left in this chunk's packed data */
    int bounded;
    /* output */
    JkLzmaOut out; void *octx;
    unsigned char obuf[65536]; size_t olen;
    /* dictionary */
    unsigned char **seg; size_t nseg, wsize;
    uint64_t pos;                         /* total bytes produced */
    uint64_t dict_start;                  /* LZMA2 dictionary reset point */
    /* range coder */
    uint32_t range, code;
    int bad;
    /* model */
    unsigned lc, lp, pb;
    Prob *lit;  size_t lit_n;
    Prob is_match[NUM_STATES << POS_BITS_MAX], is_rep[NUM_STATES], is_rep_g0[NUM_STATES], is_rep_g1[NUM_STATES],
         is_rep_g2[NUM_STATES], is_rep0_long[NUM_STATES << POS_BITS_MAX];
    Prob pos_slot[4][64], pos_dec[115], align[16];
    struct LenDec { Prob choice, choice2, low[16][8], mid[16][8], high[256]; } len, rep_len;
    unsigned state;
    uint32_t rep0, rep1, rep2, rep3;
} Dec;

static int in_byte(Dec *d)
{
    if (d->bounded) {
        if (!d->in_left) { d->bad = 1; return 0; }
        d->in_left--;
    }
    if (d->ipos == d->ilen) {
        size_t cap = sizeof d->ibuf;
        d->ilen = d->rd(d->rctx, d->ibuf, cap);
        d->ipos = 0;
        if (!d->ilen) { d->bad = 1; return 0; }
    }
    return d->ibuf[d->ipos++];
}

static int flush(Dec *d)
{
    if (d->olen && d->out(d->octx, d->obuf, d->olen)) return -1;
    d->olen = 0;
    return 0;
}

static int put(Dec *d, unsigned char b)
{
    size_t i = (size_t)(d->pos % d->wsize);
    size_t s = i >> SEG_BITS;
    if (!d->seg[s]) {
        size_t sz = d->wsize - (s << SEG_BITS);
        if (sz > SEG_SIZE) sz = SEG_SIZE;
        d->seg[s] = malloc(sz);
        if (!d->seg[s]) return JK_LZMA_NOMEM;
    }
    d->seg[s][i & (SEG_SIZE - 1)] = b;
    d->pos++;
    d->obuf[d->olen++] = b;
    if (d->olen == sizeof d->obuf && flush(d)) return JK_LZMA_OUT_FAILED;
    return 0;
}

/* the byte dist+1 back */
static unsigned char get(const Dec *d, uint32_t dist)
{
    size_t i = (size_t)((d->pos - dist - 1) % d->wsize);
    return d->seg[i >> SEG_BITS][i & (SEG_SIZE - 1)];
}

static int dist_ok(const Dec *d, uint32_t dist)
{
    return (uint64_t)dist < d->pos - d->dict_start && (uint64_t)dist < d->wsize;
}

static void rc_init(Dec *d)
{
    d->range = 0xFFFFFFFFu;
    d->code = 0;
    if (in_byte(d) != 0) d->bad = 1;
    for (int i = 0; i < 4; i++) d->code = (d->code << 8) | (uint32_t)in_byte(d);
    if (d->code == d->range) d->bad = 1;
}

static inline void norm(Dec *d)
{
    if (d->range < (1u << 24)) {
        d->range <<= 8;
        d->code = (d->code << 8) | (uint32_t)in_byte(d);
    }
}

static inline unsigned bit(Dec *d, Prob *p)
{
    uint32_t bound = (d->range >> 11) * *p;
    unsigned b;
    if (d->code < bound) {
        *p = (Prob)(*p + ((2048 - *p) >> 5));
        d->range = bound;
        b = 0;
    } else {
        *p = (Prob)(*p - (*p >> 5));
        d->code -= bound;
        d->range -= bound;
        b = 1;
    }
    norm(d);
    return b;
}

static uint32_t direct(Dec *d, unsigned n)
{
    uint32_t r = 0;
    while (n--) {
        d->range >>= 1;
        uint32_t b = d->code >= d->range;
        if (b) d->code -= d->range;
        r = (r << 1) | b;
        norm(d);
    }
    return r;
}

static unsigned tree(Dec *d, Prob *p, unsigned bits)
{
    unsigned m = 1;
    for (unsigned i = 0; i < bits; i++) m = (m << 1) | bit(d, &p[m]);
    return m - (1u << bits);
}

static unsigned rtree(Dec *d, Prob *p, unsigned bits)
{
    unsigned m = 1, s = 0;
    for (unsigned i = 0; i < bits; i++) {
        unsigned b = bit(d, &p[m]);
        m = (m << 1) | b;
        s |= b << i;
    }
    return s;
}

static void probs_init(Prob *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = PROB_INIT; }

static void len_init(struct LenDec *l)
{
    l->choice = l->choice2 = PROB_INIT;
    probs_init(&l->low[0][0], sizeof l->low / sizeof(Prob));
    probs_init(&l->mid[0][0], sizeof l->mid / sizeof(Prob));
    probs_init(l->high, 256);
}

static unsigned len_dec(Dec *d, struct LenDec *l, unsigned ps)
{
    if (!bit(d, &l->choice)) return tree(d, l->low[ps], 3);
    if (!bit(d, &l->choice2)) return 8 + tree(d, l->mid[ps], 3);
    return 16 + tree(d, l->high, 8);
}

static int set_props(Dec *d, unsigned b)
{
    if (b >= 9 * 5 * 5) return -1;
    d->lc = b % 9; b /= 9;
    d->lp = b % 5;
    d->pb = b / 5;
    if (d->pb > POS_BITS_MAX) return -1;
    size_t n = (size_t)0x300 << (d->lc + d->lp);
    if (n != d->lit_n) {
        free(d->lit);
        d->lit = malloc(n * sizeof(Prob));
        if (!d->lit) return -2;
        d->lit_n = n;
    }
    return 0;
}

static void state_reset(Dec *d)
{
    probs_init(d->lit, d->lit_n);
    probs_init(d->is_match, sizeof d->is_match / sizeof(Prob));
    probs_init(d->is_rep, NUM_STATES);
    probs_init(d->is_rep_g0, NUM_STATES);
    probs_init(d->is_rep_g1, NUM_STATES);
    probs_init(d->is_rep_g2, NUM_STATES);
    probs_init(d->is_rep0_long, sizeof d->is_rep0_long / sizeof(Prob));
    probs_init(&d->pos_slot[0][0], sizeof d->pos_slot / sizeof(Prob));
    probs_init(d->pos_dec, 115);
    probs_init(d->align, 16);
    len_init(&d->len);
    len_init(&d->rep_len);
    d->state = 0;
    d->rep0 = d->rep1 = d->rep2 = d->rep3 = 0;
}

static int dec_init(Dec *d, uint32_t dict, uint64_t total)
{
    uint64_t w = dict < 4096 ? 4096 : dict;
    if (total && total < w) w = total;
    if (w < 1) w = 1;
    d->wsize = (size_t)w;
    d->nseg = (d->wsize + SEG_SIZE - 1) >> SEG_BITS;
    d->seg = calloc(d->nseg, sizeof *d->seg);
    return d->seg ? 0 : -1;
}

static void dec_free(Dec *d)
{
    for (size_t i = 0; d->seg && i < d->nseg; i++) free(d->seg[i]);
    free(d->seg);
    free(d->lit);
}

/* Decodes until `want` more bytes are out (or the end marker when allow_end).
 * 0 ok, 1 end marker seen, <0 error. */
static int lzma_run(Dec *d, uint64_t want, int allow_end)
{
    uint64_t end = d->pos + want;
    unsigned pbmask = (1u << d->pb) - 1, lpmask = (1u << d->lp) - 1;
    while (d->pos < end) {
        if (d->bad) return JK_LZMA_DAMAGED;
        unsigned ps = (unsigned)d->pos & pbmask;
        unsigned st = d->state;
        if (!bit(d, &d->is_match[(st << POS_BITS_MAX) + ps])) {
            unsigned prev = d->pos > d->dict_start ? get(d, 0) : 0;
            Prob *probs = &d->lit[(size_t)0x300 * ((((unsigned)d->pos & lpmask) << d->lc) + (prev >> (8 - d->lc)))];
            unsigned sym = 1;
            if (st >= 7) {
                if (!dist_ok(d, d->rep0)) return JK_LZMA_DAMAGED;
                unsigned mb = get(d, d->rep0);
                do {
                    unsigned m = (mb >> 7) & 1;
                    mb <<= 1;
                    unsigned b = bit(d, &probs[((1 + m) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (m != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | bit(d, &probs[sym]);
            int r = put(d, (unsigned char)sym);
            if (r) return r;
            d->state = st < 4 ? 0 : st < 10 ? st - 3 : st - 6;
            continue;
        }
        unsigned len;
        if (bit(d, &d->is_rep[st])) {
            if (d->pos == d->dict_start) return JK_LZMA_DAMAGED;
            if (!bit(d, &d->is_rep_g0[st])) {
                if (!bit(d, &d->is_rep0_long[(st << POS_BITS_MAX) + ps])) {
                    if (!dist_ok(d, d->rep0)) return JK_LZMA_DAMAGED;
                    d->state = st < 7 ? 9 : 11;
                    int r = put(d, get(d, d->rep0));
                    if (r) return r;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!bit(d, &d->is_rep_g1[st])) {
                    dist = d->rep1;
                } else {
                    if (!bit(d, &d->is_rep_g2[st])) dist = d->rep2;
                    else { dist = d->rep3; d->rep3 = d->rep2; }
                    d->rep2 = d->rep1;
                }
                d->rep1 = d->rep0;
                d->rep0 = dist;
            }
            len = len_dec(d, &d->rep_len, ps);
            d->state = st < 7 ? 8 : 11;
        } else {
            d->rep3 = d->rep2; d->rep2 = d->rep1; d->rep1 = d->rep0;
            len = len_dec(d, &d->len, ps);
            d->state = st < 7 ? 7 : 10;
            unsigned ls = len < 3 ? len : 3;
            unsigned slot = tree(d, d->pos_slot[ls], 6);
            uint32_t dist;
            if (slot < 4) {
                dist = slot;
            } else {
                unsigned nb = (slot >> 1) - 1;
                dist = (2u | (slot & 1)) << nb;
                if (slot < 14) dist += rtree(d, &d->pos_dec[dist - slot], nb);
                else dist += (direct(d, nb - 4) << 4) + rtree(d, d->align, 4);
            }
            if (dist == 0xFFFFFFFFu) {
                if (!allow_end) return JK_LZMA_DAMAGED;
                return d->bad ? JK_LZMA_DAMAGED : 1;
            }
            d->rep0 = dist;
        }
        len += 2;
        if (!dist_ok(d, d->rep0)) return JK_LZMA_DAMAGED;
        if (d->pos + len > end) return JK_LZMA_DAMAGED;
        while (len--) {
            int r = put(d, get(d, d->rep0));
            if (r) return r;
        }
    }
    return d->bad ? JK_LZMA_DAMAGED : 0;
}

static Dec *dec_new(JkLzmaRead rd, void *rctx, JkLzmaOut out, void *octx)
{
    Dec *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->rd = rd; d->rctx = rctx; d->out = out; d->octx = octx;
    return d;
}

int jk_lzma_decode(const unsigned char props[5], uint64_t unpack_size, JkLzmaRead rd, void *rctx,
                   JkLzmaOut out, void *octx)
{
    Dec *d = dec_new(rd, rctx, out, octx);
    if (!d) return JK_LZMA_NOMEM;
    uint32_t dict = (uint32_t)props[1] | (uint32_t)props[2] << 8 | (uint32_t)props[3] << 16 | (uint32_t)props[4] << 24;
    int r = set_props(d, props[0]);
    if (r == 0 && dec_init(d, dict, unpack_size) < 0) r = -2;
    if (r < 0) { dec_free(d); free(d); return r == -2 ? JK_LZMA_NOMEM : JK_LZMA_UNSUPPORTED; }
    state_reset(d);
    rc_init(d);
    r = lzma_run(d, unpack_size, 1);
    if (r == 1) r = 0;
    if (r == 0 && d->pos != unpack_size) r = JK_LZMA_DAMAGED;
    if (r == 0 && flush(d)) r = JK_LZMA_OUT_FAILED;
    dec_free(d);
    free(d);
    return r;
}

int jk_lzma2_decode(unsigned char prop, uint64_t unpack_size, JkLzmaRead rd, void *rctx, JkLzmaOut out, void *octx)
{
    if (prop > 40) return JK_LZMA_UNSUPPORTED;
    uint32_t dict = prop == 40 ? 0xFFFFFFFFu : (2u | (prop & 1)) << (prop / 2 + 11);
    Dec *d = dec_new(rd, rctx, out, octx);
    if (!d) return JK_LZMA_NOMEM;
    int r = 0, need_props = 1, need_dict_reset = 1;
    if (dec_init(d, dict, unpack_size) < 0) { free(d); return JK_LZMA_NOMEM; }
    for (;;) {
        d->bounded = 0;
        int c = in_byte(d);
        if (d->bad) { r = JK_LZMA_DAMAGED; break; }
        if (c == 0) break;
        if (c == 1 || c == 2) {
            if (c == 1) { d->dict_start = d->pos; need_dict_reset = 0; }
            else if (need_dict_reset) { r = JK_LZMA_DAMAGED; break; }
            unsigned n = ((unsigned)in_byte(d) << 8) | (unsigned)in_byte(d);
            n += 1;
            while (n-- && !d->bad) if ((r = put(d, (unsigned char)in_byte(d))) != 0) break;
            if (r) break;
            if (d->bad) { r = JK_LZMA_DAMAGED; break; }
            continue;
        }
        if (c < 0x80) { r = JK_LZMA_DAMAGED; break; }
        unsigned mode = (c >> 5) & 3;
        uint64_t usize = (((uint64_t)c & 0x1F) << 16) + ((uint64_t)in_byte(d) << 8) + (uint64_t)in_byte(d) + 1;
        uint64_t psize = ((uint64_t)in_byte(d) << 8) + (uint64_t)in_byte(d) + 1;
        if (mode == 3) { d->dict_start = d->pos; need_dict_reset = 0; }
        else if (need_dict_reset) { r = JK_LZMA_DAMAGED; break; }
        if (mode >= 2) {
            int p = in_byte(d);
            int s = set_props(d, (unsigned)p);
            if (s < 0) { r = s == -2 ? JK_LZMA_NOMEM : JK_LZMA_DAMAGED; break; }
            need_props = 0;
        } else if (need_props) { r = JK_LZMA_DAMAGED; break; }
        if (mode >= 1) state_reset(d);
        d->bounded = 1;
        d->in_left = psize;
        rc_init(d);
        r = lzma_run(d, usize, 0);
        if (r) break;
        if (d->in_left) { r = JK_LZMA_DAMAGED; break; }        /* the chunk must use exactly its bytes */
        if (d->pos > unpack_size) { r = JK_LZMA_DAMAGED; break; }
    }
    if (r == 0 && d->pos != unpack_size) r = JK_LZMA_DAMAGED;
    if (r == 0 && flush(d)) r = JK_LZMA_OUT_FAILED;
    dec_free(d);
    free(d);
    return r;
}
