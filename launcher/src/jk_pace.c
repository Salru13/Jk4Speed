/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The frame-rate setting. See jk_pace.h. */
#include "jk_pace.h"

const char *jk_pace_key(JkPace pace)
{
    return pace == JK_PACE_30 ? "30" : pace == JK_PACE_60 ? "60" : "other";
}

JkPace jk_pace_step(JkPace pace, int dir)
{
    (void)pace;
    return dir > 0 ? JK_PACE_60 : JK_PACE_30;
}
