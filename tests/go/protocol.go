package nxp

import (
	"errors"
	"fmt"
	"io"
	"net/url"
	"strconv"
	"sync"
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

// parseNXPURL parses an nxp:// or nxps:// URL into components.
func parseNXPURL(rawURL string) (scheme, host string, port uint16, err error) {
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
	CertFile           string // TLS certificate file (for nxps://)
	KeyFile            string // TLS private key file (for nxps://)
	IdleTimeoutMs      uint64 // Idle timeout in milliseconds (0 = default 30s)
	MaxStreamsBidi     uint64 // Max bidirectional streams (0 = default 256)
	MaxStreamsUni      uint64 // Max unidirectional streams (0 = default 256)
	HeartbeatMs        uint64 // Heartbeat interval in ms (0 = disabled)
	MaxUDPPayload      uint16 // Max UDP payload (0 = default 1472)
}

// ── ListenOptions ─────────────────────────────────────

// ListenOptions configures a server listener created via ListenNXP.
type ListenOptions struct {
	CertFile           string // TLS certificate file (required for nxps://)
	KeyFile            string // TLS private key file (required for nxps://)
	IdleTimeoutMs      uint64
	MaxStreamsBidi     uint64
	MaxStreamsUni      uint64
	HeartbeatMs        uint64
	MaxUDPPayload      uint16
}

// ── Connection ────────────────────────────────────────

// Connection represents a client connection to an NXP server.
// Created via Dial("nxp://host:port") or Dial("nxps://host:port").
type Connection struct {
	raw    *Conn
	cfg    *Config
	scheme string
	host   string
	port   uint16
}

// Dial connects to an NXP server using a URL.
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
	if err := ensureInit(); err != nil {
		return nil, err
	}

	scheme, host, port, err := parseNXPURL(rawURL)
	if err != nil {
		return nil, err
	}

	cfg := NewConfig()
	if cfg == nil {
		return nil, &Error{Code: ErrOutOfMemory, Msg: "failed to allocate config"}
	}

	// Apply options
	if opts != nil {
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
	}

	// For secure scheme, apply cert/key from options
	if scheme == SchemeNXPS && opts != nil {
		if opts.CertFile != "" {
			cfg.SetCertFile(opts.CertFile)
		}
		if opts.KeyFile != "" {
			cfg.SetKeyFile(opts.KeyFile)
		}
	}

	conn, r := Connect(cfg, host, port)
	if !r.OK() {
		cfg.Free()
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}

	return &Connection{
		raw:    conn,
		cfg:    cfg,
		scheme: scheme,
		host:   host,
		port:   port,
	}, nil
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

// OpenStream opens a new stream on this connection.
// Stream type is one of StreamReliable, StreamFast, StreamMedia, StreamFile.
func (c *Connection) OpenStream(stype int) (*NXPStream, error) {
	rs, r := OpenStream(c.raw, stype, 0)
	if !r.OK() {
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}
	return &NXPStream{raw: rs, conn: c}, nil
}

// Close gracefully closes the connection and frees resources.
func (c *Connection) Close() error {
	if c.raw != nil {
		c.raw.Close()
	}
	if c.cfg != nil {
		c.cfg.Free()
		c.cfg = nil
	}
	return nil
}

// ── NXPStream ─────────────────────────────────────────

// NXPStream represents a data stream within an NXP connection.
// It implements io.ReadWriteCloser for seamless integration with Go I/O.
type NXPStream struct {
	raw  *RawStream
	conn *Connection
}

// Compile-time check that NXPStream implements io.ReadWriteCloser.
var _ io.ReadWriteCloser = (*NXPStream)(nil)

// Write sends data on the stream. Implements io.Writer.
func (s *NXPStream) Write(p []byte) (int, error) {
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
	s.raw.Close()
	return nil
}

// ID returns the stream identifier.
func (s *NXPStream) ID() uint64 {
	return s.raw.ID()
}

// WriteFin sends data with the FIN flag, signaling end-of-stream.
func (s *NXPStream) WriteFin(p []byte) (int, error) {
	n := s.raw.Send(p, true)
	if n < 0 {
		return 0, &Error{Code: n, Msg: "stream write failed"}
	}
	return n, nil
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
}

// ListenNXP creates a server listener on the given URL.
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

	scheme, addr, port, err := parseNXPURL(rawURL)
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

	if opts != nil {
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
	}

	ln, r := RawListen(cfg, addr, port)
	if !r.OK() {
		cfg.Free()
		return nil, &Error{Code: r.Code, Msg: ErrorStr(r.Code)}
	}

	return &Server{
		raw:    ln,
		cfg:    cfg,
		scheme: scheme,
		addr:   addr,
		port:   port,
	}, nil
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
	if s.raw != nil {
		s.raw.Close()
	}
	if s.cfg != nil {
		s.cfg.Free()
		s.cfg = nil
	}
	return nil
}
