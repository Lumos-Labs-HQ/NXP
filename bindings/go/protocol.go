package nxp

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/url"
	"runtime/cgo"
	"strconv"
	"sync"
	"unsafe"
)

// URL schemes for the NXP protocol.
const (
	SchemeNXP  = "nxp"  // Standard NXP connection
	SchemeNXPS = "nxps" // Secure NXP connection (TLS)
)

// ── Errors ────────────────────────────────────────────

// Error represents an NXP protocol error with a machine-readable code.
type Error struct {
	Code int
	Msg  string
}

func (e *Error) Error() string {
	return "nxp: " + e.Msg
}

var (
	ErrUnsupportedScheme = errors.New("nxp: unsupported URL scheme (use nxp:// or nxps://)")
	ErrMissingHost       = errors.New("nxp: missing host in URL")
	ErrMissingPort       = errors.New("nxp: missing port in URL")
	ErrInvalidPort       = errors.New("nxp: invalid port number")
	ErrSecureNeedsCert   = errors.New("nxp: nxps:// requires CertFile and KeyFile")
	ErrNotInitialized    = errors.New("nxp: library failed to initialize")
	ErrServerClosed      = errors.New("nxp: server closed")
	ErrConnectionClosed  = errors.New("nxp: connection closed")
)

// ── Auto-initialization ───────────────────────────────

var (
	initOnce sync.Once
	initErr  error
)

// ensureInit initializes the NXP library on first use.
func ensureInit() error {
	initOnce.Do(func() {
		r := Init()
		if !r.OK() {
			initErr = &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
		}
	})
	return initErr
}

// ── URL parsing ───────────────────────────────────────

// ParseURL parses an nxp:// or nxps:// URL into components.
func ParseURL(rawURL string) (scheme, host string, port uint16, err error) {
	u, err := url.Parse(rawURL)
	if err != nil {
		return "", "", 0, fmt.Errorf("nxp: invalid URL: %w", err)
	}

	switch u.Scheme {
	case SchemeNXP, SchemeNXPS:
		scheme = u.Scheme
	case "":
		return "", "", 0, ErrUnsupportedScheme
	default:
		return "", "", 0, ErrUnsupportedScheme
	}

	host = u.Hostname()
	if host == "" {
		return "", "", 0, ErrMissingHost
	}

	portStr := u.Port()
	if portStr == "" {
		return "", "", 0, ErrMissingPort
	}

	p, err := strconv.ParseUint(portStr, 10, 16)
	if err != nil || p == 0 {
		return "", "", 0, ErrInvalidPort
	}
	port = uint16(p)

	return scheme, host, port, nil
}

// ── DialOptions ───────────────────────────────────────

// DialOptions configures a client connection created via Dial.
type DialOptions struct {
	CertFile       string   // TLS certificate file (for nxps://)
	KeyFile        string   // TLS private key file (for nxps://)
	IdleTimeoutMs  uint64   // Idle timeout in milliseconds (0 = default 30s)
	MaxStreamsBidi uint64   // Max bidirectional streams (0 = default 256)
	MaxStreamsUni  uint64   // Max unidirectional streams (0 = default 256)
	HeartbeatMs    uint64   // Heartbeat interval in ms (0 = disabled)
	MaxUDPPayload  uint16   // Max UDP payload (0 = default 1472)
	ALPN           []string // ALPN protocols for negotiation
}

// ── ListenOptions ─────────────────────────────────────

// ListenOptions configures a server listener created via ListenNXP.
type ListenOptions struct {
	CertFile       string   // TLS certificate file (required for nxps://)
	KeyFile        string   // TLS private key file (required for nxps://)
	IdleTimeoutMs  uint64
	MaxStreamsBidi uint64
	MaxStreamsUni  uint64
	HeartbeatMs    uint64
	MaxUDPPayload  uint16
	ALPN           []string // ALPN protocols for negotiation
	AcceptBacklog  int      // Accept channel buffer size (default 128)
}

// ── Connection ────────────────────────────────────────

// Connection represents a connection to an NXP peer.
// Created via Dial("nxp://host:port") or accepted via Server.Accept().
type Connection struct {
	raw    *Conn
	cfg    *Config
	scheme string
	host   string
	port   uint16
	handle cgo.Handle

	mu             sync.Mutex
	established    bool
	closed         bool
	readyCh        chan struct{}  // closed when handshake completes
	doneCh         chan struct{}  // closed when connection closes
	streamAcceptCh chan *NXPStream
	server         bool // true if server-accepted
}

