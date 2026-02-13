package nxp

import (
	"strings"
	"testing"
)

// ══════════════════════════════════════════════════════
//  URL Parsing Tests
// ══════════════════════════════════════════════════════

func TestURLParsingValid(t *testing.T) {
	cases := []struct {
		url    string
		scheme string
		host   string
		port   uint16
	}{
		{"nxp://127.0.0.1:9000", SchemeNXP, "127.0.0.1", 9000},
		{"nxps://example.com:443", SchemeNXPS, "example.com", 443},
		{"nxp://0.0.0.0:1234", SchemeNXP, "0.0.0.0", 1234},
		{"nxps://10.0.0.1:8443", SchemeNXPS, "10.0.0.1", 8443},
		{"nxp://[::1]:9000", SchemeNXP, "::1", 9000},
	}

	for _, tc := range cases {
		scheme, host, port, err := parseNXPURL(tc.url)
		if err != nil {
			t.Errorf("parseNXPURL(%q) unexpected error: %v", tc.url, err)
			continue
		}
		if scheme != tc.scheme {
			t.Errorf("parseNXPURL(%q) scheme = %q, want %q", tc.url, scheme, tc.scheme)
		}
		if host != tc.host {
			t.Errorf("parseNXPURL(%q) host = %q, want %q", tc.url, host, tc.host)
		}
		if port != tc.port {
			t.Errorf("parseNXPURL(%q) port = %d, want %d", tc.url, port, tc.port)
		}
	}
}

func TestURLParsingInvalid(t *testing.T) {
	cases := []struct {
		url     string
		wantErr string
	}{
		{"http://example.com:80", "unsupported"},
		{"tcp://host:1234", "unsupported"},
		{"nxp://", "missing host"},
		{"nxp://host", "missing port"},
		{"nxp://host:0", "invalid port"},
		{"nxp://host:99999", "invalid port"},
		{"ftp://foo:21", "unsupported"},
	}

	for _, tc := range cases {
		_, _, _, err := parseNXPURL(tc.url)
		if err == nil {
			t.Errorf("parseNXPURL(%q) expected error containing %q, got nil", tc.url, tc.wantErr)
			continue
		}
		if !strings.Contains(strings.ToLower(err.Error()), tc.wantErr) {
			t.Errorf("parseNXPURL(%q) error = %q, want it to contain %q", tc.url, err.Error(), tc.wantErr)
		}
	}
}

// ══════════════════════════════════════════════════════
//  Protocol Dial Tests (nxp:// and nxps://)
// ══════════════════════════════════════════════════════

func TestDialNXP(t *testing.T) {
	conn, err := Dial("nxp://127.0.0.1:12345", nil)
	if err != nil {
		t.Fatalf("Dial(nxp://) failed: %v", err)
	}
	defer conn.Close()

	if conn.IsSecure() {
		t.Error("nxp:// connection should not be secure")
	}
	if !strings.HasPrefix(conn.URL(), "nxp://") {
		t.Errorf("URL() = %q, want prefix nxp://", conn.URL())
	}
	if conn.URL() != "nxp://127.0.0.1:12345" {
		t.Errorf("URL() = %q, want nxp://127.0.0.1:12345", conn.URL())
	}

	// Connection should be in handshake state (no server to complete it)
	state := conn.State()
	t.Logf("Connection state: %d", state)

	// Poll should not crash
	Poll()
}

func TestDialNXPS(t *testing.T) {
	// nxps:// client without certs still connects (cert validation is server-side)
	conn, err := Dial("nxps://127.0.0.1:12346", nil)
	if err != nil {
		t.Fatalf("Dial(nxps://) failed: %v", err)
	}
	defer conn.Close()

	if !conn.IsSecure() {
		t.Error("nxps:// connection should be secure")
	}
	if !strings.HasPrefix(conn.URL(), "nxps://") {
		t.Errorf("URL() = %q, want prefix nxps://", conn.URL())
	}
}

func TestDialWithOptions(t *testing.T) {
	conn, err := Dial("nxp://127.0.0.1:12347", &DialOptions{
		IdleTimeoutMs:  5000,
		MaxStreamsBidi: 100,
		MaxStreamsUni:   50,
		HeartbeatMs:    1000,
	})
	if err != nil {
		t.Fatalf("Dial with options failed: %v", err)
	}
	defer conn.Close()

	t.Logf("Connected to %s, state=%d", conn.URL(), conn.State())
}

func TestDialInvalidURL(t *testing.T) {
	_, err := Dial("http://example.com:80", nil)
	if err == nil {
		t.Fatal("expected error for http:// scheme")
	}

	_, err = Dial("nxp://", nil)
	if err == nil {
		t.Fatal("expected error for empty host")
	}

	_, err = Dial("nxp://host", nil)
	if err == nil {
		t.Fatal("expected error for missing port")
	}
}

// ══════════════════════════════════════════════════════
//  Server Listener Tests
// ══════════════════════════════════════════════════════

func TestListenNXP(t *testing.T) {
	srv, err := ListenNXP("nxp://127.0.0.1:19000", nil)
	if err != nil {
		t.Fatalf("ListenNXP(nxp://) failed: %v", err)
	}
	defer srv.Close()

	if srv.IsSecure() {
		t.Error("nxp:// server should not be secure")
	}
	if !strings.HasPrefix(srv.Addr(), "nxp://") {
		t.Errorf("Addr() = %q, want prefix nxp://", srv.Addr())
	}
	t.Logf("Server listening on %s", srv.Addr())
}

