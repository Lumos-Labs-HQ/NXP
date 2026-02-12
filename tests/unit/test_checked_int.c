/*
 * Unit tests for NXP Checked Integer Arithmetic
 */
#include "test_framework.h"
#include "checked_int.h"

NXP_TEST(checked_add_no_overflow) {
    uint64_t result;
    NXP_ASSERT(!nxp_checked_add_u64(&result, 10, 20));
    NXP_ASSERT_EQ(result, 30);

    NXP_ASSERT(!nxp_checked_add_u64(&result, 0, 0));
    NXP_ASSERT_EQ(result, 0);
}

NXP_TEST(checked_add_overflow) {
    uint64_t result;
    bool overflow = nxp_checked_add_u64(&result, UINT64_MAX, 1);
    NXP_ASSERT(overflow);
}

NXP_TEST(checked_sub_no_underflow) {
    uint64_t result;
    NXP_ASSERT(!nxp_checked_sub_u64(&result, 100, 50));
    NXP_ASSERT_EQ(result, 50);

    NXP_ASSERT(!nxp_checked_sub_u64(&result, 50, 50));
    NXP_ASSERT_EQ(result, 0);
}

NXP_TEST(checked_sub_underflow) {
    uint64_t result;
    bool underflow = nxp_checked_sub_u64(&result, 5, 10);
    NXP_ASSERT(underflow);
}

NXP_TEST(checked_mul_no_overflow) {
    uint64_t result;
    NXP_ASSERT(!nxp_checked_mul_u64(&result, 100, 200));
    NXP_ASSERT_EQ(result, 20000);

    NXP_ASSERT(!nxp_checked_mul_u64(&result, 0, UINT64_MAX));
    NXP_ASSERT_EQ(result, 0);
}

NXP_TEST(checked_mul_overflow) {
    uint64_t result;
    bool overflow = nxp_checked_mul_u64(&result, UINT64_MAX, 2);
    NXP_ASSERT(overflow);
}

NXP_TEST(checked_add_size) {
    size_t result;
    NXP_ASSERT(!nxp_checked_add_size(&result, 100, 200));
    NXP_ASSERT_EQ(result, 300);

    bool overflow = nxp_checked_add_size(&result, SIZE_MAX, 1);
    NXP_ASSERT(overflow);
}

int main(void) {
    printf("=== Checked Integer Tests ===\n");
    NXP_RUN_TEST(checked_add_no_overflow);
    NXP_RUN_TEST(checked_add_overflow);
    NXP_RUN_TEST(checked_sub_no_underflow);
    NXP_RUN_TEST(checked_sub_underflow);
    NXP_RUN_TEST(checked_mul_no_overflow);
    NXP_RUN_TEST(checked_mul_overflow);
    NXP_RUN_TEST(checked_add_size);
    NXP_TEST_SUMMARY();
}
