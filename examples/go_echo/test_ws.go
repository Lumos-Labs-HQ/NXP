package main

// test_ws.go — NXP fast stream: real-time bidirectional chat pattern.
//
// Pattern: client sends multiple chat messages, server echoes each one
// prefixed with "srv:". Tests StreamFast (low-latency, unordered).

import (
	"context"
	"log"
	"nxp"
	"strings"
	"time"
)

func testWS() bool {
	const (
		addr    = "nxp://127.0.0.1:9420"
		timeout = 8 * time.Second
	)

	srv, err := nxp.ListenNXP(addr, nil)
	if err != nil {
		log.Printf("[chat] listen: %v", err)
		return false
	}
	defer srv.Close()

	srvResult := make(chan string, 1)
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()

		conn, err := srv.AcceptContext(ctx)
		if err != nil {
			srvResult <- ""
			return
		}
		defer conn.Close()
		conn.WaitReady(ctx)

		stream, err := conn.AcceptStream(ctx)
		if err != nil {
			srvResult <- ""
			return
		}
		defer stream.Close()

		got := recvAll(stream, timeout)
		// Echo each line prefixed with "srv:"
		var reply strings.Builder
		for _, line := range strings.Split(got, "\n") {
			if line != "" {
				reply.WriteString("srv:" + line + "\n")
			}
		}
		stream.WriteFin([]byte(strings.TrimRight(reply.String(), "\n")))
		srvResult <- got
	}()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	conn, err := nxp.Dial(addr, nil)
	if err != nil {
		log.Printf("[chat][cli] dial: %v", err)
		return false
	}
	defer conn.Close()

	if err := conn.WaitReady(ctx); err != nil {
		log.Printf("[chat][cli] ready: %v", err)
		return false
	}

	stream, err := conn.OpenStream(nxp.StreamFast)
	if err != nil {
		log.Printf("[chat][cli] open: %v", err)
		return false
	}
	defer stream.Close()

	messages := []string{"ping", "hello server", "status?"}
	payload := strings.Join(messages, "\n")
	stream.WriteFin([]byte(payload))

	if got := <-srvResult; got != payload {
		log.Printf("[chat] server recv mismatch: %q", got)
		return false
	}

	reply := recvAll(stream, timeout)
	for _, msg := range messages {
		if !strings.Contains(reply, "srv:"+msg) {
			log.Printf("[chat] missing echo for %q in reply", msg)
			return false
		}
	}

	log.Printf("[chat] %d messages echoed", len(messages))
	return true
}
