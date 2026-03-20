/* Simple Working Server - No Crypto for Testing */
#include "../../src/core/connection_internal.h"
#include "../../src/platform/platform.h"
#include <stdio.h>
#include <signal.h>

static int running = 1;
void sigint_handler(int sig) { (void)sig; running = 0; }

int main() {
    signal(SIGINT, sigint_handler);
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Simple Test Server (No Crypto)                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    nxp_socket_init();
    
    nxp_addr bind_addr;
    nxp_addr_from_string("0.0.0.0", 8080, &bind_addr);
    nxp_socket *sock = NULL;
    
    if (nxp_result_is_err(nxp_socket_create_udp(&bind_addr, &sock))) {
        printf("❌ Failed to bind\n");
        return 1;
    }
    nxp_socket_set_nonblocking(sock);
    
    printf("✅ Listening on 0.0.0.0:8080\n\n");
    
    nxp_conn_config cfg = {0};
    cfg.scid.len = 8;
    cfg.scid.data[0] = 0xAA;
    cfg.idle_timeout_us = 60000000;
    cfg.initial_max_data = 1048576;
    cfg.initial_max_stream_data = 262144;
    cfg.max_streams_bidi = 256;
    
    nxp_conn *conn = nxp_conn_create(&cfg, true);
    nxp_conn_id dcid = {.len = 8, .data = {0xBB}};
    nxp_conn_set_established(conn, &dcid);
    
    uint64_t stream_id;
    nxp_conn_open_stream(conn, &stream_id, NXP_STREAM_RELIABLE, false);
    printf("📡 Stream ready: ID=%lu\n\n", stream_id);
    
    nxp_addr client_addr = {0};
    uint64_t now = 1000000;
    int msg_count = 0;
    
    while (running) {
        uint8_t buf[1500];
        ssize_t n = nxp_socket_recvfrom(sock, buf, sizeof(buf), &client_addr);
        
        if (n > 0) {
            printf("📥 Received %zd bytes\n", n);
            nxp_conn_recv(conn, buf, n, now);
            
            uint8_t data[1024];
            bool fin;
            ssize_t read = nxp_conn_stream_recv(conn, stream_id, data, sizeof(data), &fin);
            if (read > 0) {
                data[read] = '\0';
                printf("💬 Message: %s\n", data);
                
                char reply[128];
                snprintf(reply, sizeof(reply), "Echo: %s", data);
                nxp_conn_stream_send(conn, stream_id, (uint8_t*)reply, strlen(reply), false);
                msg_count++;
            }
        }
        
        uint8_t out[1500];
        ssize_t len = nxp_conn_send(conn, out, sizeof(out), now);
        if (len > 0) {
            nxp_socket_sendto(sock, out, len, &client_addr);
            printf("📤 Sent %zd bytes\n", len);
        }
        
        now += 10000;
        usleep(10000);
    }
    
    printf("\n✅ Messages received: %d\n", msg_count);
    nxp_socket_close(sock);
    nxp_conn_destroy(conn);
    return 0;
}
