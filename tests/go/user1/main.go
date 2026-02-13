// user1 — NXP Server
//
// Run:  go run ./user1
// Then in another terminal: go run ./user2
package main

import (
	"context"
	"errors"
	"io"
	"log"
	"nxp"
	"time"
)

func main() {
	log.SetFlags(log.Ltime | log.Lmicroseconds)
	log.Println("[SERVER] ========================================")
	log.Println("[SERVER] Starting NXP server...")
	log.Println("[SERVER] ========================================")

	srv, err := nxp.ListenNXP("nxp://127.0.0.1:9000", nil)
	if err != nil {
		log.Fatalf("[SERVER] Listen failed: %v", err)
	}
	defer srv.Close()
	defer nxp.GracefulShutdown()
	log.Printf("[SERVER] Listening on %s", srv.Addr())

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	// ── Accept connection ──────────────────────────────
	log.Println("[SERVER] Waiting for client connection...")
	conn, err := srv.AcceptContext(ctx)
	if err != nil {
		log.Fatalf("[SERVER] Accept failed: %v", err)
	}
	defer conn.Close()
	log.Printf("[SERVER] Client connected! URL: %s", conn.URL())

	// ── Wait for handshake ─────────────────────────────
	if err := conn.WaitReady(ctx); err != nil {
		log.Fatalf("[SERVER] Handshake failed: %v", err)
	}
	log.Println("[SERVER] Handshake complete!")

	// ── Accept stream from client ──────────────────────
	log.Println("[SERVER] Waiting for incoming stream...")
	stream, err := conn.AcceptStream(ctx)
	if err != nil {
		log.Fatalf("[SERVER] AcceptStream failed: %v", err)
	}
	defer stream.Close()
	log.Printf("[SERVER] Stream %d accepted", stream.ID())

	// ── Read data from client ──────────────────────────
	buf := make([]byte, 4096)
	var total int
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		n, readErr := stream.Read(buf[total:])
		if n > 0 {
			total += n
			log.Printf("[SERVER] <- Read %d bytes (total: %d)", n, total)
		}
		if readErr != nil {
			if errors.Is(readErr, io.EOF) {
				log.Println("[SERVER] <- Stream FIN received")
				break
			}
			var nxpErr *nxp.Error
			if errors.As(readErr, &nxpErr) && nxpErr.Code == nxp.ErrWouldBlock {
				time.Sleep(5 * time.Millisecond)
				continue
			}
			log.Printf("[SERVER] Read error: %v", readErr)
			break
		}
	}

	if total > 0 {
		log.Printf("[SERVER] Received message: %q", string(buf[:total]))
	} else {
		log.Println("[SERVER] No data received from client")
	}

	// ── Send response back ─────────────────────────────
	reply := "Hello from SERVER! Got your message. NXP protocol works!"
	n, err := stream.WriteFin([]byte(reply))
	if err != nil {
		log.Fatalf("[SERVER] Write failed: %v", err)
	}
	log.Printf("[SERVER] -> Sent response (%d bytes): %q", n, reply)

	// ── Let data flush ─────────────────────────────────
	time.Sleep(time.Second)

	// ── Print statistics ───────────────────────────────
	stats := conn.Statistics()
	log.Println("[SERVER] ========================================")
	log.Printf("[SERVER] Stats:")
	log.Printf("[SERVER]   Bytes sent:     %d", stats.BytesSent)
	log.Printf("[SERVER]   Bytes received: %d", stats.BytesRecv)
	log.Printf("[SERVER]   Packets sent:   %d", stats.PacketsSent)
	log.Printf("[SERVER]   Packets recv:   %d", stats.PacketsRecv)
	log.Printf("[SERVER]   Packets lost:   %d", stats.PacketsLost)
	log.Printf("[SERVER]   RTT (smoothed): %d µs", stats.RTTSmoothedUs)
	log.Println("[SERVER] ========================================")
	log.Println("[SERVER] Done!")
}
