/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Minimal test helpers for the launcher host tests. */
#ifndef JK_TEST_H
#define JK_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int jk_test_failures, jk_test_checks;

#define CHECK(cond) do { jk_test_checks++; if (!(cond)) { jk_test_failures++; \
    fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); jk_test_checks++; \
    if (_a != _b) { jk_test_failures++; fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
/* Audit 20: a wall-clock budget. A miss prints "WARN timing" and does not fail
 * the run (a loaded host is slow, not wrong), unless JK_PERF_STRICT=1. */
#define CHECK_TIMING(cond) do { jk_test_checks++; if (!(cond)) { const char *_strict = getenv("JK_PERF_STRICT"); \
    if (_strict && !strcmp(_strict, "1")) { jk_test_failures++; fprintf(stderr, "  FAIL timing %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    else fprintf(stderr, "  WARN timing %s:%d: %s (JK_PERF_STRICT=1 makes this a failure)\n", __FILE__, __LINE__, #cond); } } while (0)
#define RUN(fn) do { int _before = jk_test_failures; fn(); \
    fprintf(stderr, "%s %s\n", jk_test_failures == _before ? "ok  " : "FAIL", #fn); } while (0)

static int jk_test_report(const char *name)
{
    fprintf(stderr, "%s: %d checks, %d failed\n", name, jk_test_checks, jk_test_failures);
    return jk_test_failures ? 1 : 0;
}

#endif
