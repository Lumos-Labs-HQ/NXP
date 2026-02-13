// user2 — NXP Client
//
// Run:  go run ./user2
// (Make sure user1/server is already running)
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
	log.Println("[CLIENT] ========================================")
	log.Println("[CLIENT] Connecting to NXP server...")
	log.Println("[CLIENT] ========================================")

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	conn, err := nxp.DialContext(ctx, "nxp://127.0.0.1:9000", nil)
	if err != nil {
		log.Fatalf("[CLIENT] Dial failed: %v", err)
	}
	defer conn.Close()
	defer nxp.GracefulShutdown()
	log.Printf("[CLIENT] Connected to %s", conn.URL())

	// ── Wait for handshake ─────────────────────────────
	if err := conn.WaitReady(ctx); err != nil {
		log.Fatalf("[CLIENT] Handshake failed: %v", err)
	}
	log.Println("[CLIENT] Handshake complete!")

	// ── Open a stream ──────────────────────────────────
	stream, err := conn.OpenStream(nxp.StreamReliable)
	if err != nil {
		log.Fatalf("[CLIENT] OpenStream failed: %v", err)
	}
	defer stream.Close()
	log.Printf("[CLIENT] Opened stream %d (type: reliable)", stream.ID())

	// ── Send message to server ─────────────────────────
	msg := "Hello from CLIENT! This is User2 speaking over NXP."
	n, err := stream.Write([]byte(msg))
	if err != nil {
		log.Fatalf("[CLIENT] Write failed: %v", err)
	}
	log.Printf("[CLIENT] -> Sent (%d bytes): %q", n, msg)

	// Signal we're done sending (half-close write side)
	stream.Shutdown(nxp.ShutdownWrite)
	log.Println("[CLIENT] -> Write side shut down (FIN sent)")

	// ── Read response from server ──────────────────────
	log.Println("[CLIENT] Waiting for server response...")
	buf := make([]byte, 4096)
	var total int
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		nr, readErr := stream.Read(buf[total:])
		if nr > 0 {
			total += nr
			log.Printf("[CLIENT] <- Read %d bytes (total: %d)", nr, total)
		}
		if readErr != nil {
			if errors.Is(readErr, io.EOF) {
				log.Println("[CLIENT] <- Stream FIN received from server")
				break
			}
			var nxpErr *nxp.Error
			if errors.As(readErr, &nxpErr) && nxpErr.Code == nxp.ErrWouldBlock {
				time.Sleep(5 * time.Millisecond)
				continue
			}
			log.Printf("[CLIENT] Read error: %v", readErr)
			break
		}
	}

	if total > 0 {
		log.Printf("[CLIENT] Server response: %q", string(buf[:total]))
	} else {
		log.Println("[CLIENT] No response from server")
	}

	// ── Let data flush ─────────────────────────────────
	time.Sleep(500 * time.Millisecond)

	// ── Print statistics ───────────────────────────────
	stats := conn.Statistics()
	log.Println("[CLIENT] ========================================")
	log.Printf("[CLIENT] Stats:")
	log.Printf("[CLIENT]   Bytes sent:     %d", stats.BytesSent)
	log.Printf("[CLIENT]   Bytes received: %d", stats.BytesRecv)
	log.Printf("[CLIENT]   Packets sent:   %d", stats.PacketsSent)
	log.Printf("[CLIENT]   Packets recv:   %d", stats.PacketsRecv)
	log.Printf("[CLIENT]   Packets lost:   %d", stats.PacketsLost)
	log.Printf("[CLIENT]   RTT (smoothed): %d µs", stats.RTTSmoothedUs)
	log.Println("[CLIENT] ========================================")
	log.Println("[CLIENT] Done!")
}
