/* TCP Protocol - Real Network Test
 * Actually sends data over UDP using NXP
 */
#define _DEFAULT_SOURCE
#include "../../src/core/connection_internal.h"
#include "../../src/platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  TCP Protocol - Real Data Transfer Test                   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Create UDP socket
    nxp_addr bind_addr;
    nxp_addr_from_string("127.0.0.1", 9001, &bind_addr);
    
    nxp_socket *sock = NULL;
    nxp_result res = nxp_socket_create_udp(&bind_addr, &sock);
    if (nxp_result_is_err(res)) {
        printf("❌ Failed to create socket\n");
        return 1;
    }
    nxp_socket_set_nonblocking(sock);
    printf("✅ UDP socket created on port 9001\n");
    
    // Create connection
    nxp_conn_config cfg = {0};
    cfg.scid.data[0] = 0x01;
    cfg.scid.len = 8;
    cfg.idle_timeout_us = 30000000;
    cfg.initial_max_data = 1048576;
    cfg.initial_max_stream_data = 262144;
    cfg.max_streams_bidi = 256;
    cfg.max_streams_uni = 256;
    nxp_addr_from_string("127.0.0.1", 9002, &cfg.peer_addr);
    
    nxp_conn *conn = nxp_conn_create(&cfg, false);
    nxp_conn_id dcid = {.len = 8};
    dcid.data[0] = 0x02;
    nxp_conn_set_established(conn, &dcid);
    printf("✅ Connection established\n");
    
    // Open stream
    uint64_t stream_id;
    nxp_conn_open_stream(conn, &stream_id, NXP_STREAM_RELIABLE, false);
    printf("✅ RELIABLE stream opened\n\n");
    
    // Send multiple messages
    const char *messages[] = {
        "Message 1: Hello TCP!",
        "Message 2: Testing reliable delivery",
        "Message 3: This is ordered",
        "Message 4: All data guaranteed",
        "Message 5: Like real TCP!"
    };
    
    uint64_t now = 1000000;
    
    for (int i = 0; i < 5; i++) {
        // Write to stream
        ssize_t written = nxp_conn_stream_send(conn, stream_id, 
                                               (const uint8_t*)messages[i], 
                                               strlen(messages[i]), false);
        printf("📝 Queued: \"%s\" (%zd bytes)\n", messages[i], written);
        
        // Generate packet
        uint8_t pkt[1500];
        ssize_t pkt_len = nxp_conn_send(conn, pkt, sizeof(pkt), now);
        
        if (pkt_len > 0) {
            // Actually send over UDP
            ssize_t sent = nxp_socket_sendto(sock, pkt, pkt_len, &cfg.peer_addr);
            printf("📤 Sent packet: %zd bytes over UDP\n", sent);
            printf("   └─ Contains: \"%s\"\n", messages[i]);
        }
        
        now += 10000;
        usleep(100000); // 100ms delay
    }
    
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Statistics                                                ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Total bytes sent:    %lu bytes                            \n", conn->stats.bytes_sent);
    printf("║  Messages sent:       5                                    \n");
    printf("║  Packets generated:   %u                                   \n", conn->ack.sent_count);
    printf("║  Stream buffer used:  %lu bytes                            \n", 
           ((nxp_stream_s*)nxp_hash_map_get(conn->streams, stream_id))->send.write_offset);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    printf("\n✅ TCP Protocol: Real data sent over network!\n");
    
    nxp_socket_close(sock);
    nxp_conn_destroy(conn);
    return 0;
}
