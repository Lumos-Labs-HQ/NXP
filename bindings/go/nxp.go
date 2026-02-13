// Package nxp provides Go bindings for the NXP transport protocol library.
//
// NXP (NEXUS Protocol) is a custom transport protocol over UDP, inspired by QUIC,
// with support for multiple stream types, congestion control, and 0-RTT.
//
// Use nxp:// for standard connections and nxps:// for secure (TLS) connections:
//
//	conn, err := nxp.Dial("nxp://127.0.0.1:9000", nil)
//	conn, err := nxp.Dial("nxps://example.com:443", &nxp.DialOptions{CertFile: "cert.pem", KeyFile: "key.pem"})
package nxp

/*
#cgo CFLAGS: -I${SRCDIR}/../../include -DNXP_DEBUG=1
#cgo LDFLAGS: -L${SRCDIR}/../../build/src -lnxp_api -lnxp_core -lnxp_congestion -lnxp_crypto -lnxp_platform -lnxp_memory -lnxp_util -lssl -lcrypto -lpthread -lm

#include "nxp/nxp.h"
#include "nxp/nxp_error.h"
#include "nxp/nxp_config.h"
#include "nxp/nxp_stream.h"
#include "nxp/nxp_connection.h"
#include "nxp/nxp_listener.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations for Go callbacks (defined via //export in callbacks.go)
extern void goOnConnected(nxp_conn *conn, void *ud);
extern void goOnClosed(nxp_conn *conn, void *ud);
extern void goOnNewConn(nxp_listener *ln, nxp_conn *conn, void *ud);
extern void goOnStreamAccept(nxp_conn *conn, nxp_stream *stream, void *ud);

// C helpers that bind Go callbacks to C function pointers.
// These live here (not in callbacks.go) because //export files
// cannot have definitions in the CGo preamble.

static nxp_result nxp_connect_go(const nxp_config *cfg, const char *host,
                                  uint16_t port, void *ud, nxp_conn **out) {
    return nxp_connect(cfg, host, port, goOnConnected, goOnClosed, ud, out);
}

static nxp_result nxp_listen_go(const nxp_config *cfg, const char *addr,
                                 uint16_t port, void *ud, nxp_listener **out) {
    return nxp_listen(cfg, addr, port, goOnNewConn, ud, out);
}

static void nxp_conn_set_stream_accept_go(nxp_conn *conn, void *ud) {
    nxp_conn_set_stream_accept_cb(conn, goOnStreamAccept, ud);
}

static void nxp_conn_set_callbacks_go(nxp_conn *conn, void *ud) {
    nxp_conn_set_callbacks(conn, goOnConnected, goOnClosed, ud);
}
*/
import "C"
import "unsafe"

// ── Stream type constants ──────────────────────────────

const (
	StreamReliable = 0 // Ordered, reliable delivery (like TCP)
	StreamFast     = 1 // Unordered, unreliable (raw UDP-like)
	StreamMedia    = 2 // Partially reliable (drop old frames)
	StreamFile     = 3 // Reliable, ordered, bulk-optimized
)

// ── Connection state constants ────────────────────────

const (
	ConnIdle          = 0
	ConnHandshakeInit = 1
	ConnHandshaking   = 2
	ConnEstablished   = 3
	ConnClosing       = 4
	ConnDraining      = 5
	ConnClosed        = 6
)

// ── Stream state constants ────────────────────────────

const (
	StreamIdle             = 0
	StreamOpen             = 1
	StreamHalfClosedLocal  = 2
	StreamHalfClosedRemote = 3
	StreamClosed           = 4
	StreamReset            = 5
)

// ── Shutdown direction constants ──────────────────────

const (
	ShutdownRead  = 0
	ShutdownWrite = 1
	ShutdownBoth  = 2
)

// ── Error code constants ──────────────────────────────