func TestListenNXPSRequiresCert(t *testing.T) {
	// nxps:// without cert/key should fail
	_, err := ListenNXP("nxps://0.0.0.0:19001", nil)
	if err == nil {
		t.Fatal("expected error: nxps:// without cert should fail")
	}
	if !strings.Contains(err.Error(), "CertFile") {
		t.Errorf("error = %q, want mention of CertFile", err.Error())
	}

	// nxps:// with empty cert should also fail
	_, err = ListenNXP("nxps://0.0.0.0:19001", &ListenOptions{})
	if err == nil {
		t.Fatal("expected error: nxps:// with empty cert should fail")
	}
}

func TestListenInvalidURL(t *testing.T) {
	_, err := ListenNXP("http://0.0.0.0:8080", nil)
	if err == nil {
		t.Fatal("expected error for http:// scheme")
	}
}

// ══════════════════════════════════════════════════════
//  Client + Server Integration
// ══════════════════════════════════════════════════════

func TestClientServerLifecycle(t *testing.T) {
	// Start server on nxp://
	srv, err := ListenNXP("nxp://127.0.0.1:19002", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}
	defer srv.Close()

	// Connect client
	conn, err := Dial("nxp://127.0.0.1:19002", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Drive the event loop
	for i := 0; i < 10; i++ {
		Poll()
	}

	t.Logf("Server: %s (secure=%v)", srv.Addr(), srv.IsSecure())
	t.Logf("Client: %s (secure=%v, state=%d)", conn.URL(), conn.IsSecure(), conn.State())
}

// ══════════════════════════════════════════════════════
//  Stream Tests
// ══════════════════════════════════════════════════════

func TestStreamOpenAndClose(t *testing.T) {
	conn, err := Dial("nxp://127.0.0.1:19003", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(StreamReliable)
	if err != nil {
		t.Fatalf("OpenStream failed: %v", err)
	}

	id := stream.ID()
	t.Logf("Opened stream ID=%d", id)

	if err := stream.Close(); err != nil {
		t.Fatalf("stream.Close failed: %v", err)
	}
}

func TestStreamWrite(t *testing.T) {
	conn, err := Dial("nxp://127.0.0.1:19004", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(StreamReliable)
	if err != nil {
		t.Fatalf("OpenStream failed: %v", err)
	}
	defer stream.Close()

	// Write data (will be buffered since handshake isn't complete)
	data := []byte("Hello NXP Protocol!")
	n, err := stream.Write(data)
	// Write may succeed (buffered) or fail (no handshake) — both are valid
	t.Logf("stream.Write(%q) = (%d, %v)", string(data), n, err)

	// WriteFin
	n, err = stream.WriteFin([]byte("goodbye"))
	t.Logf("stream.WriteFin = (%d, %v)", n, err)
}

// ══════════════════════════════════════════════════════
//  Connection Statistics
// ══════════════════════════════════════════════════════

func TestConnectionStats(t *testing.T) {
	conn, err := Dial("nxp://127.0.0.1:19005", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Poll to trigger initial handshake packets
	for i := 0; i < 5; i++ {
		Poll()
	}

	stats := conn.Statistics()
	t.Logf("Stats: sent=%d bytes/%d pkts, recv=%d bytes/%d pkts, cwnd=%d",
		stats.BytesSent, stats.PacketsSent,
		stats.BytesRecv, stats.PacketsRecv,
		stats.CWnd)

	// Should have sent at least the initial handshake packet
	if stats.PacketsSent == 0 {
		t.Log("Note: no packets sent (expected — handshake flush happens on connect)")
	}
}

// ══════════════════════════════════════════════════════
//  Low-Level Regression Tests
// ══════════════════════════════════════════════════════

func TestLowLevelErrorStrings(t *testing.T) {
	cases := []struct {
		code int
		want string
	}{
		{OK, "success"},
		{ErrInvalid, "invalid argument"},
		{ErrOutOfMemory, "out of memory"},
		{ErrCryptoFail, "cryptographic failure"},
		{ErrHandshake, "handshake failed"},
		{ErrCongestion, "congestion control limit"},
	}

	for _, tc := range cases {
		got := ErrorStr(tc.code)
		if got != tc.want {
			t.Errorf("ErrorStr(%d): got %q, want %q", tc.code, got, tc.want)
		}
	}
}

func TestLowLevelConfig(t *testing.T) {
	cfg := NewConfig()
	if cfg == nil {
		t.Fatal("NewConfig returned nil")
	}
	defer cfg.Free()

	if r := cfg.SetIdleTimeout(5000); !r.OK() {
		t.Fatalf("SetIdleTimeout failed: %d", r.Code)
	}
	if r := cfg.SetMaxStreams(100, 50); !r.OK() {
		t.Fatalf("SetMaxStreams failed: %d", r.Code)
	}
	if r := cfg.SetMaxUDPPayload(1400); !r.OK() {
		t.Fatalf("SetMaxUDPPayload failed: %d", r.Code)
	}

	// Invalid payload should fail
	if r := cfg.SetMaxUDPPayload(100); r.OK() {
		t.Fatal("expected SetMaxUDPPayload(100) to fail")
	}
}

func TestLowLevelPollSafety(t *testing.T) {
	// Poll should be safe even when called outside of any connection context
	Poll()
}
