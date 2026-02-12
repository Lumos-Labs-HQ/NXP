/*
 * Unit tests for NXP Packet Engine (long and short header encode/decode)
 */
#include "test_framework.h"
#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include "core/packet_internal.h"
#include <string.h>

/* ── Long Header Tests ── */

NXP_TEST(long_header_initial_roundtrip) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_INITIAL,
        .version    = NXP_VERSION_1,
        .pkt_num    = 42,
        .pkt_num_len = 2,
        .payload_len = 100,
        .token      = nullptr,
        .token_len  = 0,
    };
    /* Set connection IDs */
    hdr.dcid.len = 8;
    memset(hdr.dcid.data, 0xAA, 8);
    hdr.scid.len = 8;
    memset(hdr.scid.data, 0xBB, 8);

    uint8_t buf[256];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    /* Decode */
    nxp_pkt_long_header decoded;
    nxp_result res = nxp_pkt_decode_long_header(buf, written, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.type, NXP_PKT_INITIAL);
    NXP_ASSERT_EQ(decoded.version, NXP_VERSION_1);
    NXP_ASSERT_EQ(decoded.dcid.len, 8);
    NXP_ASSERT(memcmp(decoded.dcid.data, hdr.dcid.data, 8) == 0);
    NXP_ASSERT_EQ(decoded.scid.len, 8);
    NXP_ASSERT(memcmp(decoded.scid.data, hdr.scid.data, 8) == 0);
    NXP_ASSERT_EQ(decoded.pkt_num, 42);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 2);
    NXP_ASSERT_EQ(decoded.payload_len, 100);
    NXP_ASSERT_EQ(decoded.token_len, 0);
    NXP_ASSERT_NULL(decoded.token);
}

NXP_TEST(long_header_initial_with_token) {
    uint8_t token_data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_INITIAL,
        .version    = NXP_VERSION_1,
        .pkt_num    = 0,
        .pkt_num_len = 1,
        .payload_len = 50,
        .token      = token_data,
        .token_len  = sizeof(token_data),
    };
    hdr.dcid.len = 4;
    memset(hdr.dcid.data, 0x11, 4);
    hdr.scid.len = 4;
    memset(hdr.scid.data, 0x22, 4);

    uint8_t buf[256];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_pkt_long_header decoded;
    nxp_result res = nxp_pkt_decode_long_header(buf, written, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.token_len, 6);
    NXP_ASSERT_NOT_NULL(decoded.token);
    NXP_ASSERT(memcmp(decoded.token, token_data, 6) == 0);
    NXP_ASSERT_EQ(decoded.pkt_num, 0);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 1);
}

NXP_TEST(long_header_handshake_roundtrip) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_HANDSHAKE,
        .version    = NXP_VERSION_1,
        .pkt_num    = 1000,
        .pkt_num_len = 4,
        .payload_len = 200,
    };
    hdr.dcid.len = 16;
    memset(hdr.dcid.data, 0xCC, 16);
    hdr.scid.len = 16;
    memset(hdr.scid.data, 0xDD, 16);

    uint8_t buf[256];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_pkt_long_header decoded;
    nxp_result res = nxp_pkt_decode_long_header(buf, written, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.type, NXP_PKT_HANDSHAKE);
    NXP_ASSERT_EQ(decoded.pkt_num, 1000);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 4);
    NXP_ASSERT_EQ(decoded.payload_len, 200);
}

NXP_TEST(long_header_zero_rtt) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_ZERO_RTT,
        .version    = NXP_VERSION_1,
        .pkt_num    = 7,
        .pkt_num_len = 1,
        .payload_len = 500,
    };
    hdr.dcid.len = 0;
    hdr.scid.len = 0;

    uint8_t buf[256];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_pkt_long_header decoded;
    nxp_result res = nxp_pkt_decode_long_header(buf, written, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.type, NXP_PKT_ZERO_RTT);
    NXP_ASSERT_EQ(decoded.dcid.len, 0);
    NXP_ASSERT_EQ(decoded.scid.len, 0);
    NXP_ASSERT_EQ(decoded.pkt_num, 7);
}

