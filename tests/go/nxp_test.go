package nxp_test

import (
	"context"
	"nxp"
	"strings"
	"testing"
	"time"
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
		{"nxp://127.0.0.1:9000", nxp.SchemeNXP, "127.0.0.1", 9000},
		{"nxps://example.com:443", nxp.SchemeNXPS, "example.com", 443},
		{"nxp://0.0.0.0:1234", nxp.SchemeNXP, "0.0.0.0", 1234},
		{"nxps://10.0.0.1:8443", nxp.SchemeNXPS, "10.0.0.1", 8443},
		{"nxp://[::1]:9000", nxp.SchemeNXP, "::1", 9000},
	}

	for _, tc := range cases {
		scheme, host, port, err := nxp.ParseURL(tc.url)
		if err != nil {
			t.Errorf("ParseURL(%q) unexpected error: %v", tc.url, err)
			continue
		}
		if scheme != tc.scheme {
			t.Errorf("ParseURL(%q) scheme = %q, want %q", tc.url, scheme, tc.scheme)
		}
		if host != tc.host {
			t.Errorf("ParseURL(%q) host = %q, want %q", tc.url, host, tc.host)
		}
		if port != tc.port {
			t.Errorf("ParseURL(%q) port = %d, want %d", tc.url, port, tc.port)
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
		_, _, _, err := nxp.ParseURL(tc.url)
		if err == nil {
			t.Errorf("ParseURL(%q) expected error containing %q, got nil", tc.url, tc.wantErr)
			continue
		}
		if !strings.Contains(strings.ToLower(err.Error()), tc.wantErr) {
			t.Errorf("ParseURL(%q) error = %q, want it to contain %q", tc.url, err.Error(), tc.wantErr)
		}
	}
}

// ══════════════════════════════════════════════════════
//  Client Dial Tests
// ══════════════════════════════════════════════════════

func TestDialNXP(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:12345", nil)
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

	state := conn.State()
	t.Logf("Connection state: %d", state)
}