const (
	OK                = 0
	ErrInvalid        = -1
	ErrOutOfMemory    = -2
	ErrBufferTooSmall = -3
	ErrWouldBlock     = -4
	ErrConnClosed     = -5
	ErrStreamClosed   = -6
	ErrFlowControl    = -7
	ErrCryptoFail     = -8
	ErrHandshake      = -9
	ErrInvalidPacket  = -10
	ErrInvalidFrame   = -11
	ErrIdleTimeout    = -12
	ErrInternal       = -14
	ErrPlatform       = -15
	ErrStreamLimit    = -19
	ErrCongestion     = -20
)

// ── Result ────────────────────────────────────────────

// Result wraps nxp_result for low-level C API calls.
type Result struct {
	Code int
}

// OK returns true if the result indicates success.
func (r Result) OK() bool { return r.Code == 0 }

// ── Library Lifecycle ─────────────────────────────────

// Init initializes the NXP library. Must be called before any other function.
func Init() Result {
	config := C.nxp_global_config{}
	r := C.nxp_init(&config)
	return Result{Code: int(r.code)}
}

// Shutdown cleans up all NXP resources.
func Shutdown() {
	C.nxp_shutdown()
}

// Poll drives the event loop once (non-blocking).
func Poll() {
	C.nxp_poll()
}

// Run runs the event loop until shutdown (blocking).
func Run() {
	C.nxp_run()
}

// ErrorStr returns a human-readable string for an error code.
func ErrorStr(code int) string {
	return C.GoString(C.nxp_error_str(C.nxp_error_code(code)))
}

// ── Config ────────────────────────────────────────────

// Config wraps nxp_config for connection/listener setup.
type Config struct {
	c *C.nxp_config
}

// NewConfig creates a new configuration with sensible defaults.
func NewConfig() *Config {
	c := C.nxp_config_new()
	if c == nil {
		return nil
	}
	return &Config{c: c}
}

// Free releases the config resources.
func (cfg *Config) Free() {
	if cfg.c != nil {
		C.nxp_config_free(cfg.c)
		cfg.c = nil
	}
}

// SetIdleTimeout sets the idle timeout in milliseconds.
func (cfg *Config) SetIdleTimeout(ms uint64) Result {
	r := C.nxp_config_set_idle_timeout(cfg.c, C.uint64_t(ms))
	return Result{Code: int(r.code)}
}

// SetMaxStreams sets maximum bidirectional and unidirectional streams.
func (cfg *Config) SetMaxStreams(bidi, uni uint64) Result {
	r := C.nxp_config_set_max_streams(cfg.c, C.uint64_t(bidi), C.uint64_t(uni))
	return Result{Code: int(r.code)}
}

// SetHeartbeatInterval sets the heartbeat interval in milliseconds.
func (cfg *Config) SetHeartbeatInterval(ms uint64) Result {
	r := C.nxp_config_set_heartbeat_interval(cfg.c, C.uint64_t(ms))
	return Result{Code: int(r.code)}
}

// SetMaxUDPPayload sets the maximum UDP payload size (1200-1500).
func (cfg *Config) SetMaxUDPPayload(size uint16) Result {
	r := C.nxp_config_set_max_udp_payload(cfg.c, C.uint16_t(size))
	return Result{Code: int(r.code)}
}

// SetCertFile sets the TLS certificate file path.
func (cfg *Config) SetCertFile(path string) Result {
	cs := C.CString(path)
	defer C.free(unsafe.Pointer(cs))
	r := C.nxp_config_set_cert_file(cfg.c, cs)
	return Result{Code: int(r.code)}
}

// SetKeyFile sets the TLS private key file path.
func (cfg *Config) SetKeyFile(path string) Result {
	cs := C.CString(path)
	defer C.free(unsafe.Pointer(cs))
	r := C.nxp_config_set_key_file(cfg.c, cs)
	return Result{Code: int(r.code)}
}

// SetALPN sets ALPN protocols for negotiation.
func (cfg *Config) SetALPN(protocols []string) Result {
	if len(protocols) == 0 {
		return Result{Code: ErrInvalid}
	}
	cStrs := make([]*C.char, len(protocols))
	for i, p := range protocols {
		cStrs[i] = C.CString(p)
	}
	r := C.nxp_config_set_alpn(cfg.c, &cStrs[0], C.size_t(len(protocols)))
	for _, cs := range cStrs {
		C.free(unsafe.Pointer(cs))
	}
	return Result{Code: int(r.code)}
}

