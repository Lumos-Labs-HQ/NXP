package main

// test_nxp.go — NXP reliable stream echo over UDP.
//
// Pattern: client sends a payload + FIN, server reads it, echoes it back.
// Verifies reliable ordered delivery end-to-end.

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"nxp"
	"strings"
	"time"
)

func testNXP() bool {
	const (
		addr    = "nxp://127.0.0.1:9400"
		timeout = 8 * time.Second
	)

	srv, err := nxp.ListenNXP(addr, nil)
	if err != nil {
		log.Printf("[nxp] listen: %v", err)
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
		stream.WriteFin([]byte(got)) // echo
		srvResult <- got
	}()

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	conn, err := nxp.Dial(addr, nil)
	if err != nil {
		log.Printf("[nxp][cli] dial: %v", err)
		return false
	}
	defer conn.Close()

	if err := conn.WaitReady(ctx); err != nil {
		log.Printf("[nxp][cli] ready: %v", err)
		return false
	}

	stream, err := conn.OpenStream(nxp.StreamReliable)
	if err != nil {
		log.Printf("[nxp][cli] open: %v", err)
		return false
	}
	defer stream.Close()

	payload := fmt.Sprintf("hello|world|nxp:%s", strings.Repeat("x", 64))
	stream.WriteFin([]byte(payload))

	if got := <-srvResult; got != payload {
		log.Printf("[nxp] server recv mismatch (%d bytes)", len(got))
		return false
	}

	echo := recvAll(stream, timeout)
	if echo != payload {
		log.Printf("[nxp] echo mismatch (%d bytes)", len(echo))
		return false
	}

	s := conn.Statistics()
	log.Printf("[nxp] sent=%dB rtt=%dµs", s.BytesSent, s.RTTSmoothedUs)
	return true
}

// recvAll reads from stream until EOF or timeout.
func recvAll(stream *nxp.NXPStream, timeout time.Duration) string {
	var sb strings.Builder
	buf := make([]byte, 4096)
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		n, err := stream.Read(buf)
		if n > 0 {
			sb.Write(buf[:n])
		}
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			var e *nxp.Error
			if errors.As(err, &e) && e.Code == nxp.ErrWouldBlock {
				time.Sleep(2 * time.Millisecond)
				continue
			}
			break
		}
	}
	return sb.String()
}
