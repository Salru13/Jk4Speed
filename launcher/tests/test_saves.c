/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Save games: where they are, export to USB, import from USB (PC folder,
 * profile folders, zip), checked saves only, collisions, atomic swap with a
 * backup first (owner decisions 2026-11-23). Runs on temp folders with
 * synthetic saves in the v1.3 layout (jk_saves.h). */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_inflate.h"
#include "jk_md5.h"
#include "jk_saves.h"
#include "jk_savefx.h"
#include "jk_sha256.h"
#include "jk_strings.h"

typedef struct Env { char root[256], saves[400], usb[300], data[300]; } Env;

static void env_new(Env *e)
{
    tmp_dir(e->root, sizeof e->root);
    snprintf(e->saves, sizeof e->saves, "%s/Jk4Speed/Saves/NFS Most Wanted", e->root);
    snprintf(e->usb, sizeof e->usb, "%s/usb0", e->root);
    snprintf(e->data, sizeof e->data, "%s/data", e->root);
    mkdir(e->usb, 0755);
    mkdir(e->data, 0755);
}

static void put32(unsigned char *b, unsigned v) { for (int i = 0; i < 4; i++) b[i] = (unsigned char)(v >> (8 * i)); }


/* A stored zip (what any PC zip tool can make). */
static void put_zip(const char *path, const char *const *names, const unsigned char *const *data, const size_t *sizes, int n)
{
    FILE *f = fopen(path, "wb");
    static unsigned char cd[8192];
    size_t cn = 0, off = 0;
    for (int i = 0; i < n; i++) {
        unsigned char h[30] = { 'P', 'K', 3, 4, 20 };
        unsigned crc = jk_crc32(data[i], sizes[i]);
        size_t nl = strlen(names[i]);
        put32(h + 14, crc); put32(h + 18, (unsigned)sizes[i]); put32(h + 22, (unsigned)sizes[i]);
        h[26] = (unsigned char)nl;
        fwrite(h, 1, 30, f); fwrite(names[i], 1, nl, f); fwrite(data[i], 1, sizes[i], f);
        unsigned char c[46] = { 'P', 'K', 1, 2, 20, 0, 20 };
        put32(c + 16, crc); put32(c + 20, (unsigned)sizes[i]); put32(c + 24, (unsigned)sizes[i]);
        c[28] = (unsigned char)nl;
        put32(c + 42, (unsigned)off);
        memcpy(cd + cn, c, 46); memcpy(cd + cn + 46, names[i], nl);
        cn += 46 + nl;
        off += 30 + nl + sizes[i];
    }
    unsigned char end[22] = { 'P', 'K', 5, 6 };
    end[8] = end[10] = (unsigned char)n;
    put32(end + 12, (unsigned)cn); put32(end + 16, (unsigned)off);
    fwrite(cd, 1, cn, f); fwrite(end, 1, 22, f);
    fclose(f);
}

static void info_counts_profiles(void)
{
    Env e; env_new(&e);
    JkSavesInfo in;
    CHECK_EQ(jk_saves_info(e.saves, &in), 0);
    CHECK_EQ(in.exists, 0);
    mkdir(e.saves, 0755);
    tmp_put(e.root, "Jk4Speed/Saves/NFS Most Wanted/.keep", "");
    CHECK_EQ(jk_saves_info(e.saves, &in), 0);              /* the console today: there, empty */
    CHECK_EQ(in.exists, 1);
    CHECK_EQ(in.profiles, 0);
    tmp_put(e.saves, "RAZOR/RAZOR", "save one");
    tmp_put(e.saves, "JOKER/JOKER", "save two!");
    tmp_put(e.saves, "Settings.ini", "x");
    CHECK_EQ(jk_saves_info(e.saves, &in), 0);
    CHECK_EQ(in.profiles, 2);
    CHECK_EQ(in.files, 3);
    tmp_rm_rf(e.root);
}

