package main

// test_tcp.go — NXP file stream: bulk request/response pattern.
//
// Pattern: client sends a 4 KB request, server transforms it (uppercase)
// and sends back the response. Tests large payload delivery via StreamFile.

import (
	"context"
	"fmt"
	"log"
	"nxp"
	"strings"
	"time"
)

func testTCP() bool {
	const (
		addr    = "nxp://127.0.0.1:9410"
		timeout = 8 * time.Second
	)

	srv, err := nxp.ListenNXP(addr, nil)
	if err != nil {
		log.Printf("[bulk] listen: %v", err)
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

		req := recvAll(stream, timeout)
		stream.WriteFin([]byte(strings.ToUpper(req))) // transform + reply
		srvResult <- req
	}()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	conn, err := nxp.Dial(addr, nil)
	if err != nil {
		log.Printf("[bulk][cli] dial: %v", err)
		return false
	}
	defer conn.Close()

	if err := conn.WaitReady(ctx); err != nil {
		log.Printf("[bulk][cli] ready: %v", err)
		return false
	}

	stream, err := conn.OpenStream(nxp.StreamFile)
	if err != nil {
		log.Printf("[bulk][cli] open: %v", err)
		return false
	}
	defer stream.Close()

	request := fmt.Sprintf("GET /data HTTP/1.1\r\nHost: nxp\r\n\r\n%s",
		strings.Repeat("payload:", 500)) // ~4 KB

	stream.WriteFin([]byte(request))

	if got := <-srvResult; got != request {
		log.Printf("[bulk] server recv mismatch (%d bytes)", len(got))
		return false
	}

	response := recvAll(stream, timeout)
	if response != strings.ToUpper(request) {
		log.Printf("[bulk] response mismatch (%d bytes)", len(response))
		return false
	}

	log.Printf("[bulk] req=%dB resp=%dB", len(request), len(response))
	return true
}