// Dial connects to an NXP server using a URL.
// Starts the background event loop automatically.
//
// Supported schemes:
//   - nxp://host:port   — standard connection
//   - nxps://host:port  — secure connection (TLS)
//
// Example:
//
//	conn, err := nxp.Dial("nxp://127.0.0.1:9000", nil)
//	conn, err := nxp.Dial("nxps://example.com:443", &nxp.DialOptions{
//	    CertFile: "client.pem",
//	    KeyFile:  "client-key.pem",
//	})
func Dial(rawURL string, opts *DialOptions) (*Connection, error) {
	return DialContext(context.Background(), rawURL, opts)
}

// DialContext connects to an NXP server with context for cancellation/timeout.
func DialContext(ctx context.Context, rawURL string, opts *DialOptions) (*Connection, error) {
	if err := ensureInit(); err != nil {
		return nil, err
	}

	scheme, host, port, err := ParseURL(rawURL)
	if err != nil {
		return nil, err
	}

	// Check context before doing work
	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	default:
	}

	cfg := NewConfig()
	if cfg == nil {
		return nil, &Error{Code: ErrOutOfMemory, Msg: "failed to allocate config"}
	}

	applyDialOptions(cfg, opts, scheme)

	// Create connection object with channels
	c := &Connection{
		cfg:            cfg,
		scheme:         scheme,
		host:           host,
		port:           port,
		readyCh:        make(chan struct{}),
		doneCh:         make(chan struct{}),
		streamAcceptCh: make(chan *NXPStream, 16),
	}

	// Create cgo.Handle for callback bridge
	h := cgo.NewHandle(c)
	c.handle = h

	// Start event loop
	startEventLoop()

	// Connect with callbacks
	conn, r := ConnectWithCb(cfg, host, port, unsafe.Pointer(uintptr(h)))
	if !r.OK() {
		h.Delete()
		cfg.Free()
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}

	c.raw = conn

	// Register stream accept callback
	conn.SetStreamAcceptCb(unsafe.Pointer(uintptr(h)))

	return c, nil
}

func applyDialOptions(cfg *Config, opts *DialOptions, scheme string) {
	if opts == nil {
		return
	}
	if opts.CertFile != "" {
		cfg.SetCertFile(opts.CertFile)
	}
	if opts.KeyFile != "" {
		cfg.SetKeyFile(opts.KeyFile)
	}
	if opts.IdleTimeoutMs > 0 {
		cfg.SetIdleTimeout(opts.IdleTimeoutMs)
	}
	if opts.MaxStreamsBidi > 0 || opts.MaxStreamsUni > 0 {
		cfg.SetMaxStreams(opts.MaxStreamsBidi, opts.MaxStreamsUni)
	}
	if opts.HeartbeatMs > 0 {
		cfg.SetHeartbeatInterval(opts.HeartbeatMs)
	}
	if opts.MaxUDPPayload > 0 {
		cfg.SetMaxUDPPayload(opts.MaxUDPPayload)
	}
	if len(opts.ALPN) > 0 {
		cfg.SetALPN(opts.ALPN)
	}
}

// WaitReady blocks until the handshake completes or the context is cancelled.
func (c *Connection) WaitReady(ctx context.Context) error {
	select {
	case <-c.readyCh:
		return nil
	case <-c.doneCh:
		return ErrConnectionClosed
	case <-ctx.Done():
		return ctx.Err()
	}
}

// Done returns a channel that is closed when the connection closes.
func (c *Connection) Done() <-chan struct{} {
	return c.doneCh
}

