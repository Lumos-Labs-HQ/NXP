package nxp

import "testing"

// TestInitShutdown verifies the library lifecycle round-trip.
func TestInitShutdown(t *testing.T) {
	r := Init()
	if !r.OK() {
		t.Fatalf("Init failed: code=%d", r.Code)
	}
	Shutdown()

	// Second init should succeed after shutdown
	r = Init()
	if !r.OK() {
		t.Fatalf("Init (second) failed: code=%d", r.Code)
	}
	Shutdown()
}

// TestDoubleInitFails verifies that calling init twice fails.
func TestDoubleInitFails(t *testing.T) {
	r := Init()
	if !r.OK() {
		t.Fatalf("Init failed: %d", r.Code)
	}
	defer Shutdown()

	r = Init()
	if r.OK() {
		t.Fatal("expected second Init to fail")
	}
}

// TestErrorStrings verifies error string mapping.
func TestErrorStrings(t *testing.T) {
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

// TestConfigLifecycle verifies config creation, modification, and freeing.
func TestConfigLifecycle(t *testing.T) {
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
	if r := cfg.SetHeartbeatInterval(1000); !r.OK() {
		t.Fatalf("SetHeartbeatInterval failed: %d", r.Code)
	}
	if r := cfg.SetMaxUDPPayload(1400); !r.OK() {
		t.Fatalf("SetMaxUDPPayload failed: %d", r.Code)
	}

	// Invalid payload should fail
	if r := cfg.SetMaxUDPPayload(100); r.OK() {
		t.Fatal("expected SetMaxUDPPayload(100) to fail")
	}

	if r := cfg.SetCertFile("/path/to/cert.pem"); !r.OK() {
		t.Fatalf("SetCertFile failed: %d", r.Code)
	}
	if r := cfg.SetKeyFile("/path/to/key.pem"); !r.OK() {
		t.Fatalf("SetKeyFile failed: %d", r.Code)
	}
}

// TestPollWithoutInit verifies that poll is safe before init.
func TestPollWithoutInit(t *testing.T) {
	Poll() // should not crash
}

// TestListenAndConnect exercises the full listener + connect API surface.
func TestListenAndConnect(t *testing.T) {
	r := Init()
	if !r.OK() {
		t.Fatalf("Init failed: %d", r.Code)
	}
	defer Shutdown()

	cfg := NewConfig()
	if cfg == nil {
		t.Fatal("NewConfig returned nil")
	}
	defer cfg.Free()

	// Start a listener
	ln, r := Listen(cfg, "127.0.0.1", 0)
	if !r.OK() {
		t.Fatalf("Listen failed: %d", r.Code)
	}
	defer ln.Close()

	// Connect a client (handshake won't complete without matching
	// server crypto, but the API layer exercises successfully)
	conn, r := Connect(cfg, "127.0.0.1", 12345)
	if !r.OK() {
		t.Fatalf("Connect failed: %d", r.Code)
	}

	// Poll a few iterations to drive the event loop
	for i := 0; i < 10; i++ {
		Poll()
	}

	state := conn.State()
	t.Logf("Connection state after polling: %d", state)

	conn.Close()
}
