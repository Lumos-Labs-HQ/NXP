/*
 * Unit tests for NXP CRC32C
 */
#include "test_framework.h"
#include "crc32c.h"
#include <string.h>

NXP_TEST(crc32c_empty) {
    uint32_t crc = nxp_crc32c(nullptr, 0);
    /* CRC32C of empty input should be 0 */
    NXP_ASSERT_EQ(crc, (uint32_t)0);
}

NXP_TEST(crc32c_known_values) {
    /* Well-known CRC32C test vectors */

    /* "123456789" -> 0xE3069283 */
    const uint8_t *data = (const uint8_t *)"123456789";
    uint32_t crc = nxp_crc32c(data, 9);
    NXP_ASSERT_EQ(crc, (uint32_t)0xE3069283);
}

NXP_TEST(crc32c_zeros) {
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    uint32_t crc = nxp_crc32c(zeros, sizeof(zeros));
    /* Just verify it's not zero (non-trivial) */
    NXP_ASSERT_NE(crc, (uint32_t)0);
}

NXP_TEST(crc32c_incremental) {
    /* CRC computed incrementally should equal CRC computed at once */
    const uint8_t *data = (const uint8_t *)"Hello, NXP protocol!";
    size_t total_len = strlen((const char *)data);

    uint32_t crc_once = nxp_crc32c(data, total_len);

    /* Split at multiple points */
    uint32_t crc_inc = nxp_crc32c_update(0, data, 6);
    crc_inc = nxp_crc32c_update(crc_inc, data + 6, total_len - 6);

    NXP_ASSERT_EQ(crc_once, crc_inc);
}

NXP_TEST(crc32c_single_byte) {
    uint8_t byte = 0xFF;
    uint32_t crc = nxp_crc32c(&byte, 1);
    /* Should produce a valid non-zero checksum */
    NXP_ASSERT_NE(crc, (uint32_t)0);
}

NXP_TEST(crc32c_different_data_different_crc) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};
    uint32_t crc1 = nxp_crc32c(data1, 3);
    uint32_t crc2 = nxp_crc32c(data2, 3);
    NXP_ASSERT_NE(crc1, crc2);
}

int main(void) {
    printf("=== NXP CRC32C Tests ===\n");

    NXP_RUN_TEST(crc32c_empty);
    NXP_RUN_TEST(crc32c_known_values);
    NXP_RUN_TEST(crc32c_zeros);
    NXP_RUN_TEST(crc32c_incremental);
    NXP_RUN_TEST(crc32c_single_byte);
    NXP_RUN_TEST(crc32c_different_data_different_crc);

    NXP_TEST_SUMMARY();
}