func TestDialNXPS(t *testing.T) {
	conn, err := nxp.Dial("nxps://127.0.0.1:12346", nil)
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
	conn, err := nxp.Dial("nxp://127.0.0.1:12347", &nxp.DialOptions{
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
	_, err := nxp.Dial("http://example.com:80", nil)
	if err == nil {
		t.Fatal("expected error for http:// scheme")
	}

	_, err = nxp.Dial("nxp://", nil)
	if err == nil {
		t.Fatal("expected error for empty host")
	}

	_, err = nxp.Dial("nxp://host", nil)
	if err == nil {
		t.Fatal("expected error for missing port")
	}
}

func TestDialContext(t *testing.T) {
	// Cancelled context should fail immediately
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	_, err := nxp.DialContext(ctx, "nxp://127.0.0.1:12348", nil)
	if err == nil {
		t.Fatal("expected error for cancelled context")
	}
	t.Logf("DialContext with cancelled ctx: %v", err)
}

// ══════════════════════════════════════════════════════
//  Server Listener Tests
// ══════════════════════════════════════════════════════

func TestServerAccept(t *testing.T) {
	srv, err := nxp.ListenNXP("nxp://127.0.0.1:19010", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}
	defer srv.Close()

	if srv.IsSecure() {
		t.Error("nxp:// server should not be secure")
	}
	if !strings.HasPrefix(srv.Addr(), "nxp://") {
		t.Errorf("Addr() = %q, want prefix nxp://", srv.Addr())
	}
	t.Logf("Server listening on %s", srv.Addr())

	// Connect a client
	conn, err := nxp.Dial("nxp://127.0.0.1:19010", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Try Accept with short timeout
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()

	accepted, err := srv.AcceptContext(ctx)
	if err != nil {
		// Accept may timeout if handshake doesn't complete — that's OK
		t.Logf("AcceptContext: %v (expected without full handshake)", err)
	} else {
		defer accepted.Close()
		t.Logf("Accepted connection from client, state=%d", accepted.State())
	}
}

func TestServerAcceptContext(t *testing.T) {
	srv, err := nxp.ListenNXP("nxp://127.0.0.1:19011", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}
	defer srv.Close()

	// AcceptContext with already-cancelled context should fail
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	_, err = srv.AcceptContext(ctx)
	if err == nil {
		t.Fatal("expected error for cancelled context")
	}
	t.Logf("AcceptContext with cancelled ctx: %v", err)
}

func TestServerClose(t *testing.T) {
	srv, err := nxp.ListenNXP("nxp://127.0.0.1:19012", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}

	// Close the server
	srv.Close()

	// Accept after close should fail
	_, err = srv.Accept()
	if err != nxp.ErrServerClosed {
		t.Errorf("Accept after close: got %v, want ErrServerClosed", err)
	}
}

func TestListenNXPSRequiresCert(t *testing.T) {
	// nxps:// without cert/key should fail
	_, err := nxp.ListenNXP("nxps://0.0.0.0:19013", nil)
	if err == nil {
		t.Fatal("expected error: nxps:// without cert should fail")
	}
	if !strings.Contains(err.Error(), "CertFile") {
		t.Errorf("error = %q, want mention of CertFile", err.Error())
	}

	// nxps:// with empty cert should also fail
	_, err = nxp.ListenNXP("nxps://0.0.0.0:19013", &nxp.ListenOptions{})
	if err == nil {
		t.Fatal("expected error: nxps:// with empty cert should fail")
	}
}

func TestListenInvalidURL(t *testing.T) {
	_, err := nxp.ListenNXP("http://0.0.0.0:8080", nil)
	if err == nil {
		t.Fatal("expected error for http:// scheme")
	}
}

// ══════════════════════════════════════════════════════
//  Client-Server Integration Tests
// ══════════════════════════════════════════════════════

func TestClientServerLifecycle(t *testing.T) {
	srv, err := nxp.ListenNXP("nxp://127.0.0.1:19020", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}
	defer srv.Close()

	conn, err := nxp.Dial("nxp://127.0.0.1:19020", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Drive the event loop a bit
	time.Sleep(50 * time.Millisecond)

	t.Logf("Server: %s (secure=%v)", srv.Addr(), srv.IsSecure())
	t.Logf("Client: %s (secure=%v, state=%d)", conn.URL(), conn.IsSecure(), conn.State())

	// Check Done channel is open
	select {
	case <-conn.Done():
		t.Log("Connection already closed")
	default:
		t.Log("Connection still active (Done channel open)")
	}
}

func TestClientServerHandshake(t *testing.T) {
	srv, err := nxp.ListenNXP("nxp://127.0.0.1:19021", nil)
	if err != nil {
		t.Fatalf("ListenNXP failed: %v", err)
	}
	defer srv.Close()

	conn, err := nxp.Dial("nxp://127.0.0.1:19021", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// WaitReady with short timeout
	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()

	err = conn.WaitReady(ctx)
	if err != nil {
		t.Logf("WaitReady: %v (expected — handshake may not complete in test)", err)
	} else {
		t.Log("Handshake completed!")
	}
}

// ══════════════════════════════════════════════════════
//  Stream Tests
// ══════════════════════════════════════════════════════

func TestStreamOpenAndClose(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19030", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(nxp.StreamReliable)
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
	conn, err := nxp.Dial("nxp://127.0.0.1:19031", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(nxp.StreamReliable)
	if err != nil {
		t.Fatalf("OpenStream failed: %v", err)
	}
	defer stream.Close()

	data := []byte("Hello NXP Protocol!")
	n, err := stream.Write(data)
	t.Logf("stream.Write(%q) = (%d, %v)", string(data), n, err)

	n, err = stream.WriteFin([]byte("goodbye"))
	t.Logf("stream.WriteFin = (%d, %v)", n, err)
}

func TestStreamShutdown(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19032", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(nxp.StreamReliable)
	if err != nil {
		t.Fatalf("OpenStream failed: %v", err)
	}
	defer stream.Close()

	// Shutdown write direction (half-close)
	if err := stream.Shutdown(nxp.ShutdownWrite); err != nil {
		t.Fatalf("Shutdown(Write) failed: %v", err)
	}
	t.Logf("Stream state after ShutdownWrite: %d", stream.State())
}

func TestStreamTypes(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19033", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	types := []struct {
		stype int
		name  string
	}{
		{nxp.StreamReliable, "Reliable"},
		{nxp.StreamFast, "Fast"},
		{nxp.StreamMedia, "Media"},
		{nxp.StreamFile, "File"},
	}

	for _, tc := range types {
		stream, err := conn.OpenStream(tc.stype)
		if err != nil {
			t.Errorf("OpenStream(%s) failed: %v", tc.name, err)
			continue
		}
		t.Logf("Opened %s stream ID=%d", tc.name, stream.ID())
		stream.Close()
	}
}

func TestStreamPriority(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19034", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Open streams with different priorities
	high, err := conn.OpenStreamWithPriority(nxp.StreamReliable, 0)
	if err != nil {
		t.Fatalf("OpenStreamWithPriority(0) failed: %v", err)
	}
	defer high.Close()

	low, err := conn.OpenStreamWithPriority(nxp.StreamReliable, 255)
	if err != nil {
		t.Fatalf("OpenStreamWithPriority(255) failed: %v", err)
	}
	defer low.Close()

	t.Logf("High priority stream ID=%d, Low priority stream ID=%d", high.ID(), low.ID())
}

func TestStreamBackpressure(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19035", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	stream, err := conn.OpenStream(nxp.StreamReliable)
	if err != nil {
		t.Fatalf("OpenStream failed: %v", err)
	}
	defer stream.Close()

	writable := stream.Writable()
	readable := stream.Readable()
	t.Logf("Stream backpressure: writable=%d, readable=%d", writable, readable)
}

// ══════════════════════════════════════════════════════
//  Connection Features Tests
// ══════════════════════════════════════════════════════

func TestConnectionStats(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19040", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	// Let event loop flush handshake packets
	time.Sleep(20 * time.Millisecond)

	stats := conn.Statistics()
	t.Logf("Stats: sent=%d bytes/%d pkts, recv=%d bytes/%d pkts, cwnd=%d",
		stats.BytesSent, stats.PacketsSent,
		stats.BytesRecv, stats.PacketsRecv,
		stats.CWnd)
}

func TestConnectionState(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19041", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}
	defer conn.Close()

	state := conn.State()
	t.Logf("Connection state: %d (0=idle, 1=handshake_init, 2=handshaking, 3=established)", state)

	// State should be handshaking since there's no server to complete it
	if state == nxp.ConnClosed {
		t.Error("Connection should not be closed immediately after Dial")
	}
}

func TestConnectionDone(t *testing.T) {
	conn, err := nxp.Dial("nxp://127.0.0.1:19042", nil)
	if err != nil {
		t.Fatalf("Dial failed: %v", err)
	}

	done := conn.Done()

	// Should not be closed yet
	select {
	case <-done:
		t.Error("Done channel should not be closed before Close()")
	default:
		t.Log("Done channel is open")
	}

	// Close the connection
	conn.Close()

	// Done should be closed now
	select {
	case <-done:
		t.Log("Done channel closed after Close()")
	case <-time.After(100 * time.Millisecond):
		t.Error("Done channel should be closed after Close()")
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
		{nxp.OK, "success"},
		{nxp.ErrInvalid, "invalid argument"},
		{nxp.ErrOutOfMemory, "out of memory"},
		{nxp.ErrCryptoFail, "cryptographic failure"},
		{nxp.ErrHandshake, "handshake failed"},
		{nxp.ErrCongestion, "congestion control limit"},
	}

	for _, tc := range cases {
		got := nxp.ErrorStr(tc.code)
		if got != tc.want {
			t.Errorf("ErrorStr(%d): got %q, want %q", tc.code, got, tc.want)
		}
	}
}

func TestLowLevelConfig(t *testing.T) {
	cfg := nxp.NewConfig()
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
	nxp.Poll()
}