NXP_TEST(long_header_retry) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_RETRY,
        .version    = NXP_VERSION_1,
    };
    hdr.dcid.len = 8;
    memset(hdr.dcid.data, 0x55, 8);
    hdr.scid.len = 8;
    memset(hdr.scid.data, 0x66, 8);

    uint8_t buf[256];
    /* Retry: form=1, fixed=1, type=10, reserved=00, pn_len=00 -> 0xE0
     * But encode will set pkt_num_len to 0 bits (special for retry) */
    hdr.pkt_num_len = 1; /* Encoder needs at least 1, but retry skips pn */
    hdr.payload_len = 0;

    /* Manual construction for retry since encoder uses pkt_num_len */
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_pkt_long_header decoded;
    nxp_result res = nxp_pkt_decode_long_header(buf, written, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.type, NXP_PKT_RETRY);
    NXP_ASSERT_EQ(decoded.dcid.len, 8);
    NXP_ASSERT_EQ(decoded.scid.len, 8);
    /* Retry has no packet number */
    NXP_ASSERT_EQ(decoded.pkt_num, 0);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 0);
}

/* ── Short Header Tests ── */

NXP_TEST(short_header_roundtrip) {
    nxp_pkt_short_header hdr = {
        .spin_bit   = true,
        .key_phase  = false,
        .pkt_num    = 256,
        .pkt_num_len = 2,
    };
    hdr.dcid.len = 8;
    memset(hdr.dcid.data, 0x77, 8);

    uint8_t buf[64];
    size_t written = nxp_pkt_encode_short_header(&hdr, 8, buf, sizeof(buf));
    NXP_ASSERT(written > 0);
    NXP_ASSERT_EQ(written, (size_t)(1 + 8 + 2));

    nxp_pkt_short_header decoded;
    nxp_result res = nxp_pkt_decode_short_header(buf, written, 8, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.spin_bit, true);
    NXP_ASSERT_EQ(decoded.key_phase, false);
    NXP_ASSERT_EQ(decoded.pkt_num, 256);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 2);
    NXP_ASSERT_EQ(decoded.dcid.len, 8);
    NXP_ASSERT(memcmp(decoded.dcid.data, hdr.dcid.data, 8) == 0);
}

NXP_TEST(short_header_key_phase) {
    nxp_pkt_short_header hdr = {
        .spin_bit   = false,
        .key_phase  = true,
        .pkt_num    = 0xFFFF,
        .pkt_num_len = 4,
    };
    hdr.dcid.len = 0;

    uint8_t buf[64];
    size_t written = nxp_pkt_encode_short_header(&hdr, 0, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_pkt_short_header decoded;
    nxp_result res = nxp_pkt_decode_short_header(buf, written, 0, &decoded);
    NXP_ASSERT_OK(res);

    NXP_ASSERT_EQ(decoded.spin_bit, false);
    NXP_ASSERT_EQ(decoded.key_phase, true);
    NXP_ASSERT_EQ(decoded.pkt_num, 0xFFFF);
    NXP_ASSERT_EQ(decoded.pkt_num_len, 4);
}

NXP_TEST(short_header_first_byte) {
    /* Verify first byte bits are correct */
    nxp_pkt_short_header hdr = {
        .spin_bit   = true,
        .key_phase  = true,
        .pkt_num    = 1,
        .pkt_num_len = 1,
    };
    hdr.dcid.len = 0;

    uint8_t buf[64];
    size_t written = nxp_pkt_encode_short_header(&hdr, 0, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    /* form=0, fixed=1, spin=1, key_phase=1, reserved=00, pn_len=00 */
    /* 0x70 = 0b01110000 */
    NXP_ASSERT_EQ(buf[0], 0x70);

    /* Verify it's detected as short header */
    NXP_ASSERT(!nxp_pkt_is_long_header(buf[0]));
}

NXP_TEST(long_header_first_byte) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_INITIAL,
        .version    = NXP_VERSION_1,
        .pkt_num    = 0,
        .pkt_num_len = 1,
        .payload_len = 10,
    };
    hdr.dcid.len = 0;
    hdr.scid.len = 0;

    uint8_t buf[64];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    /* form=1, fixed=1, type=00, reserved=00, pn_len=00 */
    /* 0xC0 = 0b11000000 */
    NXP_ASSERT_EQ(buf[0], 0xC0);
    NXP_ASSERT(nxp_pkt_is_long_header(buf[0]));
}

