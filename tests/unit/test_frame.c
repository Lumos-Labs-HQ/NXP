/*
 * Unit tests for NXP Frame Engine (all frame types encode/decode)
 */
#include "test_framework.h"
#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"
#include "core/frame_internal.h"
#include <string.h>

/* ── Padding & Ping ── */

NXP_TEST(frame_padding_roundtrip) {
    uint8_t buf[64];
    size_t written = nxp_frame_encode_padding(10, buf, sizeof(buf));
    NXP_ASSERT_EQ(written, (size_t)10);

    /* All bytes should be zero */
    for (size_t i = 0; i < 10; i++) {
        NXP_ASSERT_EQ(buf[i], 0);
    }

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT(consumed > 0);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_PADDING);
    NXP_ASSERT_EQ(f.padding.count, (uint64_t)10);
}

NXP_TEST(frame_ping_roundtrip) {
    uint8_t buf[8];
    size_t written = nxp_frame_encode_ping(buf, sizeof(buf));
    NXP_ASSERT_EQ(written, (size_t)1);
    NXP_ASSERT_EQ(buf[0], NXP_FRAME_PING);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, (size_t)1);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_PING);
}

/* ── ACK ── */

NXP_TEST(frame_ack_simple_roundtrip) {
    nxp_frame_ack ack = {
        .largest_acked  = 100,
        .ack_delay      = 500,
        .first_ack_range = 10,
        .range_count    = 0,
        .has_ecn        = false,
    };

    uint8_t buf[128];
    size_t written = nxp_frame_encode_ack(&ack, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_ACK);
    NXP_ASSERT_EQ(f.ack.largest_acked, (uint64_t)100);
    NXP_ASSERT_EQ(f.ack.ack_delay, (uint64_t)500);
    NXP_ASSERT_EQ(f.ack.first_ack_range, (uint64_t)10);
    NXP_ASSERT_EQ(f.ack.range_count, (uint32_t)0);
    NXP_ASSERT(!f.ack.has_ecn);
}

NXP_TEST(frame_ack_with_ranges) {
    nxp_frame_ack ack = {
        .largest_acked  = 500,
        .ack_delay      = 100,
        .first_ack_range = 5,
        .range_count    = 2,
        .has_ecn        = false,
    };
    ack.ranges[0] = (nxp_ack_range){.gap = 3, .ack_range = 2};
    ack.ranges[1] = (nxp_ack_range){.gap = 10, .ack_range = 7};

    uint8_t buf[256];
    size_t written = nxp_frame_encode_ack(&ack, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.ack.range_count, (uint32_t)2);
    NXP_ASSERT_EQ(f.ack.ranges[0].gap, (uint64_t)3);
    NXP_ASSERT_EQ(f.ack.ranges[0].ack_range, (uint64_t)2);
    NXP_ASSERT_EQ(f.ack.ranges[1].gap, (uint64_t)10);
    NXP_ASSERT_EQ(f.ack.ranges[1].ack_range, (uint64_t)7);
}

NXP_TEST(frame_ack_ecn_roundtrip) {
    nxp_frame_ack ack = {
        .largest_acked  = 200,
        .ack_delay      = 50,
        .first_ack_range = 0,
        .range_count    = 0,
        .has_ecn        = true,
        .ect0_count     = 10,
        .ect1_count     = 20,
        .ecn_ce_count   = 1,
    };

    uint8_t buf[128];
    size_t written = nxp_frame_encode_ack(&ack, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_ACK_ECN);
    NXP_ASSERT(f.ack.has_ecn);
    NXP_ASSERT_EQ(f.ack.ect0_count, (uint64_t)10);
    NXP_ASSERT_EQ(f.ack.ect1_count, (uint64_t)20);
    NXP_ASSERT_EQ(f.ack.ecn_ce_count, (uint64_t)1);
}

/* ── Stream Frames ── */

