// go_echo — NXP end-to-end transport tests
//
// Three independent tests, one per real-world usage pattern:
//
//	test_nxp.go  — reliable stream echo over NXP/UDP
//	test_tcp.go  — bulk request/response (file-transfer style)
//	test_ws.go   — bidirectional chat (real-time style)
//
// Usage:
//
//	go run .
package main

import (
	"fmt"
	"log"
	"nxp"
)

func main() {
	log.SetFlags(log.Ltime | log.Lmicroseconds)
	fmt.Println("=== NXP Transport Tests ===")
	defer nxp.GracefulShutdown()

	tests := []struct {
		name string
		fn   func() bool
	}{
		{"NXP reliable echo", testNXP},
		{"NXP bulk request/response", testTCP},
		{"NXP real-time chat", testWS},
	}

	fails := 0
	for _, t := range tests {
		if t.fn() {
			fmt.Printf("  %-28s PASS\n", t.name)
		} else {
			fmt.Printf("  %-28s FAIL\n", t.name)
			fails++
		}
	}

	fmt.Println()
	if fails == 0 {
		fmt.Println("PASS")
	} else {
		fmt.Printf("FAIL (%d/%d)\n", fails, len(tests))
	}
}
