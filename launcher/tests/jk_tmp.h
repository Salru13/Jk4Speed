/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Test helpers: temporary folders and small files. */
#ifndef JK_TMP_H
#define JK_TMP_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A fresh empty folder under /tmp. */
static __attribute__((unused)) void tmp_dir(char *out, size_t cap)
{
    snprintf(out, cap, "/tmp/jk-test-XXXXXX");
    if (!mkdtemp(out)) { perror("mkdtemp"); exit(2); }
}

static __attribute__((unused)) void tmp_rm_rf(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", path, e->d_name);
            tmp_rm_rf(p);
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

/* Writes text to dir/rel, creating parent folders. */
static __attribute__((unused)) void tmp_put(const char *dir, const char *rel, const char *text)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); exit(2); }
    fputs(text, f);
    fclose(f);
}

/* Reads dir/rel into buf (NUL-terminated). Returns -1 when missing. */
static __attribute__((unused)) int tmp_get(const char *dir, const char *rel, char *buf, size_t cap)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
    fclose(f);
    return (int)n;
}

static __attribute__((unused)) int tmp_exists(const char *dir, const char *rel)
{
    char p[1024];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    return stat(p, &st) == 0;
}

#endif
