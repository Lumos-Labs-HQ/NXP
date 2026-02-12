/*
 * NXP Minimal Test Framework
 *
 * Simple, dependency-free test harness for C23.
 */
#ifndef NXP_TEST_FRAMEWORK_H
#define NXP_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define NXP_TEST(name) static void test_##name(void)

#define NXP_RUN_TEST(name) do { \
    g_tests_run++; \
    printf("  [RUN ] %s ...", #name); \
    fflush(stdout); \
    test_##name(); \
    g_tests_passed++; \
    printf(" PASS\n"); \
} while(0)

#define NXP_ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        g_tests_failed++; \
        g_tests_passed--; \
        return; \
    } \
} while(0)

#define NXP_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf(" FAIL\n    Expected %lld == %lld\n    at %s:%d\n", \
               (long long)(a), (long long)(b), __FILE__, __LINE__); \
        g_tests_failed++; \
        g_tests_passed--; \
        return; \
    } \
} while(0)

#define NXP_ASSERT_NE(a, b) do { \
    if ((a) == (b)) { \
        printf(" FAIL\n    Expected %lld != %lld\n    at %s:%d\n", \
               (long long)(a), (long long)(b), __FILE__, __LINE__); \
        g_tests_failed++; \
        g_tests_passed--; \
        return; \
    } \
} while(0)

#define NXP_ASSERT_NOT_NULL(ptr) NXP_ASSERT((ptr) != nullptr)
#define NXP_ASSERT_NULL(ptr)     NXP_ASSERT((ptr) == nullptr)

#define NXP_ASSERT_OK(result)    NXP_ASSERT(nxp_result_is_ok(result))

#define NXP_TEST_SUMMARY() do { \
    printf("\n=== Test Summary ===\n"); \
    printf("  Total:  %d\n", g_tests_run); \
    printf("  Passed: %d\n", g_tests_passed); \
    printf("  Failed: %d\n", g_tests_failed); \
    return g_tests_failed > 0 ? 1 : 0; \
} while(0)

#endif /* NXP_TEST_FRAMEWORK_H */
