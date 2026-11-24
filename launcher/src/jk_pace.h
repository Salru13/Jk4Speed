/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The frame-rate setting (owner decision D6): 30 or 60. r4: the choice lives
 * in the launcher's settings (jk_config.h, <data_root>/state/launcher.ini);
 * the launcher composes DXVK_PS4_NFS_PACE into <game_dir>/jk4speed-dev.ini
 * from it, read by the Native DXVK client (perf007a; racing only).
 */
#ifndef JK_PACE_H
#define JK_PACE_H

typedef enum JkPace { JK_PACE_30 = 0, JK_PACE_60, JK_PACE_OTHER } JkPace;

/* Left (-1) to 30, right (+1) to 60; no wrapping. */
JkPace jk_pace_step(JkPace pace, int dir);
/* "30", "60", "other". */
const char *jk_pace_key(JkPace pace);

#endif
