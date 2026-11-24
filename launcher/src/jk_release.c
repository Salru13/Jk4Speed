/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "jk_release.h"
#include "jk_ed25519.h"
#include "jk_fs.h"
#include "jk_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_key_id[9], g_version[64];

#define REL_REASON_BAD "The signature does not match the JK4SPEED release key."
#define REL_REASON_FORMAT "The signed file list could not be read."

static int safe_rel(const char *rel)
{
    if (!rel[0] || rel[0] == '/' || strstr(rel, "..") || strchr(rel, 92)) return 0;   /* no backslash */
    return 1;
}

int jk_release_verify(const char *app_root, const unsigned char pk[32], JkRelease *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    out->state = JK_REL_UNOFFICIAL;
    if (!app_root || !pk) return 0;
    {
        char sha[65];
        jk_sha256_hex(pk, 32, sha);
        for (int i = 0; i < 8; i++) g_key_id[i] = (char)(sha[i] >= 'a' ? sha[i] - 32 : sha[i]);
        g_key_id[8] = 0;
        out->key_id = g_key_id;
    }
    char txt[1024], sigp[1024];
    if (snprintf(txt, sizeof txt, "%s/jk4speed-files/release.txt", app_root) >= (int)sizeof txt ||
        snprintf(sigp, sizeof sigp, "%s/jk4speed-files/release.sig", app_root) >= (int)sizeof sigp)
        return 0;
    size_t tn = 0, sn = 0;
    char *text = jk_fs_read(txt, 4u << 20, &tn);
    char *sig = jk_fs_read(sigp, 64, &sn);
    if (!text || !sig || sn != 64) { free(text); free(sig); return 0; }      /* reason NULL: no signature */
    if (jk_ed25519_verify((const unsigned char *)sig, (const unsigned char *)text, tn, pk) != 0) {
        out->reason = REL_REASON_BAD;
        free(text); free(sig);
        return 0;
    }
    free(sig);
    /* signed: now the files */
    char *save = NULL, *line = strtok_r(text, "\n", &save);
    if (!line || strcmp(line, "JK4SPEED-RELEASE 1") != 0) { out->reason = REL_REASON_FORMAT; free(text); return 0; }
    int changed = 0, files = 0, bad = 0;
    while ((line = strtok_r(NULL, "\n", &save))) {
        if (!strncmp(line, "build ", 6)) { snprintf(g_version, sizeof g_version, "%s", line + 6); out->version = g_version; continue; }
        if (strlen(line) < 67 || line[64] != ' ' || line[65] != ' ') { bad = 1; break; }
        const char *rel = line + 66;
        if (!safe_rel(rel)) { bad = 1; break; }
        char path[1024], sha[65];
        files++;
        if (snprintf(path, sizeof path, "%s/%s", app_root, rel) >= (int)sizeof path || jk_fs_sha256(path, sha, NULL) < 0 ||
            strncmp(sha, line, 64) != 0)
            changed++;
    }
    free(text);
    if (bad || files == 0) { out->reason = REL_REASON_FORMAT; return 0; }
    out->key_label = "JK4SPEED release key";
    out->changed = changed;
    out->state = changed ? JK_REL_CHANGED : JK_REL_OFFICIAL;
    return 0;
}