static void save_check_layout(void)
{
    Env e; env_new(&e);
    char p[512];
    put_save(e.root, "GOATS/GOATS", "GOATS", 1);
    snprintf(p, sizeof p, "%s/GOATS/GOATS", e.root);
    CHECK_EQ(jk_save_check(p), 0);
    save_bytes("GOATS", 1);
    SAVE[30000] ^= 1;                                      /* body changed: MD5 fails */
    jk_fs_write_atomic(p, SAVE, (size_t)JK_SAVE_SIZE);
    CHECK(jk_save_check(p) < 0);
    jk_fs_write_atomic(p, save_bytes("GOATS", 1), (size_t)JK_SAVE_SIZE - 1);   /* another size */
    CHECK(jk_save_check(p) < 0);
    save_bytes("GOATS", 1);
    memcpy(SAVE, "MC03", 4);
    jk_fs_write_atomic(p, SAVE, (size_t)JK_SAVE_SIZE);
    CHECK(jk_save_check(p) < 0);
    tmp_put(e.root, "x/x", "a text file named like a save");
    snprintf(p, sizeof p, "%s/x/x", e.root);
    CHECK(jk_save_check(p) < 0);
    char d[16];
    jk_saves_date(1357171200LL, d);                         /* 2013-01-03 00:00 UTC */
    CHECK(!strcmp(d, "2013-01-03"));
    jk_saves_date(0, d);
    CHECK(!strcmp(d, "1970-01-01"));
    tmp_rm_rf(e.root);
}

