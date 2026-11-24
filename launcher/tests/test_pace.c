/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The frame-rate setting: names and steps. r4: the value itself is read from the
 * launcher's settings (jk_settings_load) and composed into the ini by jk_config;
 * the file formats are covered in test_config.c. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_config.h"
#include "jk_pace.h"

/* The pace the launcher shows is the one in <state>/launcher.ini; the old ini
 * line is not read any more. */
static void pace_comes_from_settings(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    JkSettings s;
    CHECK_EQ(jk_settings_load(d, &s), 0);
    CHECK_EQ(s.pace, JK_PACE_30);                              /* no file: what PATCH installs */
    tmp_put(d, "launcher.ini", "pace=60\n");
    CHECK_EQ(jk_settings_load(d, &s), 0);
    CHECK_EQ(s.pace, JK_PACE_60);
    CHECK_EQ(jk_pace_step(s.pace, -1), JK_PACE_30);
    tmp_rm_rf(d);
}

static void names(void)
{
    CHECK(!strcmp(jk_pace_key(JK_PACE_30), "30"));
    CHECK(!strcmp(jk_pace_key(JK_PACE_60), "60"));
    CHECK(!strcmp(jk_pace_key(JK_PACE_OTHER), "other"));
    CHECK_EQ(jk_pace_step(JK_PACE_30, 1), JK_PACE_60);
    CHECK_EQ(jk_pace_step(JK_PACE_60, 1), JK_PACE_60);
    CHECK_EQ(jk_pace_step(JK_PACE_60, -1), JK_PACE_30);
    CHECK_EQ(jk_pace_step(JK_PACE_30, -1), JK_PACE_30);
    CHECK_EQ(jk_pace_step(JK_PACE_OTHER, -1), JK_PACE_30);
    CHECK_EQ(jk_pace_step(JK_PACE_OTHER, 1), JK_PACE_60);
}

int main(void)
{
    RUN(pace_comes_from_settings);
    RUN(names);
    return jk_test_report("test_pace");
}
