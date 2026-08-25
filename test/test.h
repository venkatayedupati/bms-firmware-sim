#ifndef TEST_H
#define TEST_H

#include <stdio.h>

/*
 * Minimal, dependency-free unit test macros. No submodules to fetch, no
 * package manager -- `make test` works offline on any machine with a C
 * compiler.
 */

extern int g_tests_run;
extern int g_tests_failed;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        g_tests_failed++; \
        printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

#define TEST_ASSERT_EQ_INT(expected, actual, msg) do { \
    g_tests_run++; \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        g_tests_failed++; \
        printf("  FAIL: %s: expected %lld, got %lld (%s:%d)\n", \
               msg, _e, _a, __FILE__, __LINE__); \
    } \
} while (0)

#define RUN_SUITE(fn) do { printf("-- %s --\n", #fn); fn(); } while (0)

#endif /* TEST_H */