static void export_copies_tree_to_usb(void)
{
    Env e; env_new(&e);
    put_save(e.saves, "RAZOR/RAZOR", "RAZOR", 7);
    char out[512], why[160] = "";
    CHECK_EQ(jk_saves_export(e.saves, e.usb, "2026-11-23_2100", out, sizeof out, why, sizeof why), 0);
    CHECK(same_save(e.usb, "Jk4Speed/saves/2026-11-23_2100/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 7));
    CHECK(tmp_exists(e.usb, "Jk4Speed/saves/2026-11-23_2100/jk4speed-export.txt"));
    put_save(e.saves, "RAZOR/RAZOR", "RAZOR", 8);
    CHECK_EQ(jk_saves_export(e.saves, e.usb, "2026-11-23_2100", out, sizeof out, why, sizeof why), 0);
    CHECK(same_save(e.usb, "Jk4Speed/saves/2026-11-23_2100/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 7));   /* never overwritten */
    CHECK(same_save(e.usb, "Jk4Speed/saves/2026-11-23_2100-2/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 8));
    tmp_rm_rf(e.root);
}

static void export_needs_saves_and_usb(void)
{
    Env e; env_new(&e);
    char out[512], why[160] = "";
    CHECK(jk_saves_export(e.saves, e.usb, NULL, out, sizeof out, why, sizeof why) < 0);
    CHECK(strstr(why, "no saves") != NULL);
    put_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1);
    CHECK(jk_saves_export(e.saves, NULL, NULL, out, sizeof out, why, sizeof why) < 0);
    CHECK(strstr(why, "USB") != NULL);
    tmp_rm_rf(e.root);
}

static int cand(const JkSavesFound *f, const char *alias)
{
    for (int i = 0; i < f->n; i++) if (!strcmp(f->c[i].alias, alias)) return i;
    return -1;
}

static void find_every_layout(void)
{
    Env e; env_new(&e);
    put_save(e.usb, "Jk4Speed/saves/NFS Most Wanted/ALPHA/ALPHA", "ALPHA", 1);    /* 1. the PC folder */
    tmp_put(e.usb, "Jk4Speed/saves/NFS Most Wanted/ALPHA/notes.txt", "a sibling file");
    put_save(e.usb, "Jk4Speed/saves/BRAVO/BRAVO", "BRAVO", 2);                    /* 2. a profile folder */
    const unsigned char *d1 = save_bytes("CHARL", 3);
    static unsigned char c1[JK_SAVE_SIZE];
    memcpy(c1, d1, sizeof c1);
    const unsigned char *d2 = save_bytes("DELTA", 4);
    const char *n1[1] = { "NFS Most Wanted/CHARL/CHARL" };
    const char *n2[1] = { "DELTA/DELTA" };
    const unsigned char *p1[1] = { c1 }, *p2[1] = { d2 };
    size_t s1[1] = { (size_t)JK_SAVE_SIZE };
    char z[512];
    snprintf(z, sizeof z, "%s/Jk4Speed/saves/pc-saves.zip", e.usb);
    put_zip(z, n1, p1, s1, 1);                                                      /* 3. zips of either */
    snprintf(z, sizeof z, "%s/Jk4Speed/saves/delta.zip", e.usb);
    put_zip(z, n2, p2, s1, 1);
    tmp_put(e.usb, "Jk4Speed/saves/ECHO/ECHO", "not a save at all");                 /* foreign */
    put_save(e.usb, "Jk4Speed/saves/2026-11-20_1000/NFS Most Wanted/FOXT/FOXT", "FOXT", 5);   /* our export */
    tmp_put(e.usb, "Jk4Speed/saves/2026-11-20_1000/jk4speed-export.txt", "x");
    put_save(e.saves, "BRAVO/BRAVO", "BRAVO", 9);                                   /* already on the console */
    JkSavesFound f;
    char why[160] = "";
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK_EQ(f.n, 5);
    CHECK(cand(&f, "ALPHA") >= 0 && cand(&f, "BRAVO") >= 0 && cand(&f, "CHARL") >= 0 && cand(&f, "DELTA") >= 0);
    CHECK(cand(&f, "FOXT") >= 0 && f.c[cand(&f, "FOXT")].console == 1);
    CHECK(f.c[cand(&f, "ALPHA")].console == 0);
    CHECK_EQ(cand(&f, "ECHO"), -1);
    CHECK_EQ(f.rejected, 1);
    CHECK(strstr(f.rejected_name, "ECHO") != NULL);
    CHECK_EQ(f.collisions, 1);
    CHECK(f.c[cand(&f, "BRAVO")].exists && !strcmp(f.c[cand(&f, "BRAVO")].keep_as, "BRAV2"));
    /* nothing there */
    Env g; env_new(&g);
    CHECK_EQ(jk_saves_find(g.saves, g.usb, g.data, &f, why, sizeof why), 0);
    CHECK_EQ(f.n, 0);
    CHECK(jk_saves_find(g.saves, NULL, g.data, &f, why, sizeof why) < 0);
    tmp_rm_rf(g.root);
    tmp_rm_rf(e.root);
}

static void import_overwrite_backs_up_first(void)
{
    Env e; env_new(&e);
    put_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1);
    put_save(e.saves, "KEEP/KEEP", "KEEP", 2);
    put_save(e.usb, "Jk4Speed/saves/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 3);
    put_save(e.usb, "Jk4Speed/saves/NFS Most Wanted/JOKER/JOKER", "JOKER", 4);
    tmp_put(e.usb, "Jk4Speed/saves/NFS Most Wanted/JOKER/extra.cfg", "sibling");
    JkSavesFound f;
    char why[160] = "", bk[512];
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK_EQ(jk_saves_import_found(e.saves, &f, 0, e.data, "2026-11-23_2110", bk, sizeof bk, why, sizeof why), 0);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 3));
    CHECK(same_save(e.saves, "JOKER/JOKER", "JOKER", 4));
    CHECK(tmp_exists(e.saves, "JOKER/extra.cfg"));
    CHECK(same_save(e.saves, "KEEP/KEEP", "KEEP", 2));
    CHECK(same_save(e.data, "backup/saves/2026-11-23_2110/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 1));
    CHECK(strstr(bk, "backup/saves/2026-11-23_2110") != NULL);
    char side[512];
    snprintf(side, sizeof side, "%s.jk4new", e.saves);
    CHECK(!jk_fs_exists(side));
    snprintf(side, sizeof side, "%s.jk4old", e.saves);
    CHECK(!jk_fs_exists(side));
    tmp_rm_rf(e.root);
}

static void import_keep_both_renames_the_copy(void)
{
    Env e; env_new(&e);
    put_save(e.saves, "GOATS/GOATS", "GOATS", 1);
    put_save(e.usb, "Jk4Speed/saves/GOATS/GOATS", "GOATS", 2);
    JkSavesFound f;
    char why[160] = "", bk[512], p[512];
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK_EQ(f.collisions, 1);
    CHECK_EQ(jk_saves_import_found(e.saves, &f, 1, e.data, NULL, bk, sizeof bk, why, sizeof why), 0);
    CHECK(same_save(e.saves, "GOATS/GOATS", "GOATS", 1));               /* the console's own: untouched */
    snprintf(p, sizeof p, "%s/GOAT2/GOAT2", e.saves);
    CHECK_EQ(jk_save_check(p), 0);                                       /* renamed copy: still a valid save */
    size_t n = 0;
    char *d = jk_fs_read(p, (size_t)JK_SAVE_SIZE, &n);
    CHECK(d && !memcmp(d + JK_SAVE_NAME_AT, "GOAT2", 6));
    save_bytes("GOATS", 2);
    CHECK(d && !memcmp(d + 0x34, SAVE + 0x34, JK_SAVE_NAME_AT - 0x34));   /* the career data itself unchanged */
    free(d);
    tmp_rm_rf(e.root);
}

static void import_creates_missing_folder(void)
{
    Env e; env_new(&e);
    put_save(e.usb, "Jk4Speed/saves/NFS Most Wanted/RAZOR/RAZOR", "RAZOR", 1);
    JkSavesFound f;
    char why[160] = "", bk[512];
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK_EQ(jk_saves_import_found(e.saves, &f, 0, e.data, NULL, bk, sizeof bk, why, sizeof why), 0);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1));
    CHECK_EQ(bk[0], 0);
    tmp_rm_rf(e.root);
}

static void recover_after_a_cut(void)
{
    Env e; env_new(&e);
    char old[512], nw[512];
    snprintf(old, sizeof old, "%s.jk4old", e.saves);
    snprintf(nw, sizeof nw, "%s.jk4new", e.saves);
    /* cut after the first rename: the folder is gone, the old one beside it */
    put_save(old, "RAZOR/RAZOR", "RAZOR", 1);
    put_save(nw, "RAZOR/RAZOR", "RAZOR", 2);
    CHECK_EQ(jk_saves_recover(e.saves), 1);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1));
    CHECK(!jk_fs_exists(old) && !jk_fs_exists(nw));
    /* cut after the second rename: the new folder is in place, the old one left over */
    put_save(old, "RAZOR/RAZOR", "RAZOR", 1);
    CHECK_EQ(jk_saves_recover(e.saves), 1);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1) && !jk_fs_exists(old));
    CHECK_EQ(jk_saves_recover(e.saves), 0);
    tmp_rm_rf(e.root);
}