// ── Conn ──────────────────────────────────────────────

// Conn wraps a raw nxp_conn handle.
type Conn struct {
	c *C.nxp_conn
}

// Connect creates a client connection to host:port (no callbacks).
func Connect(cfg *Config, host string, port uint16) (*Conn, Result) {
	cs := C.CString(host)
	defer C.free(unsafe.Pointer(cs))

	var cfgC *C.nxp_config
	if cfg != nil {
		cfgC = cfg.c
	}

	var conn *C.nxp_conn
	r := C.nxp_connect(cfgC, cs, C.uint16_t(port), nil, nil, nil, &conn)
	if r.code != C.NXP_OK {
		return nil, Result{Code: int(r.code)}
	}
	return &Conn{c: conn}, Result{Code: 0}
}

// ConnectWithCb creates a connection with Go callback bridge.
// The ud parameter is passed to goOnConnected/goOnClosed callbacks.
func ConnectWithCb(cfg *Config, host string, port uint16, ud unsafe.Pointer) (*Conn, Result) {
	cs := C.CString(host)
	defer C.free(unsafe.Pointer(cs))

	var cfgC *C.nxp_config
	if cfg != nil {
		cfgC = cfg.c
	}

	var conn *C.nxp_conn
	r := C.nxp_connect_go(cfgC, cs, C.uint16_t(port), ud, &conn)
	if r.code != C.NXP_OK {
		return nil, Result{Code: int(r.code)}
	}
	return &Conn{c: conn}, Result{Code: 0}
}

// Close closes the connection with error code 0.
func (c *Conn) Close() {
	if c.c != nil {
		C.nxp_conn_close(c.c, 0)
	}
}

// State returns the connection state (see Conn* constants).
func (c *Conn) State() int {
	return int(C.nxp_conn_get_state(c.c))
}

// Stats returns connection statistics.
func (c *Conn) Stats() ConnStats {
	s := C.nxp_conn_get_stats(c.c)
	return ConnStats{
		BytesSent:     uint64(s.bytes_sent),
		BytesRecv:     uint64(s.bytes_recv),
		PacketsSent:   uint64(s.packets_sent),
		PacketsRecv:   uint64(s.packets_recv),
		PacketsLost:   uint64(s.packets_lost),
		RTTMinUs:      uint64(s.rtt_min_us),
		RTTSmoothedUs: uint64(s.rtt_smoothed_us),
		CWnd:          uint64(s.cwnd),
		BytesInFlight: uint64(s.bytes_in_flight),
		StreamsOpened: uint64(s.streams_opened),
		HandshakeUs:   uint64(s.handshake_time_us),
	}
}

// SetStreamAcceptCb registers the Go stream accept callback on this connection.
func (c *Conn) SetStreamAcceptCb(ud unsafe.Pointer) {
	if c.c != nil {
		C.nxp_conn_set_stream_accept_go(c.c, ud)
	}
}

// SetCallbacks registers connected/closed callbacks on this connection.
func (c *Conn) SetCallbacks(ud unsafe.Pointer) {
	if c.c != nil {
		C.nxp_conn_set_callbacks_go(c.c, ud)
	}
}

// ConnStats holds connection-level statistics.
type ConnStats struct {
	BytesSent     uint64
	BytesRecv     uint64
	PacketsSent   uint64
	PacketsRecv   uint64
	PacketsLost   uint64
	RTTMinUs      uint64
	RTTSmoothedUs uint64
	CWnd          uint64
	BytesInFlight uint64
	StreamsOpened uint64
	HandshakeUs   uint64
}

// ── Listener ──────────────────────────────────────────

// RawListener wraps nxp_listener for server-side listening.
type RawListener struct {
	l *C.nxp_listener
}

// RawListen starts a server listener on addr:port (no callbacks).
func RawListen(cfg *Config, addr string, port uint16) (*RawListener, Result) {
	cs := C.CString(addr)
	defer C.free(unsafe.Pointer(cs))

	var cfgC *C.nxp_config
	if cfg != nil {
		cfgC = cfg.c
	}

	var listener *C.nxp_listener
	r := C.nxp_listen(cfgC, cs, C.uint16_t(port), nil, nil, &listener)
	if r.code != C.NXP_OK {
		return nil, Result{Code: int(r.code)}
	}
	return &RawListener{l: listener}, Result{Code: 0}
}

