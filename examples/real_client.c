/* Real Production WebSocket Client */
#include <nxp/nxp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static nxp_conn *g_conn = NULL;
static nxp_stream *g_chat_stream = NULL;
static int connected = 0;
static int msg_count = 0;

void on_connected(nxp_conn *conn, void *ud) {
    (void)ud;
    printf("✅ Connected to server!\n");
    connected = 1;
    
    // Open chat stream
    nxp_result r = nxp_stream_open(conn, NXP_STREAM_RELIABLE, 128,
                                   NULL, NULL, NULL, NULL, &g_chat_stream);
    if (nxp_result_is_ok(r)) {
        printf("📡 Chat stream opened (ID: %lu)\n", nxp_stream_get_id(g_chat_stream));
    }
}

void on_closed(nxp_conn *conn, void *ud) {
    (void)conn; (void)ud;
    printf("🔌 Connection closed\n");
    connected = 0;
}

void on_stream_data(nxp_stream *stream, void *ud) {
    (void)ud;
    uint8_t buf[4096];
    bool fin;
    
    ssize_t n = nxp_stream_recv(stream, buf, sizeof(buf), &fin);
    if (n > 0) {
        buf[n] = '\0';
        printf("📥 Server reply: %s\n", buf);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  NXP Real WebSocket Client (Production Mode)              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Initialize NXP
    nxp_global_config gcfg = {
        .worker_threads = 0,
        .packet_pool_size = 4096
    };
    
    if (nxp_result_is_err(nxp_init(&gcfg))) {
        printf("❌ Failed to initialize NXP\n");
        return 1;
    }
    
    // Create config
    nxp_config *cfg = nxp_config_new();
    (void)nxp_config_set_idle_timeout(cfg, 60000);
    (void)nxp_config_set_max_streams(cfg, 256, 256);
    
    printf("🔗 Connecting to %s:8080...\n", argv[1]);
    
    // Connect
    nxp_result r = nxp_connect(cfg, argv[1], 8080, 
                               on_connected, on_closed, NULL, &g_conn);
    
    if (nxp_result_is_err(r)) {
        printf("❌ Failed to connect\n");
        nxp_config_free(cfg);
        nxp_shutdown();
        return 1;
    }
    
    printf("⏳ Waiting for connection...\n");
    
    // Wait for connection
    for (int i = 0; i < 50 && !connected; i++) {
        nxp_poll();
        usleep(100000); // 100ms
    }
    
    if (!connected) {
        printf("❌ Connection timeout\n");
        nxp_conn_close(g_conn, 0);
        nxp_config_free(cfg);
        nxp_shutdown();
        return 1;
    }
    
    // Send messages
    const char *messages[] = {
        "Hello server!",
        "How are you?",
        "Testing real protocol",
        "With actual handshake",
        "This is production ready!"
    };
    
    for (int i = 0; i < 5; i++) {
        printf("\n[Message %d]\n", i+1);
        printf("💬 Sending: %s\n", messages[i]);
        
        ssize_t sent = nxp_stream_send(g_chat_stream, 
                                       (uint8_t*)messages[i], 
                                       strlen(messages[i]), false);
        if (sent > 0) {
            printf("📤 Sent %zd bytes\n", sent);
        }
        
        // Process responses
        for (int j = 0; j < 10; j++) {
            nxp_poll();
            usleep(100000); // 100ms
        }
        
        msg_count++;
    }
    
    printf("\n\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Client Statistics                                         ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    
    nxp_conn_stats stats = nxp_conn_get_stats(g_conn);
    printf("║  Messages sent:      %d                                    \n", msg_count);
    printf("║  Bytes sent:         %lu                                   \n", stats.bytes_sent);
    printf("║  Bytes received:     %lu                                   \n", stats.bytes_recv);
    printf("║  Packets sent:       %lu                                   \n", stats.packets_sent);
    printf("║  Packets received:   %lu                                   \n", stats.packets_recv);
    printf("║  RTT (smoothed):     %lu μs                                \n", stats.rtt_smoothed_us);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    printf("\n✅ Test complete!\n");
    
    // Cleanup
    nxp_stream_close(g_chat_stream);
    nxp_conn_close(g_conn, 0);
    nxp_config_free(cfg);
    nxp_shutdown();
    
    return 0;
}