// AcceptStream waits for a peer-initiated stream on this connection.
func (c *Connection) AcceptStream(ctx context.Context) (*NXPStream, error) {
	select {
	case stream, ok := <-c.streamAcceptCh:
		if !ok {
			return nil, ErrConnectionClosed
		}
		return stream, nil
	case <-c.doneCh:
		return nil, ErrConnectionClosed
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// OpenStream opens a new outgoing stream on this connection.
// Stream type is one of StreamReliable, StreamFast, StreamMedia, StreamFile.
func (c *Connection) OpenStream(stype int) (*NXPStream, error) {
	return c.OpenStreamWithPriority(stype, 0)
}

// OpenStreamWithPriority opens a stream with a specific priority (0-255, lower = higher).
func (c *Connection) OpenStreamWithPriority(stype int, priority uint8) (*NXPStream, error) {
	c.mu.Lock()
	if c.closed {
		c.mu.Unlock()
		return nil, ErrConnectionClosed
	}
	c.mu.Unlock()

	rs, r := OpenStream(c.raw, stype, priority)
	if !r.OK() {
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}
	return &NXPStream{raw: rs, conn: c}, nil
}

// URL returns the connection URL string (e.g. "nxp://127.0.0.1:9000").
func (c *Connection) URL() string {
	return fmt.Sprintf("%s://%s:%d", c.scheme, c.host, c.port)
}

// IsSecure returns true if this is an nxps:// connection.
func (c *Connection) IsSecure() bool {
	return c.scheme == SchemeNXPS
}

// State returns the connection state (see Conn* constants).
func (c *Connection) State() int {
	return c.raw.State()
}

// Statistics returns connection-level statistics.
func (c *Connection) Statistics() ConnStats {
	return c.raw.Stats()
}

// Close gracefully closes the connection and frees resources.
func (c *Connection) Close() error {
	c.mu.Lock()
	alreadyClosed := c.closed
	if !c.closed {
		c.closed = true
	}
	c.mu.Unlock()

	if c.raw != nil {
		c.raw.Close()
	}

	// Only free config for client-initiated connections (server conns don't own config)
	if c.cfg != nil {
		c.cfg.Free()
		c.cfg = nil
	}

	// Close channels if not already closed
	if !alreadyClosed {
		// doneCh may already be closed by goOnClosed callback
		select {
		case <-c.doneCh:
		default:
			close(c.doneCh)
		}
	}

	// NOTE: Do NOT delete cgo handle here. The C event loop may still
	// fire the on_closed callback asynchronously via nxp_poll(). The
	// handle is deleted in goOnClosed after the C library is done with it.

	return nil
}

// ── NXPStream ─────────────────────────────────────────

// NXPStream represents a data stream within an NXP connection.
// It implements io.ReadWriteCloser for seamless integration with Go I/O.
type NXPStream struct {
	raw  *RawStream
	conn *Connection
	mu   sync.Mutex
}

// Compile-time check that NXPStream implements io.ReadWriteCloser.
var _ io.ReadWriteCloser = (*NXPStream)(nil)

// Write sends data on the stream. Implements io.Writer.
func (s *NXPStream) Write(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(p) == 0 {
		return 0, nil
	}
	n := s.raw.Send(p, false)
	if n < 0 {
		return 0, &Error{Code: n, Msg: "stream write failed"}
	}
	return n, nil
}

// Read receives data from the stream. Implements io.Reader.
// Returns io.EOF when the stream's FIN flag is received.
func (s *NXPStream) Read(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(p) == 0 {
		return 0, nil
	}
	n, fin := s.raw.Recv(p)
	if n == 0 && fin {
		return 0, io.EOF
	}
	if n == 0 {
		return 0, &Error{Code: ErrWouldBlock, Msg: "no data available"}
	}
	if fin {
		return n, io.EOF
	}
	return n, nil
}

// Close closes the stream, sending FIN. Implements io.Closer.
func (s *NXPStream) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.raw.Close()
	return nil
}

// ID returns the stream identifier.
func (s *NXPStream) ID() uint64 {
	return s.raw.ID()
}

// WriteFin sends data with the FIN flag, signaling end-of-stream.
func (s *NXPStream) WriteFin(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	n := s.raw.Send(p, true)
	if n < 0 {
		return 0, &Error{Code: n, Msg: "stream write failed"}
	}
	return n, nil
}

// Shutdown selectively shuts down the stream.
// Use ShutdownRead, ShutdownWrite, or ShutdownBoth.
func (s *NXPStream) Shutdown(dir int) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.raw.Shutdown(dir)
	return nil
}

// State returns the stream state (see Stream* constants).
func (s *NXPStream) State() int {
	return s.raw.StreamState()
}

// Writable returns bytes of write buffer space available (backpressure).
func (s *NXPStream) Writable() int {
	return s.raw.Writable()
}

// Readable returns bytes available to read.
func (s *NXPStream) Readable() int {
	return s.raw.Readable()
}

// ── Server ────────────────────────────────────────────

// Server represents an NXP server listener.
// Created via ListenNXP("nxp://0.0.0.0:9000") or ListenNXP("nxps://0.0.0.0:443").
type Server struct {
	raw    *RawListener
	cfg    *Config
	scheme string
	addr   string
	port   uint16
	handle cgo.Handle

	mu       sync.Mutex
	closed   bool
	acceptCh chan *Connection
}