NXP_TEST(frame_stream_basic) {
    uint8_t data[] = "Hello, NXP!";
    nxp_frame_stream stream = {
        .stream_id  = 4,
        .offset     = 0,
        .length     = sizeof(data) - 1,
        .has_offset = false,
        .has_length = true,
        .fin        = false,
        .data       = data,
    };

    uint8_t buf[64];
    size_t written = nxp_frame_encode_stream(&stream, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_STREAM);
    NXP_ASSERT_EQ(f.stream.stream_id, (uint64_t)4);
    NXP_ASSERT(!f.stream.has_offset);
    NXP_ASSERT(f.stream.has_length);
    NXP_ASSERT(!f.stream.fin);
    NXP_ASSERT_EQ(f.stream.length, sizeof(data) - 1);
    NXP_ASSERT(memcmp(f.stream.data, data, sizeof(data) - 1) == 0);
}

NXP_TEST(frame_stream_with_offset_and_fin) {
    uint8_t data[] = {0xDE, 0xAD};
    nxp_frame_stream stream = {
        .stream_id  = 100,
        .offset     = 4096,
        .length     = 2,
        .has_offset = true,
        .has_length = true,
        .fin        = true,
        .data       = data,
    };

    uint8_t buf[64];
    size_t written = nxp_frame_encode_stream(&stream, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    /* Check type byte has correct bits set */
    NXP_ASSERT(nxp_frame_is_stream(buf[0]));
    NXP_ASSERT(buf[0] & NXP_FRAME_STREAM_OFF);
    NXP_ASSERT(buf[0] & NXP_FRAME_STREAM_LEN);
    NXP_ASSERT(buf[0] & NXP_FRAME_STREAM_FIN);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT(f.stream.has_offset);
    NXP_ASSERT_EQ(f.stream.offset, (uint64_t)4096);
    NXP_ASSERT(f.stream.fin);
    NXP_ASSERT(memcmp(f.stream.data, data, 2) == 0);
}

/* ── CRYPTO Frame ── */

NXP_TEST(frame_crypto_roundtrip) {
    uint8_t crypto_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    nxp_frame_crypto crypto = {
        .offset = 0,
        .length = 5,
        .data   = crypto_data,
    };

    uint8_t buf[64];
    size_t written = nxp_frame_encode_crypto(&crypto, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_CRYPTO);
    NXP_ASSERT_EQ(f.crypto.offset, (uint64_t)0);
    NXP_ASSERT_EQ(f.crypto.length, (uint64_t)5);
    NXP_ASSERT(memcmp(f.crypto.data, crypto_data, 5) == 0);
}

/* ── RESET_STREAM ── */

NXP_TEST(frame_reset_stream_roundtrip) {
    nxp_frame_reset_stream rs = {
        .stream_id  = 42,
        .error_code = 7,
        .final_size = 10000,
    };

    uint8_t buf[64];
    size_t written = nxp_frame_encode_reset_stream(&rs, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_RESET_STREAM);
    NXP_ASSERT_EQ(f.reset_stream.stream_id, (uint64_t)42);
    NXP_ASSERT_EQ(f.reset_stream.error_code, (uint64_t)7);
    NXP_ASSERT_EQ(f.reset_stream.final_size, (uint64_t)10000);
}

/* ── STOP_SENDING ── */

NXP_TEST(frame_stop_sending_roundtrip) {
    nxp_frame_stop_sending ss = {
        .stream_id  = 99,
        .error_code = 3,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_stop_sending(&ss, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_STOP_SENDING);
    NXP_ASSERT_EQ(f.stop_sending.stream_id, (uint64_t)99);
    NXP_ASSERT_EQ(f.stop_sending.error_code, (uint64_t)3);
}

/* ── Flow Control Frames ── */

NXP_TEST(frame_max_data_roundtrip) {
    uint8_t buf[32];
    size_t written = nxp_frame_encode_max_data(1000000, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_MAX_DATA);
    NXP_ASSERT_EQ(f.max_data.max_data, (uint64_t)1000000);
}

NXP_TEST(frame_max_stream_data_roundtrip) {
    nxp_frame_max_stream_data msd = {
        .stream_id       = 8,
        .max_stream_data = 65536,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_max_stream_data(&msd, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_MAX_STREAM_DATA);
    NXP_ASSERT_EQ(f.max_stream_data.stream_id, (uint64_t)8);
    NXP_ASSERT_EQ(f.max_stream_data.max_stream_data, (uint64_t)65536);
}

NXP_TEST(frame_max_streams_bidi) {
    nxp_frame_max_streams ms = { .max_streams = 128, .is_bidi = true };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_max_streams(&ms, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_MAX_STREAMS_BIDI);
    NXP_ASSERT(f.max_streams.is_bidi);
    NXP_ASSERT_EQ(f.max_streams.max_streams, (uint64_t)128);
}

NXP_TEST(frame_max_streams_uni) {
    nxp_frame_max_streams ms = { .max_streams = 64, .is_bidi = false };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_max_streams(&ms, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_MAX_STREAMS_UNI);
    NXP_ASSERT(!f.max_streams.is_bidi);
}

/* ── Blocked Frames ── */

NXP_TEST(frame_data_blocked_roundtrip) {
    uint8_t buf[32];
    size_t written = nxp_frame_encode_data_blocked(4096, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_DATA_BLOCKED);
    NXP_ASSERT_EQ(f.data_blocked.max_data, (uint64_t)4096);
}

NXP_TEST(frame_stream_data_blocked_roundtrip) {
    nxp_frame_stream_data_blocked sdb = {
        .stream_id       = 16,
        .max_stream_data = 8192,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_stream_data_blocked(&sdb, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_STREAM_DATA_BLOCKED);
    NXP_ASSERT_EQ(f.stream_data_blocked.stream_id, (uint64_t)16);
    NXP_ASSERT_EQ(f.stream_data_blocked.max_stream_data, (uint64_t)8192);
}

/* ── Connection ID Frames ── */

NXP_TEST(frame_new_connection_id_roundtrip) {
    nxp_frame_new_connection_id ncid = {
        .seq_num         = 1,
        .retire_prior_to = 0,
    };
    ncid.cid.len = 8;
    memset(ncid.cid.data, 0xAA, 8);
    memset(ncid.stateless_reset_token, 0xBB, 16);

    uint8_t buf[64];
    size_t written = nxp_frame_encode_new_connection_id(&ncid, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_NEW_CONNECTION_ID);
    NXP_ASSERT_EQ(f.new_connection_id.seq_num, (uint64_t)1);
    NXP_ASSERT_EQ(f.new_connection_id.retire_prior_to, (uint64_t)0);
    NXP_ASSERT_EQ(f.new_connection_id.cid.len, (uint8_t)8);
    NXP_ASSERT(memcmp(f.new_connection_id.cid.data, ncid.cid.data, 8) == 0);
    NXP_ASSERT(memcmp(f.new_connection_id.stateless_reset_token, ncid.stateless_reset_token, 16) == 0);
}

NXP_TEST(frame_retire_connection_id_roundtrip) {
    uint8_t buf[32];
    size_t written = nxp_frame_encode_retire_connection_id(5, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_RETIRE_CONNECTION_ID);
    NXP_ASSERT_EQ(f.retire_connection_id.seq_num, (uint64_t)5);
}

/* ── Path Frames ── */

NXP_TEST(frame_path_challenge_roundtrip) {
    uint8_t challenge_data[8] = {1,2,3,4,5,6,7,8};

    uint8_t buf[32];
    size_t written = nxp_frame_encode_path_challenge(challenge_data, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_PATH_CHALLENGE);
    NXP_ASSERT(memcmp(f.path_challenge.data, challenge_data, 8) == 0);
}

NXP_TEST(frame_path_response_roundtrip) {
    uint8_t response_data[8] = {8,7,6,5,4,3,2,1};

    uint8_t buf[32];
    size_t written = nxp_frame_encode_path_response(response_data, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_PATH_RESPONSE);
    NXP_ASSERT(memcmp(f.path_response.data, response_data, 8) == 0);
}

/* ── Connection Close ── */

NXP_TEST(frame_connection_close_transport) {
    const char *reason = "test error";
    nxp_frame_connection_close cc = {
        .error_code = 0x0A,
        .frame_type = NXP_FRAME_STREAM,
        .reason_len = strlen(reason),
        .reason     = (const uint8_t *)reason,
        .is_app     = false,
    };

    uint8_t buf[64];
    size_t written = nxp_frame_encode_connection_close(&cc, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_CONNECTION_CLOSE);
    NXP_ASSERT(!f.connection_close.is_app);
    NXP_ASSERT_EQ(f.connection_close.error_code, (uint64_t)0x0A);
    NXP_ASSERT_EQ(f.connection_close.frame_type, (uint64_t)NXP_FRAME_STREAM);
    NXP_ASSERT_EQ(f.connection_close.reason_len, strlen(reason));
    NXP_ASSERT(memcmp(f.connection_close.reason, reason, strlen(reason)) == 0);
}

NXP_TEST(frame_connection_close_app) {
    nxp_frame_connection_close cc = {
        .error_code = 42,
        .reason_len = 0,
        .reason     = nullptr,
        .is_app     = true,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_connection_close(&cc, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_CONNECTION_CLOSE_APP);
    NXP_ASSERT(f.connection_close.is_app);
    NXP_ASSERT_EQ(f.connection_close.error_code, (uint64_t)42);
    NXP_ASSERT_EQ(f.connection_close.reason_len, (uint64_t)0);
}

/* ── Handshake Done ── */

NXP_TEST(frame_handshake_done_roundtrip) {
    uint8_t buf[8];
    size_t written = nxp_frame_encode_handshake_done(buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_HANDSHAKE_DONE);
}

/* ── NXP Extension Frames ── */

NXP_TEST(frame_heartbeat_roundtrip) {
    uint8_t buf[32];
    size_t written = nxp_frame_encode_heartbeat(1234567890, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_HEARTBEAT);
    NXP_ASSERT_EQ(f.heartbeat.timestamp_us, (uint64_t)1234567890);
}

NXP_TEST(frame_stream_priority_roundtrip) {
    nxp_frame_stream_priority sp = {
        .stream_id = 8,
        .priority  = 128,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_stream_priority(&sp, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_STREAM_PRIORITY);
    NXP_ASSERT_EQ(f.stream_priority.stream_id, (uint64_t)8);
    NXP_ASSERT_EQ(f.stream_priority.priority, (uint8_t)128);
}

NXP_TEST(frame_datagram_roundtrip) {
    uint8_t dgram_data[] = {0xCA, 0xFE, 0xBA, 0xBE};
    nxp_frame_datagram dg = {
        .length = 4,
        .data   = dgram_data,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_datagram(&dg, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_DATAGRAM);
    NXP_ASSERT_EQ(f.datagram.length, (uint64_t)4);
    NXP_ASSERT(memcmp(f.datagram.data, dgram_data, 4) == 0);
}

/* ── NEW_TOKEN ── */

NXP_TEST(frame_new_token_roundtrip) {
    uint8_t token[] = {0xAA, 0xBB, 0xCC, 0xDD};
    nxp_frame_new_token nt = {
        .token_len = 4,
        .token     = token,
    };

    uint8_t buf[32];
    size_t written = nxp_frame_encode_new_token(&nt, buf, sizeof(buf));
    NXP_ASSERT(written > 0);

    nxp_frame f;
    size_t consumed = nxp_frame_decode(buf, written, &f);
    NXP_ASSERT_EQ(consumed, written);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_NEW_TOKEN);
    NXP_ASSERT_EQ(f.new_token.token_len, (uint64_t)4);
    NXP_ASSERT(memcmp(f.new_token.token, token, 4) == 0);
}

/* ── Ack-Eliciting Check ── */

NXP_TEST(frame_ack_eliciting) {
    NXP_ASSERT(!nxp_frame_is_ack_eliciting(NXP_FRAME_PADDING));
    NXP_ASSERT(!nxp_frame_is_ack_eliciting(NXP_FRAME_ACK));
    NXP_ASSERT(!nxp_frame_is_ack_eliciting(NXP_FRAME_ACK_ECN));
    NXP_ASSERT(!nxp_frame_is_ack_eliciting(NXP_FRAME_CONNECTION_CLOSE));
    NXP_ASSERT(!nxp_frame_is_ack_eliciting(NXP_FRAME_CONNECTION_CLOSE_APP));

    NXP_ASSERT(nxp_frame_is_ack_eliciting(NXP_FRAME_PING));
    NXP_ASSERT(nxp_frame_is_ack_eliciting(NXP_FRAME_STREAM));
    NXP_ASSERT(nxp_frame_is_ack_eliciting(NXP_FRAME_CRYPTO));
    NXP_ASSERT(nxp_frame_is_ack_eliciting(NXP_FRAME_HEARTBEAT));
}

/* ── Multi-Frame Decode ── */

NXP_TEST(frame_multi_decode) {
    /* Encode multiple frames into one buffer */
    uint8_t buf[256];
    size_t pos = 0;
    size_t n;

    /* PING */
    n = nxp_frame_encode_ping(&buf[pos], sizeof(buf) - pos);
    NXP_ASSERT(n > 0);
    pos += n;

    /* MAX_DATA */
    n = nxp_frame_encode_max_data(999, &buf[pos], sizeof(buf) - pos);
    NXP_ASSERT(n > 0);
    pos += n;

    /* HEARTBEAT */
    n = nxp_frame_encode_heartbeat(12345, &buf[pos], sizeof(buf) - pos);
    NXP_ASSERT(n > 0);
    pos += n;

    /* Decode all three */
    size_t offset = 0;
    nxp_frame f;

    size_t consumed = nxp_frame_decode(&buf[offset], pos - offset, &f);
    NXP_ASSERT(consumed > 0);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_PING);
    offset += consumed;

    consumed = nxp_frame_decode(&buf[offset], pos - offset, &f);
    NXP_ASSERT(consumed > 0);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_MAX_DATA);
    NXP_ASSERT_EQ(f.max_data.max_data, (uint64_t)999);
    offset += consumed;

    consumed = nxp_frame_decode(&buf[offset], pos - offset, &f);
    NXP_ASSERT(consumed > 0);
    NXP_ASSERT_EQ(f.type, NXP_FRAME_HEARTBEAT);
    NXP_ASSERT_EQ(f.heartbeat.timestamp_us, (uint64_t)12345);
    offset += consumed;

    /* Should have consumed everything */
    NXP_ASSERT_EQ(offset, pos);
}

int main(void) {
    printf("=== NXP Frame Engine Tests ===\n");

    NXP_RUN_TEST(frame_padding_roundtrip);
    NXP_RUN_TEST(frame_ping_roundtrip);
    NXP_RUN_TEST(frame_ack_simple_roundtrip);
    NXP_RUN_TEST(frame_ack_with_ranges);
    NXP_RUN_TEST(frame_ack_ecn_roundtrip);
    NXP_RUN_TEST(frame_stream_basic);
    NXP_RUN_TEST(frame_stream_with_offset_and_fin);
    NXP_RUN_TEST(frame_crypto_roundtrip);
    NXP_RUN_TEST(frame_reset_stream_roundtrip);
    NXP_RUN_TEST(frame_stop_sending_roundtrip);
    NXP_RUN_TEST(frame_max_data_roundtrip);
    NXP_RUN_TEST(frame_max_stream_data_roundtrip);
    NXP_RUN_TEST(frame_max_streams_bidi);
    NXP_RUN_TEST(frame_max_streams_uni);
    NXP_RUN_TEST(frame_data_blocked_roundtrip);
    NXP_RUN_TEST(frame_stream_data_blocked_roundtrip);
    NXP_RUN_TEST(frame_new_connection_id_roundtrip);
    NXP_RUN_TEST(frame_retire_connection_id_roundtrip);
    NXP_RUN_TEST(frame_path_challenge_roundtrip);
    NXP_RUN_TEST(frame_path_response_roundtrip);
    NXP_RUN_TEST(frame_connection_close_transport);
    NXP_RUN_TEST(frame_connection_close_app);
    NXP_RUN_TEST(frame_handshake_done_roundtrip);
    NXP_RUN_TEST(frame_heartbeat_roundtrip);
    NXP_RUN_TEST(frame_stream_priority_roundtrip);
    NXP_RUN_TEST(frame_datagram_roundtrip);
    NXP_RUN_TEST(frame_new_token_roundtrip);
    NXP_RUN_TEST(frame_ack_eliciting);
    NXP_RUN_TEST(frame_multi_decode);

    NXP_TEST_SUMMARY();
}