static void import_skips_links(void)
{
    Env e; env_new(&e);
    put_save(e.usb, "Jk4Speed/saves/RAZOR/RAZOR", "RAZOR", 1);
    tmp_put(e.root, "secret.txt", "outside");
    char link[512], target[512];
    snprintf(target, sizeof target, "%s/secret.txt", e.root);
    snprintf(link, sizeof link, "%s/Jk4Speed/saves/RAZOR/link", e.usb);
    CHECK_EQ(symlink(target, link), 0);
    JkSavesFound f;
    char why[160] = "", bk[512];
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK_EQ(jk_saves_import_found(e.saves, &f, 0, e.data, NULL, bk, sizeof bk, why, sizeof why), 0);
    CHECK(tmp_exists(e.saves, "RAZOR/RAZOR"));
    CHECK(!tmp_exists(e.saves, "RAZOR/link"));
    tmp_rm_rf(e.root);
}

static void import_refuses_saves_dir_link(void)
{
    Env e; env_new(&e);
    put_save(e.usb, "Jk4Speed/saves/RAZOR/RAZOR", "RAZOR", 1);
    char outside[512], parent[512];
    snprintf(outside, sizeof outside, "%s/outside", e.root);
    mkdir(outside, 0755);
    snprintf(parent, sizeof parent, "%s", e.saves);
    *strrchr(parent, '/') = 0;
    tmp_put(parent, "x", "");
    CHECK_EQ(symlink(outside, e.saves), 0);
    JkSavesFound f;
    char why[160] = "", bk[512];
    CHECK_EQ(jk_saves_find(e.saves, e.usb, e.data, &f, why, sizeof why), 0);
    CHECK(jk_saves_import_found(e.saves, &f, 0, e.data, NULL, bk, sizeof bk, why, sizeof why) < 0);
    CHECK(!tmp_exists(outside, "RAZOR"));
    tmp_rm_rf(e.root);
}

/* ---- rename (docs/launcher/2026-11-23-save-format.md) ---- */