/* ── Packet Number Reconstruction Tests ── */

NXP_TEST(pkt_num_decode_simple) {
    /* Largest acked = 0, truncated = 5, 1 byte */
    uint64_t result = nxp_pkt_decode_pkt_num(0, 5, 1);
    NXP_ASSERT_EQ(result, 5);
}

NXP_TEST(pkt_num_decode_wrap) {
    /* Largest acked = 0xAA82F30E, truncated = 0x9B32, 2 bytes */
    uint64_t result = nxp_pkt_decode_pkt_num(0xAA82F30E, 0x9B32, 2);
    NXP_ASSERT_EQ(result, 0xAA829B32);
}

NXP_TEST(pkt_num_len_small) {
    NXP_ASSERT_EQ(nxp_pkt_num_len(10, 0), 1);
    NXP_ASSERT_EQ(nxp_pkt_num_len(127, 0), 1);
    NXP_ASSERT_EQ(nxp_pkt_num_len(128, 0), 2);
    NXP_ASSERT_EQ(nxp_pkt_num_len(1000, 900), 1);
}

/* ── Error Cases ── */

NXP_TEST(decode_too_short) {
    uint8_t buf[] = {0xC0}; /* Only 1 byte, need at least 7 */
    nxp_pkt_long_header hdr;
    nxp_result res = nxp_pkt_decode_long_header(buf, 1, &hdr);
    NXP_ASSERT(nxp_result_is_err(res));
}

NXP_TEST(decode_bad_form_bit) {
    /* Try to decode a short header as long */
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x40; /* form=0, fixed=1 */
    nxp_pkt_long_header hdr;
    nxp_result res = nxp_pkt_decode_long_header(buf, sizeof(buf), &hdr);
    NXP_ASSERT(nxp_result_is_err(res));
}

NXP_TEST(encode_invalid_pkt_num_len) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_INITIAL,
        .version    = NXP_VERSION_1,
        .pkt_num    = 0,
        .pkt_num_len = 5, /* Invalid: must be 1-4 */
        .payload_len = 10,
    };
    uint8_t buf[256];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT_EQ(written, (size_t)0);
}

NXP_TEST(version_encoding) {
    nxp_pkt_long_header hdr = {
        .type       = NXP_PKT_INITIAL,
        .version    = NXP_VERSION_1,
        .pkt_num    = 0,
        .pkt_num_len = 1,
        .payload_len = 10,
    };
    hdr.dcid.len = 0;
    hdr.scid.len = 0;

    uint8_t buf[64];
    size_t written = nxp_pkt_encode_long_header(&hdr, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    /* Version bytes should be 0x4E585001 = 'N','X','P','\x01' */
    NXP_ASSERT_EQ(buf[1], 0x4E);
    NXP_ASSERT_EQ(buf[2], 0x58);
    NXP_ASSERT_EQ(buf[3], 0x50);
    NXP_ASSERT_EQ(buf[4], 0x01);
}

int main(void) {
    printf("=== NXP Packet Engine Tests ===\n");

    NXP_RUN_TEST(long_header_initial_roundtrip);
    NXP_RUN_TEST(long_header_initial_with_token);
    NXP_RUN_TEST(long_header_handshake_roundtrip);
    NXP_RUN_TEST(long_header_zero_rtt);
    NXP_RUN_TEST(long_header_retry);
    NXP_RUN_TEST(short_header_roundtrip);
    NXP_RUN_TEST(short_header_key_phase);
    NXP_RUN_TEST(short_header_first_byte);
    NXP_RUN_TEST(long_header_first_byte);
    NXP_RUN_TEST(pkt_num_decode_simple);
    NXP_RUN_TEST(pkt_num_decode_wrap);
    NXP_RUN_TEST(pkt_num_len_small);
    NXP_RUN_TEST(decode_too_short);
    NXP_RUN_TEST(decode_bad_form_bit);
    NXP_RUN_TEST(encode_invalid_pkt_num_len);
    NXP_RUN_TEST(version_encoding);

    NXP_TEST_SUMMARY();
}