// ListenWithCb starts a listener with Go callback bridge.
func ListenWithCb(cfg *Config, addr string, port uint16, ud unsafe.Pointer) (*RawListener, Result) {
	cs := C.CString(addr)
	defer C.free(unsafe.Pointer(cs))

	var cfgC *C.nxp_config
	if cfg != nil {
		cfgC = cfg.c
	}

	var listener *C.nxp_listener
	r := C.nxp_listen_go(cfgC, cs, C.uint16_t(port), ud, &listener)
	if r.code != C.NXP_OK {
		return nil, Result{Code: int(r.code)}
	}
	return &RawListener{l: listener}, Result{Code: 0}
}

// Close stops the listener and frees resources.
func (l *RawListener) Close() {
	if l.l != nil {
		C.nxp_listener_close(l.l)
		l.l = nil
	}
}

// ── Stream ────────────────────────────────────────────

// RawStream wraps nxp_stream for data I/O.
type RawStream struct {
	s *C.nxp_stream
}

// OpenStream opens a new stream on a connection.
func OpenStream(conn *Conn, stype int, priority uint8) (*RawStream, Result) {
	if conn == nil || conn.c == nil {
		return nil, Result{Code: ErrInvalid}
	}

	var stream *C.nxp_stream
	r := C.nxp_stream_open(
		conn.c,
		C.nxp_stream_type(stype),
		C.uint8_t(priority),
		nil, nil, nil, nil,
		&stream,
	)
	if r.code != C.NXP_OK {
		return nil, Result{Code: int(r.code)}
	}
	return &RawStream{s: stream}, Result{Code: 0}
}

// Send writes data to the stream. Set fin=true to signal end-of-stream.
func (s *RawStream) Send(data []byte, fin bool) int {
	if s.s == nil || len(data) == 0 {
		return 0
	}
	n := C.nxp_stream_send(
		s.s,
		(*C.uint8_t)(unsafe.Pointer(&data[0])),
		C.size_t(len(data)),
		C.bool(fin),
	)
	return int(n)
}

// Recv reads data from the stream. Returns (bytes read, fin flag).
func (s *RawStream) Recv(buf []byte) (int, bool) {
	if s.s == nil || len(buf) == 0 {
		return 0, false
	}
	var fin C.bool
	n := C.nxp_stream_recv(
		s.s,
		(*C.uint8_t)(unsafe.Pointer(&buf[0])),
		C.size_t(len(buf)),
		&fin,
	)
	if n < 0 {
		return 0, bool(fin)
	}
	return int(n), bool(fin)
}

// Close closes the stream, sending FIN and freeing resources.
func (s *RawStream) Close() {
	if s.s != nil {
		C.nxp_stream_close(s.s)
		s.s = nil
	}
}

// Shutdown selectively shuts down the stream (ShutdownRead/ShutdownWrite/ShutdownBoth).
func (s *RawStream) Shutdown(dir int) {
	if s.s != nil {
		C.nxp_stream_shutdown(s.s, C.nxp_shutdown_dir(dir))
	}
}

// ID returns the stream identifier.
func (s *RawStream) ID() uint64 {
	if s.s == nil {
		return ^uint64(0)
	}
	return uint64(C.nxp_stream_get_id(s.s))
}

// Writable returns bytes of write buffer space available.
func (s *RawStream) Writable() int {
	if s.s == nil {
		return 0
	}
	return int(C.nxp_stream_writable(s.s))
}

// Readable returns bytes available to read.
func (s *RawStream) Readable() int {
	if s.s == nil {
		return 0
	}
	return int(C.nxp_stream_readable(s.s))
}

// StreamState returns the stream state (see Stream* constants).
func (s *RawStream) StreamState() int {
	if s.s == nil {
		return StreamClosed
	}
	return int(C.nxp_stream_get_state(s.s))
}