static void alias_rules(void)
{
    CHECK(jk_alias_valid("NAME"));
    CHECK(jk_alias_valid("Joker"));
    CHECK(jk_alias_valid("A"));
    CHECK(jk_alias_valid("abcdefghij12345"));                 /* 15 */
    CHECK(!jk_alias_valid("abcdefghij123456"));               /* 16 */
    CHECK(!jk_alias_valid(""));
    CHECK(!jk_alias_valid("Big Joker"));
    const char *bad = "-=/?\\;',.!@#~%`&*()_+[]|:\"<>";
    for (const char *c = bad; *c; c++) {
        char s[4] = { 'A', *c, 'B', 0 };
        CHECK(!jk_alias_valid(s));
        CHECK(!jk_alias_char(*c));
    }
    for (int ch = 0; ch < 128; ch++)
        CHECK_EQ(jk_alias_char(ch), (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'));
}

static void rename_bytes_reseals(void)
{
    static unsigned char d[JK_SAVE_SIZE], orig[JK_SAVE_SIZE];
    memcpy(orig, save_bytes("NAME", 5), sizeof orig);
    memcpy(d, orig, sizeof d);
    CHECK_EQ(jk_save_rename_bytes(d, sizeof d, "NAME", "NAME"), 0);
    CHECK(!memcmp(d, orig, sizeof d));                         /* identity: byte for byte */
    CHECK_EQ(jk_save_rename_bytes(d, sizeof d, "NAME", "Joker"), 0);
    CHECK(!memcmp(d + JK_SAVE_NAME_AT, "Joker\0\0\0", 8));
    CHECK(!memcmp(d + 0x1C, orig + 0x1C, 8) && !memcmp(d + 0x10, orig + 0x10, 4));   /* block A and crcA unchanged */
    CHECK(memcmp(d + 0x14, orig + 0x14, 4) != 0);              /* crcB, crcHdr and the MD5 change */
    CHECK(memcmp(d + JK_SAVE_SIZE - 16, orig + JK_SAVE_SIZE - 16, 16) != 0);
    memcpy(SAVE, d, sizeof d);
    CHECK(!memcmp(save_bytes("Joker", 5), d, sizeof d));       /* the same as a save made as Joker */
    /* refusals: wrong old name, bad new name, any checksum off */
    memcpy(d, orig, sizeof d);
    CHECK(jk_save_rename_bytes(d, sizeof d, "NAM", "Joker") < 0);
    CHECK(jk_save_rename_bytes(d, sizeof d, "NAMEX", "Joker") < 0);
    CHECK(jk_save_rename_bytes(d, sizeof d, "NAME", "Jo ker") < 0);
    CHECK(!memcmp(d, orig, sizeof d));
    int offs[4] = { 0x10, 0x14, 0x18, 0x5000 };
    for (int i = 0; i < 4; i++) {
        memcpy(d, orig, sizeof d);
        d[offs[i]] ^= 1;
        CHECK(jk_save_rename_bytes(d, sizeof d, "NAME", "Joker") < 0);
    }
}

static void rename_on_disk(void)
{
    Env e; env_new(&e);
    put_save(e.saves, "NAME/NAME", "NAME", 3);
    put_save(e.saves, "RAZOR/RAZOR", "RAZOR", 4);
    char why[160] = "", bk[512];
    /* collisions, any case; bad names */
    CHECK(jk_saves_rename(e.saves, "NAME", "razor", e.data, NULL, bk, sizeof bk, why, sizeof why) < 0);
    CHECK(strstr(why, "already") != NULL);
    CHECK(jk_saves_rename(e.saves, "NAME", "Jo_ker", e.data, NULL, bk, sizeof bk, why, sizeof why) < 0);
    CHECK(jk_saves_rename(e.saves, "GHOST", "Joker", e.data, NULL, bk, sizeof bk, why, sizeof why) < 0);
    CHECK(same_save(e.saves, "NAME/NAME", "NAME", 3) && !tmp_exists(e.data, "backup/saves"));
    /* the real thing */
    CHECK_EQ(jk_saves_rename(e.saves, "NAME", "Joker", e.data, "2026-11-23_2200", bk, sizeof bk, why, sizeof why), 0);
    CHECK(!tmp_exists(e.saves, "NAME"));
    CHECK(same_save(e.saves, "Joker/Joker", "Joker", 3));
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 4));
    CHECK(same_save(e.data, "backup/saves/2026-11-23_2200/NFS Most Wanted/NAME/NAME", "NAME", 3));
    /* a case change of the same profile is allowed */
    CHECK_EQ(jk_saves_rename(e.saves, "Joker", "JOKER", e.data, NULL, bk, sizeof bk, why, sizeof why), 0);
    CHECK(same_save(e.saves, "JOKER/JOKER", "JOKER", 3));
    /* a corrupt save: refused, nothing changed */
    char p[512];
    save_bytes("RAZOR", 4);
    SAVE[0x6000] ^= 0x80;
    snprintf(p, sizeof p, "%s/RAZOR/RAZOR", e.saves);
    jk_fs_write_atomic(p, SAVE, (size_t)JK_SAVE_SIZE);
    CHECK(jk_saves_rename(e.saves, "RAZOR", "Blaze", e.data, NULL, bk, sizeof bk, why, sizeof why) < 0);
    CHECK(strstr(why, "did not check out") != NULL);
    CHECK(tmp_exists(e.saves, "RAZOR/RAZOR") && !tmp_exists(e.saves, "Blaze"));
    tmp_rm_rf(e.root);
}