// ListenNXP creates a server listener on the given URL.
// Starts the background event loop automatically.
//
// Supported schemes:
//   - nxp://addr:port   — standard listener
//   - nxps://addr:port  — secure listener (requires CertFile + KeyFile)
//
// Example:
//
//	srv, err := nxp.ListenNXP("nxp://0.0.0.0:9000", nil)
//	srv, err := nxp.ListenNXP("nxps://0.0.0.0:443", &nxp.ListenOptions{
//	    CertFile: "server.pem",
//	    KeyFile:  "server-key.pem",
//	})
func ListenNXP(rawURL string, opts *ListenOptions) (*Server, error) {
	if err := ensureInit(); err != nil {
		return nil, err
	}

	scheme, addr, port, err := ParseURL(rawURL)
	if err != nil {
		return nil, err
	}

	// Secure listeners require cert + key
	if scheme == SchemeNXPS {
		if opts == nil || opts.CertFile == "" || opts.KeyFile == "" {
			return nil, ErrSecureNeedsCert
		}
	}

	cfg := NewConfig()
	if cfg == nil {
		return nil, &Error{Code: ErrOutOfMemory, Msg: "failed to allocate config"}
	}

	applyListenOptions(cfg, opts)

	backlog := 128
	if opts != nil && opts.AcceptBacklog > 0 {
		backlog = opts.AcceptBacklog
	}

	srv := &Server{
		cfg:      cfg,
		scheme:   scheme,
		addr:     addr,
		port:     port,
		acceptCh: make(chan *Connection, backlog),
	}

	// Create cgo.Handle for callback bridge
	h := cgo.NewHandle(srv)
	srv.handle = h

	// Start event loop
	startEventLoop()

	// Listen with callbacks
	ln, r := ListenWithCb(cfg, addr, port, unsafe.Pointer(uintptr(h)))
	if !r.OK() {
		h.Delete()
		cfg.Free()
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}

	srv.raw = ln
	return srv, nil
}

func applyListenOptions(cfg *Config, opts *ListenOptions) {
	if opts == nil {
		return
	}
	if opts.CertFile != "" {
		cfg.SetCertFile(opts.CertFile)
	}
	if opts.KeyFile != "" {
		cfg.SetKeyFile(opts.KeyFile)
	}
	if opts.IdleTimeoutMs > 0 {
		cfg.SetIdleTimeout(opts.IdleTimeoutMs)
	}
	if opts.MaxStreamsBidi > 0 || opts.MaxStreamsUni > 0 {
		cfg.SetMaxStreams(opts.MaxStreamsBidi, opts.MaxStreamsUni)
	}
	if opts.HeartbeatMs > 0 {
		cfg.SetHeartbeatInterval(opts.HeartbeatMs)
	}
	if opts.MaxUDPPayload > 0 {
		cfg.SetMaxUDPPayload(opts.MaxUDPPayload)
	}
	if len(opts.ALPN) > 0 {
		cfg.SetALPN(opts.ALPN)
	}
}

// Accept waits for and returns the next incoming connection.
// Blocks until a connection arrives or the server is closed.
func (s *Server) Accept() (*Connection, error) {
	return s.AcceptContext(context.Background())
}

// AcceptContext waits for an incoming connection with cancellation support.
func (s *Server) AcceptContext(ctx context.Context) (*Connection, error) {
	select {
	case conn, ok := <-s.acceptCh:
		if !ok {
			return nil, ErrServerClosed
		}
		return conn, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// Addr returns the server address as a URL string.
func (s *Server) Addr() string {
	return fmt.Sprintf("%s://%s:%d", s.scheme, s.addr, s.port)
}

// IsSecure returns true if this is an nxps:// listener.
func (s *Server) IsSecure() bool {
	return s.scheme == SchemeNXPS
}

// Close stops the listener and frees all resources.
func (s *Server) Close() error {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return nil
	}
	s.closed = true
	s.mu.Unlock()

	// Close accept channel to unblock Accept()
	close(s.acceptCh)

	if s.raw != nil {
		s.raw.Close()
	}
	if s.cfg != nil {
		s.cfg.Free()
		s.cfg = nil
	}

	// Release cgo handle
	if s.handle != 0 {
		s.handle.Delete()
		s.handle = 0
	}

	return nil
}

// ── Library Lifecycle ─────────────────────────────────

// GracefulShutdown stops the event loop and closes all connections/listeners.
func GracefulShutdown() {
	stopEventLoop()
	Shutdown()
}
