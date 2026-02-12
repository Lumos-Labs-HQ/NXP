/*
 * Unit tests for NXP Variable-Length Integer Encoding
 */
#include "test_framework.h"
#include "varint.h"

NXP_TEST(varint_encode_1byte) {
    uint8_t buf[8];
    NXP_ASSERT_EQ(nxp_varint_encode(0, buf, sizeof(buf)), 1);
    NXP_ASSERT_EQ(buf[0], 0x00);

    NXP_ASSERT_EQ(nxp_varint_encode(63, buf, sizeof(buf)), 1);
    NXP_ASSERT_EQ(buf[0], 0x3F);

    NXP_ASSERT_EQ(nxp_varint_encode(37, buf, sizeof(buf)), 1);
    NXP_ASSERT_EQ(buf[0], 37);
}

NXP_TEST(varint_encode_2byte) {
    uint8_t buf[8];
    NXP_ASSERT_EQ(nxp_varint_encode(64, buf, sizeof(buf)), 2);
    NXP_ASSERT_EQ(buf[0] >> 6, 1);  /* prefix = 01 */

    NXP_ASSERT_EQ(nxp_varint_encode(16383, buf, sizeof(buf)), 2);
    NXP_ASSERT_EQ(buf[0], 0x7F);
    NXP_ASSERT_EQ(buf[1], 0xFF);
}

NXP_TEST(varint_encode_4byte) {
    uint8_t buf[8];
    NXP_ASSERT_EQ(nxp_varint_encode(16384, buf, sizeof(buf)), 4);
    NXP_ASSERT_EQ(buf[0] >> 6, 2);  /* prefix = 10 */

    NXP_ASSERT_EQ(nxp_varint_encode(1073741823, buf, sizeof(buf)), 4);
}

NXP_TEST(varint_encode_8byte) {
    uint8_t buf[8];
    NXP_ASSERT_EQ(nxp_varint_encode(1073741824, buf, sizeof(buf)), 8);
    NXP_ASSERT_EQ(buf[0] >> 6, 3);  /* prefix = 11 */

    NXP_ASSERT_EQ(nxp_varint_encode(NXP_VARINT_MAX, buf, sizeof(buf)), 8);
}

NXP_TEST(varint_encode_too_large) {
    uint8_t buf[8];
    NXP_ASSERT_EQ(nxp_varint_encode(NXP_VARINT_MAX + 1, buf, sizeof(buf)), 0);
}

NXP_TEST(varint_encode_buffer_too_small) {
    uint8_t buf[1];
    NXP_ASSERT_EQ(nxp_varint_encode(64, buf, 1), 0);  /* Needs 2 bytes */
}

NXP_TEST(varint_roundtrip) {
    uint64_t test_values[] = {
        0, 1, 63, 64, 16383, 16384, 1073741823, 1073741824,
        NXP_VARINT_MAX, 255, 256, 100000, 999999999
    };
    size_t count = sizeof(test_values) / sizeof(test_values[0]);

    uint8_t buf[8];
    for (size_t i = 0; i < count; i++) {
        memset(buf, 0xCC, sizeof(buf));

        size_t enc_len = nxp_varint_encode(test_values[i], buf, sizeof(buf));
        NXP_ASSERT(enc_len > 0);

        uint64_t decoded;
        size_t dec_len = nxp_varint_decode(buf, sizeof(buf), &decoded);
        NXP_ASSERT_EQ(dec_len, enc_len);
        NXP_ASSERT_EQ(decoded, test_values[i]);
    }
}

NXP_TEST(varint_len_function) {
    NXP_ASSERT_EQ(nxp_varint_len(0), 1);
    NXP_ASSERT_EQ(nxp_varint_len(63), 1);
    NXP_ASSERT_EQ(nxp_varint_len(64), 2);
    NXP_ASSERT_EQ(nxp_varint_len(16383), 2);
    NXP_ASSERT_EQ(nxp_varint_len(16384), 4);
    NXP_ASSERT_EQ(nxp_varint_len(1073741823), 4);
    NXP_ASSERT_EQ(nxp_varint_len(1073741824), 8);
    NXP_ASSERT_EQ(nxp_varint_len(NXP_VARINT_MAX), 8);
    NXP_ASSERT_EQ(nxp_varint_len(NXP_VARINT_MAX + 1), 0);
}

int main(void) {
    printf("=== Varint Tests ===\n");
    NXP_RUN_TEST(varint_encode_1byte);
    NXP_RUN_TEST(varint_encode_2byte);
    NXP_RUN_TEST(varint_encode_4byte);
    NXP_RUN_TEST(varint_encode_8byte);
    NXP_RUN_TEST(varint_encode_too_large);
    NXP_RUN_TEST(varint_encode_buffer_too_small);
    NXP_RUN_TEST(varint_roundtrip);
    NXP_RUN_TEST(varint_len_function);
    NXP_TEST_SUMMARY();
}
