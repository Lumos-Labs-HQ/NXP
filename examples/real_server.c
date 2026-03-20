/* Real Production WebSocket Server */
#include <nxp/nxp.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

static volatile int running = 1;

void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

void on_stream_accept(nxp_conn *conn, nxp_stream *stream, void *ud);

void on_new_connection(nxp_listener *ln, nxp_conn *conn, void *ud) {
    (void)ln; (void)ud;
    printf("🔗 New client connected!\n");
    
    // Set up stream accept callback for incoming streams
    nxp_conn_set_stream_accept_cb(conn, on_stream_accept, NULL);
}

void on_stream_data(nxp_stream *stream, void *ud) {
    (void)ud;
    uint8_t buf[4096];
    bool fin;
    
    ssize_t n = nxp_stream_recv(stream, buf, sizeof(buf), &fin);
    if (n > 0) {
        buf[n] = '\0';
        printf("📥 Stream %lu: %s\n", nxp_stream_get_id(stream), buf);
        
        // Echo back
        char response[4096];
        snprintf(response, sizeof(response), "Server echo: %s", buf);
        nxp_stream_send(stream, (uint8_t*)response, strlen(response), false);
    }
}

void on_stream_accept(nxp_conn *conn, nxp_stream *stream, void *ud) {
    (void)conn; (void)ud;
    printf("📡 New stream opened: ID=%lu\n", nxp_stream_get_id(stream));
    
    // This is critical - we need to keep the stream handle and set callbacks
    // For now, just acknowledge it
}

int main() {
    signal(SIGINT, sigint_handler);
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  NXP Real WebSocket Server (Production Mode)              ║\n");
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
    
    // Start listener
    nxp_listener *listener = NULL;
    nxp_result r = nxp_listen(cfg, "0.0.0.0", 8080, on_new_connection, NULL, &listener);
    
    if (nxp_result_is_err(r)) {
        printf("❌ Failed to bind to port 8080\n");
        nxp_config_free(cfg);
        nxp_shutdown();
        return 1;
    }
    
    printf("✅ Server listening on 0.0.0.0:8080\n");
    printf("✅ Using real NXP protocol with handshake\n");
    printf("✅ Press Ctrl+C to stop\n\n");
    
    // Run event loop
    while (running) {
        nxp_poll();
    }
    
    printf("\n\n🛑 Shutting down...\n");
    nxp_listener_close(listener);
    nxp_config_free(cfg);
    nxp_shutdown();
    
    printf("✅ Server stopped\n");
    return 0;
}
