/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The "game didn't start" marker. See jk_session.h. */
#include "jk_session.h"
#include "jk_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER "state/starting"

int jk_session_starting(const char *data_root, const char *note)
{
    char p[1024], line[256];
    if (!data_root || jk_fs_join(p, sizeof p, data_root, MARKER) < 0 || jk_fs_mkdir_parent(p) < 0) return -1;
    int n = snprintf(line, sizeof line, "%s\n", note ? note : "");
    return jk_fs_write_atomic(p, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1));
}

void jk_session_first_frame(const char *data_root)
{
    char p[1024];
    if (data_root && jk_fs_join(p, sizeof p, data_root, MARKER) == 0) jk_fs_remove(p);
}

int jk_session_take_failure(const char *data_root, char *note, size_t cap)
{
    char p[1024];
    if (note && cap) note[0] = 0;
    if (!data_root || jk_fs_join(p, sizeof p, data_root, MARKER) < 0 || !jk_fs_exists(p)) return 0;
    char *text = jk_fs_read(p, 4096, NULL);
    if (text && note && cap) {
        size_t n = strcspn(text, "\r\n");
        if (n >= cap) n = cap - 1;
        memcpy(note, text, n);
        note[n] = 0;
    }
    free(text);
    jk_fs_remove(p);
    return 1;
}
