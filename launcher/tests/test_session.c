/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The "game didn't start" marker: <data_root>/state/starting is written on
 * PLAY, deleted at the game's first frame; one left behind means the last
 * start died before its first picture. */
#include "jk_test.h"
#include "jk_tmp.h"
#include "jk_session.h"

static void clean_start_leaves_nothing(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    CHECK_EQ(jk_session_starting(d, "pace=30"), 0);
    CHECK(tmp_exists(d, "state/starting"));
    jk_session_first_frame(d);
    CHECK(!tmp_exists(d, "state/starting"));
    char what[64];
    CHECK_EQ(jk_session_take_failure(d, what, sizeof what), 0);
    tmp_rm_rf(d);
}

static void crash_before_first_frame_is_reported_once(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    CHECK_EQ(jk_session_starting(d, "pace=60 at 2026-11-23_1800"), 0);
    char what[64] = "";
    CHECK_EQ(jk_session_take_failure(d, what, sizeof what), 1);
    CHECK(!strcmp(what, "pace=60 at 2026-11-23_1800"));
    CHECK(!tmp_exists(d, "state/starting"));
    CHECK_EQ(jk_session_take_failure(d, what, sizeof what), 0);      /* reported once */
    tmp_rm_rf(d);
}

static void first_frame_without_marker_is_harmless(void)
{
    char d[256];
    tmp_dir(d, sizeof d);
    jk_session_first_frame(d);
    jk_session_first_frame(NULL);
    CHECK(jk_session_starting(NULL, "x") < 0);
    char what[8];
    CHECK_EQ(jk_session_take_failure(NULL, what, sizeof what), 0);
    tmp_put(d, "state/starting", "a very long description that does not fit");
    CHECK_EQ(jk_session_take_failure(d, what, sizeof what), 1);
    CHECK_EQ(strlen(what), 7);
    tmp_rm_rf(d);
}

int main(void)
{
    RUN(clean_start_leaves_nothing);
    RUN(crash_before_first_frame_is_reported_once);
    RUN(first_frame_without_marker_is_harmless);
    return jk_test_report("test_session");
}