/* The doc's test vector on the real save, when its local copy is in
 * launcher/build/saves/NAME/NAME (git-ignored; never in the repo). */
static void real_save_vector(void)
{
    const char *path = JK_ROOT_DIR "/build/saves/NAME/NAME";
    size_t n = 0;
    unsigned char *d = (unsigned char *)jk_fs_read(path, (size_t)JK_SAVE_SIZE, &n);
    if (!d) { fprintf(stderr, "  (skipped: no copy at %s)\n", path); return; }
    char sha[65];
    jk_sha256_hex(d, n, sha);
    CHECK(!strcmp(sha, "e63551fef3e95a95d5fad6ee5584f0463e9cdeff6cbc845512f8f4eb38845943"));
    CHECK_EQ(jk_save_check(path), 0);
    CHECK_EQ(jk_save_crc(d, 0x18), 0x93B32626u);
    CHECK_EQ(jk_save_crc(d + 0x1C, 8), 0x28A9C0BBu);
    CHECK_EQ(jk_save_crc(d + 0x24, 0xF848), 0x5F8427CEu);
    CHECK_EQ(jk_save_rename_bytes(d, n, "NAME", "NAME"), 0);
    jk_sha256_hex(d, n, sha);
    CHECK(!strcmp(sha, "e63551fef3e95a95d5fad6ee5584f0463e9cdeff6cbc845512f8f4eb38845943"));
    CHECK_EQ(jk_save_rename_bytes(d, n, "NAME", "Joker"), 0);
    jk_sha256_hex(d, n, sha);
    CHECK(!strcmp(sha, "8c3ccdcb64730ad47cd1f4e1734a8454b408d51c1e9741ced9466989b43a6091"));
    free(d);
}

static void migrate_copies_once_never_moves(void)
{
    Env e; env_new(&e);
    char old[400], state[400], note[200];
    snprintf(old, sizeof old, "%s/old/NFS Most Wanted", e.root);
    snprintf(state, sizeof state, "%s/state", e.data);
    put_save(old, "RAZOR/RAZOR", "RAZOR", 1);
    put_save(old, "JOKER/JOKER", "JOKER", 2);
    CHECK_EQ(jk_saves_migrate_once(old, e.saves, state, note, sizeof note), 1);
    CHECK(same_save(e.saves, "RAZOR/RAZOR", "RAZOR", 1));
    CHECK(same_save(e.saves, "JOKER/JOKER", "JOKER", 2));
    CHECK(same_save(old, "RAZOR/RAZOR", "RAZOR", 1));       /* copied, never moved */
    CHECK(same_save(old, "JOKER/JOKER", "JOKER", 2));
    CHECK(tmp_exists(state, "saves-migrated"));
    tmp_rm_rf(e.saves);                                     /* gone again: the marker still stops a second copy */
    CHECK_EQ(jk_saves_migrate_once(old, e.saves, state, note, sizeof note), 0);
    CHECK(!tmp_exists(e.saves, "RAZOR/RAZOR"));
}

