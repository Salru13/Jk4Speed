/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* File helpers and the package manifest (spec 8.1, 8.2). */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_fs.h"
#include "jk_manifest.h"
#include "jk_sha256.h"

#define SHA_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define SHA_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define EXE_LINE "exe speed.exe 80774c2e5d619b4f120b48d4462896fd504c263399d203a238769cffde1d253c\n"

static void sha256_known_vector(void)
{
    char h[65];
    jk_sha256_hex("abc", 3, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

static void fs_atomic_write_and_hash(void)
{
    char d[256], p[512], h[65], want[65];
    tmp_dir(d, sizeof d);
    snprintf(p, sizeof p, "%s/a/b/c.txt", d);
    CHECK_EQ(jk_fs_mkdir_parent(p), 0);
    CHECK_EQ(jk_fs_write_atomic(p, "hello\n", 6), 0);
    long long size = -1;
    CHECK_EQ(jk_fs_sha256(p, h, &size), 0);
    jk_sha256_hex("hello\n", 6, want);
    CHECK(!strcmp(h, want));
    CHECK_EQ(size, 6);
    CHECK(!tmp_exists(d, "a/b/c.txt.jk4new"));
    CHECK(jk_fs_sha256("/nonexistent/x", h, &size) < 0);
    tmp_rm_rf(d);
}

static void fs_copy_checked_verifies(void)
{
    char d[256], src[512], dst[512], h[65];
    tmp_dir(d, sizeof d);
    tmp_put(d, "src.bin", "payload");
    snprintf(src, sizeof src, "%s/src.bin", d);
    snprintf(dst, sizeof dst, "%s/out/dst.bin", d);
    jk_sha256_hex("payload", 7, h);
    CHECK_EQ(jk_fs_copy_checked(src, dst, h), 0);
    char buf[64];
    CHECK_EQ(tmp_get(d, "out/dst.bin", buf, sizeof buf), 7);
    /* a wrong expected hash: the target is not created and no temp is left */
    snprintf(dst, sizeof dst, "%s/out/other.bin", d);
    CHECK(jk_fs_copy_checked(src, dst, SHA_A) < 0);
    CHECK(!tmp_exists(d, "out/other.bin"));
    CHECK(!tmp_exists(d, "out/other.bin.jk4new"));
    tmp_rm_rf(d);
}

static void fs_join_rejects_overflow(void)
{
    char small[8];
    CHECK(jk_fs_join(small, sizeof small, "/data", "Jk4Speed") < 0);
    char ok[64];
    CHECK_EQ(jk_fs_join(ok, sizeof ok, "/data", "Jk4Speed"), 0);
    CHECK(!strcmp(ok, "/data/Jk4Speed"));
}

static void parses_all_kinds(void)
{
    JkManifest m;
    const char *t = "# Jk4Speed files\n" EXE_LINE
                    "require GLOBAL\n"
                    "file d3d9.dll " SHA_A " 1234\n"
                    "config scripts/NFSMostWanted.FusionFix.cfg " SHA_B " 99\n"
                    "remove scripts/NFSMostWanted.WidescreenFix.asi\n"
                    "seed nfsmw.reg " SHA_A " 10\n"
                    "\n";
    CHECK_EQ(jk_manifest_parse(t, &m), 0);
    CHECK_EQ(m.n, 6);
    CHECK_EQ(m.e[0].kind, JK_M_EXE);
    CHECK(!strcmp(m.e[0].path, "speed.exe"));
    CHECK_EQ(m.e[1].kind, JK_M_REQUIRE);
    CHECK_EQ(m.e[2].kind, JK_M_FILE);
    CHECK_EQ(m.e[2].size, 1234);
    CHECK(!strcmp(m.e[2].sha, SHA_A));
    CHECK_EQ(m.e[3].kind, JK_M_CONFIG);
    CHECK_EQ(m.e[4].kind, JK_M_REMOVE);
    CHECK_EQ(m.e[5].kind, JK_M_SEED);
    CHECK(jk_manifest_exe(&m) == &m.e[0]);
}

static int parse_fails(const char *text)
{
    JkManifest m;
    int r = jk_manifest_parse(text, &m);
    return r < 0 && m.err[0];
}

static void rejects_dotdot(void) { CHECK(parse_fails(EXE_LINE "file ../d3d9.dll " SHA_A " 1\n")); }
static void rejects_absolute(void) { CHECK(parse_fails(EXE_LINE "file /etc/d3d9.dll " SHA_A " 1\n")); }
static void rejects_backslash(void) { CHECK(parse_fails(EXE_LINE "file scripts\\a.asi " SHA_A " 1\n")); }
static void rejects_speed_exe_as_file(void) { CHECK(parse_fails(EXE_LINE "file speed.exe " SHA_A " 1\n")); }
static void rejects_unknown_name(void)
{
    CHECK(parse_fails(EXE_LINE "file GLOBAL/GLOBALB.BUN " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "remove TRACKS\n"));
    CHECK(parse_fails(EXE_LINE "file scripts/sub/a.asi " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "file scripts/a.dll " SHA_A " 1\n"));
}
static void allows_known_names(void)
{
    JkManifest m;
    CHECK_EQ(jk_manifest_parse(EXE_LINE "file scripts/X.asi " SHA_A " 1\nfile Xidi.32.dll " SHA_A " 1\n"
                               "file XIDI.INI " SHA_A " 1\nfile dxvk.conf " SHA_A " 1\nfile dinput8.dll " SHA_A " 1\n"
                               "remove dsound.dll\nremove scripts/A.tpk\n", &m), 0);
}
static void rejects_bad_sha(void)
{
    CHECK(parse_fails(EXE_LINE "file d3d9.dll abc 1\n"));
    CHECK(parse_fails(EXE_LINE "file d3d9.dll " "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" " 1\n"));
    CHECK(parse_fails(EXE_LINE "file d3d9.dll " SHA_A "\n"));
}
static void exactly_one_exe(void)
{
    CHECK(parse_fails("file d3d9.dll " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE EXE_LINE));
}
static void rejects_unknown_kind(void) { CHECK(parse_fails(EXE_LINE "patch d3d9.dll\n")); }
static void seed_must_be_reg(void)
{
    CHECK(parse_fails(EXE_LINE "seed sub/nfsmw.reg " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "seed d3d9.dll " SHA_A " 1\n"));
}
static void error_names_line(void)
{
    JkManifest m;
    CHECK(jk_manifest_parse(EXE_LINE "require GLOBAL\nfile ../x " SHA_A " 1\n", &m) < 0);
    CHECK(strstr(m.err, "line 3") != NULL);
}
static void too_many_entries(void)
{
    static char t[64 * 80 + 200];
    strcpy(t, EXE_LINE);
    for (int i = 0; i < JK_MANIFEST_MAX + 1; i++) strcat(t, "require GLOBAL\n");
    CHECK(parse_fails(t));
}

#define SHA_ROAD "e2a7a504a2563b72ef0674108cbc14dfb24b1896c2e880ca937d4e95e161c226"
#define SHA_HUDTEX "6e57f398c77618651ba849429679807d5dec08a4632c9e3582feeb7f283118da"
#define SHA_INGAME "9825274a478bafa22c0b61f2059552ca45c6c14b8052a1884e5bc26d20069b59"
#define SHA_HUDS "08b9d1ef4847e1092d8bbfcd4aa8181ce4532438ba042b420ab2661f36d6706d"
#define SHA_P30 "12268daf2318fe88544aad5a05ea718384723d9265fb5c6d925f00fb489595ee"
#define SHA_P60 "5ba0a0292ebc24e6d01e3cc8f16446c258a08260540a599885b3af3f30226c91"

/* Owner decision D3, narrowed 2026-11-23: the HD files are pinned, but come
 * only from the mods folder, never from a package line. */
static void hd_files_only_at_release_hash(void)
{
    JkManifest m;
    CHECK(parse_fails(EXE_LINE "file TRACKS/STREAML2RA.BUN " SHA_ROAD " 532663556\n"));
    CHECK(parse_fails(EXE_LINE "file GLOBAL/HUDTEXRACE.BIN " SHA_HUDTEX " 2696832\n"));
    CHECK(parse_fails(EXE_LINE "file GLOBAL/HUDS_Custom_00.bin " SHA_HUDS " 133632\n"));
    CHECK(parse_fails(EXE_LINE "file scripts/X360Stuff.asi "
                      "70171ad75d5ccdf6032bed161fbcdad6a0799b2f7c717af6d977e3571dd185bf 402944\n"));
    CHECK_EQ(jk_manifest_parse(EXE_LINE "require GLOBAL/InGameA.bun\n", &m), 0);   /* requiring one is fine */
    CHECK(parse_fails(EXE_LINE "file TRACKS/STREAML2RA.BUN " SHA_A " 1\n"));        /* any other build */
    CHECK(parse_fails(EXE_LINE "file GLOBAL/InGameA.bun " SHA_HUDTEX " 1\n"));      /* another HD file's hash */
    CHECK(parse_fails(EXE_LINE "config GLOBAL/HUDTEXRACE.BIN " SHA_HUDTEX " 1\n"));
    CHECK(parse_fails(EXE_LINE "remove GLOBAL/HUDS_Custom_00.bin\n"));
    CHECK(parse_fails(EXE_LINE "file GLOBAL/GlobalB.lzc " SHA_A " 1\n"));          /* not one of the four */
    CHECK(parse_fails(EXE_LINE "file GLOBAL/attributes.bin " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "file TRACKS/STREAML2RA.BUN.bak " SHA_ROAD " 1\n"));
    CHECK(parse_fails(EXE_LINE "file global/ingamea.bun " SHA_INGAME " 1\n"));      /* exact name only */
    CHECK(jk_manifest_path_allowed(JK_M_FILE, "GLOBAL/InGameA.bun"));
    CHECK(!jk_manifest_path_allowed(JK_M_CONFIG, "GLOBAL/InGameA.bun"));
    CHECK(jk_manifest_hd_sha("TRACKS/STREAML2RA.BUN") && !strcmp(jk_manifest_hd_sha("TRACKS/STREAML2RA.BUN"), SHA_ROAD));
    CHECK(jk_manifest_hd_sha("d3d9.dll") == NULL);
}

/* The release ini: one file line and one accept line (its PACE=60 twin). */
static void accept_adds_a_second_hash(void)
{
    JkManifest m;
    CHECK_EQ(jk_manifest_parse(EXE_LINE "file jk4speed-dev.ini " SHA_P30 " 890\naccept jk4speed-dev.ini " SHA_P60 "\n", &m), 0);
    CHECK_EQ(m.n, 3);
    CHECK_EQ(m.e[2].kind, JK_M_ACCEPT);
    CHECK(jk_manifest_accepts(&m, "jk4speed-dev.ini", SHA_P30));
    CHECK(jk_manifest_accepts(&m, "jk4speed-dev.ini", SHA_P60));
    CHECK(!jk_manifest_accepts(&m, "jk4speed-dev.ini", SHA_A));
    CHECK(!jk_manifest_accepts(&m, "d3d9.dll", SHA_P30));
    CHECK(!jk_manifest_accepts(&m, "jk4speed-dev.ini", ""));
    CHECK(parse_fails(EXE_LINE "accept jk4speed-dev.ini " SHA_P60 "\n"));             /* no file line before it */
    CHECK(parse_fails(EXE_LINE "file d3d9.dll " SHA_A " 1\naccept jk4speed-dev.ini " SHA_P60 "\n"));
    CHECK(parse_fails(EXE_LINE "file jk4speed-dev.ini " SHA_P30 " 890\naccept jk4speed-dev.ini " SHA_P60 " 890\n"));
    CHECK(parse_fails(EXE_LINE "file TRACKS/STREAML2RA.BUN " SHA_ROAD " 1\naccept TRACKS/STREAML2RA.BUN " SHA_A "\n"));
    CHECK(parse_fails(EXE_LINE "file jk4look.ini " SHA_A " 1\n"));                   /* LOOK is gone */
}

/* The runtime profile in /data/prospero-win/profiles. */
static void profile_kind(void)
{
    JkManifest m;
    CHECK_EQ(jk_manifest_parse(EXE_LINE "profile nfsmw-x360.profile " SHA_A " 700\n", &m), 0);
    CHECK_EQ(m.e[1].kind, JK_M_PROFILE);
    CHECK(jk_manifest_accepts(&m, "nfsmw-x360.profile", SHA_A));
    CHECK(parse_fails(EXE_LINE "profile sub/nfsmw-x360.profile " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "profile nfsmw-x360.ini " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "profile profiles.lst " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "file nfsmw-x360.profile " SHA_A " 1\n"));
}

/* The native DXVK server for /data/prospero-win/override. */
static void server_kind(void)
{
    JkManifest m;
    CHECK_EQ(jk_manifest_parse(EXE_LINE "server dxvknative.prx " SHA_A " 4404304\n", &m), 0);
    CHECK_EQ(m.e[1].kind, JK_M_SERVER);
    CHECK(jk_manifest_accepts(&m, "dxvknative.prx", SHA_A));
    CHECK(parse_fails(EXE_LINE "server sub/dxvknative.prx " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "server dxvknative.dll " SHA_A " 1\n"));
    CHECK(parse_fails(EXE_LINE "server ntdll.prx " SHA_A " 1\n"));        /* only the DXVK server */
    CHECK(parse_fails(EXE_LINE "file dxvknative.prx " SHA_A " 1\n"));
}

int main(void)
{
    RUN(sha256_known_vector);
    RUN(fs_atomic_write_and_hash);
    RUN(fs_copy_checked_verifies);
    RUN(fs_join_rejects_overflow);
    RUN(parses_all_kinds);
    RUN(rejects_dotdot);
    RUN(rejects_absolute);
    RUN(rejects_backslash);
    RUN(rejects_speed_exe_as_file);
    RUN(rejects_unknown_name);
    RUN(allows_known_names);
    RUN(rejects_bad_sha);
    RUN(exactly_one_exe);
    RUN(rejects_unknown_kind);
    RUN(seed_must_be_reg);
    RUN(error_names_line);
    RUN(too_many_entries);
    RUN(hd_files_only_at_release_hash);
    RUN(accept_adds_a_second_hash);
    RUN(profile_kind);
    RUN(server_kind);
    return jk_test_report("test_manifest");
}