static void migrate_skips_when_new_has_profiles(void)
{
    Env e; env_new(&e);
    char old[400], state[400], note[200];
    snprintf(old, sizeof old, "%s/old/NFS Most Wanted", e.root);
    snprintf(state, sizeof state, "%s/state", e.data);
    put_save(old, "RAZOR/RAZOR", "RAZOR", 1);
    put_save(e.saves, "JOKER/JOKER", "JOKER", 2);
    CHECK_EQ(jk_saves_migrate_once(old, e.saves, state, note, sizeof note), 0);
    CHECK(!tmp_exists(e.saves, "RAZOR/RAZOR"));
    CHECK(same_save(e.saves, "JOKER/JOKER", "JOKER", 2));
    CHECK(same_save(old, "RAZOR/RAZOR", "RAZOR", 1));
    CHECK(tmp_exists(state, "saves-migrated"));              /* decided once: no later surprise copy */
}

static void migrate_failure_leaves_nothing_and_retries(void)
{
    Env e; env_new(&e);
    char old[400], state[400], note[200], rel[64];
    snprintf(old, sizeof old, "%s/old/NFS Most Wanted", e.root);
    snprintf(state, sizeof state, "%s/state", e.data);
    put_save(old, "AAA/AAA", "AAA", 1);
    put_save(old, "BBB/BBB", "BBB", 2);
    put_save(old, "CCC/CCC", "CCC", 3);
    for (int i = 0; i <= JK_SAVES_MAX_FILES; i++) {        /* too many files: the copy of CCC fails */
        snprintf(rel, sizeof rel, "CCC/x%d", i);
        tmp_put(old, rel, "x");
    }
    tmp_rm_rf(e.saves);
    CHECK(jk_saves_migrate_once(old, e.saves, state, note, sizeof note) < 0);
    CHECK(!tmp_exists(e.saves, "AAA/AAA"));                 /* nothing half-copied */
    CHECK(!tmp_exists(e.saves, "BBB/BBB"));
    CHECK(!tmp_exists(state, "saves-migrated"));
    for (int i = 0; i <= JK_SAVES_MAX_FILES; i++) {        /* the problem goes away */
        char p[500];
        snprintf(p, sizeof p, "%s/CCC/x%d", old, i);
        unlink(p);
    }
    CHECK_EQ(jk_saves_migrate_once(old, e.saves, state, note, sizeof note), 1);
    CHECK(same_save(e.saves, "AAA/AAA", "AAA", 1));
    CHECK(same_save(e.saves, "BBB/BBB", "BBB", 2));
    CHECK(same_save(e.saves, "CCC/CCC", "CCC", 3));
}

static void migrate_nothing_to_copy(void)
{
    Env e; env_new(&e);
    char state[400], note[200];
    snprintf(state, sizeof state, "%s/state", e.data);
    CHECK_EQ(jk_saves_migrate_once("/nonexistent/old", e.saves, state, note, sizeof note), 0);
    CHECK_EQ(jk_saves_migrate_once(NULL, e.saves, state, note, sizeof note), 0);
}

static void saves_screen_shows_new_path(void)
{
    CHECK(!strcmp(JK_S_SAVES_DIR, "/data/Jk4Speed/Saves/NFS Most Wanted"));
    CHECK(!strcasestr(JK_S_SAVES_DIR, "prospero"));
}

int main(void)
{
    RUN(alias_rules);
    RUN(rename_bytes_reseals);
    RUN(rename_on_disk);
    RUN(real_save_vector);
    RUN(info_counts_profiles);
    RUN(save_check_layout);
    RUN(export_copies_tree_to_usb);
    RUN(export_needs_saves_and_usb);
    RUN(find_every_layout);
    RUN(import_overwrite_backs_up_first);
    RUN(import_keep_both_renames_the_copy);
    RUN(import_creates_missing_folder);
    RUN(recover_after_a_cut);
    RUN(import_skips_links);
    RUN(import_refuses_saves_dir_link);
    RUN(migrate_copies_once_never_moves);
    RUN(migrate_skips_when_new_has_profiles);
    RUN(migrate_failure_leaves_nothing_and_retries);
    RUN(migrate_nothing_to_copy);
    RUN(saves_screen_shows_new_path);
    return jk_test_report("test_saves");
}
