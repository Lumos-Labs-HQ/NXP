// Package nxp provides Go bindings for the NXP transport protocol library.
package nxp

/*
#cgo CFLAGS: -I${SRCDIR}/../../include -DNXP_DEBUG=1
#cgo LDFLAGS: -L${SRCDIR}/../../build/src -lnxp_api -lnxp_core -lnxp_congestion -lnxp_crypto -lnxp_platform -lnxp_memory -lnxp_util -lssl -lcrypto -lpthread -lm

#include "nxp/nxp.h"
#include "nxp/nxp_error.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"
import "unsafe"

// Result wraps nxp_result
type Result struct {
	Code int
}

func (r Result) OK() bool { return r.Code == 0 }

// Init initializes the NXP library.
func Init() Result {
	config := C.nxp_global_config{}
	r := C.nxp_init(&config)
	return Result{Code: int(r.code)}
}

// Shutdown cleans up the NXP library.
func Shutdown() {
	C.nxp_shutdown()
}

// Poll drives the event loop (non-blocking).
func Poll() {
	C.nxp_poll()
}

// ErrorStr returns a human-readable error string.
func ErrorStr(code int) string {
	return C.GoString(C.nxp_error_str(C.nxp_error_code(code)))
}

// Config wraps nxp_config.
type Config struct {
	c *C.nxp_config
}

// NewConfig creates a new configuration.
func NewConfig() *Config {
	c := C.nxp_config_new()
	if c == nil {
		return nil
	}
	return &Config{c: c}
}

// Free releases the config.
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

// SetMaxUDPPayload sets the maximum UDP payload size.
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

// Conn wraps nxp_conn.
type Conn struct {
	c *C.nxp_conn
}

// Connect creates a client connection.
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

// Close closes the connection.
func (c *Conn) Close() {
	if c.c != nil {
		C.nxp_conn_close(c.c, 0)
	}
}

// State returns the connection state.
func (c *Conn) State() int {
	return int(C.nxp_conn_get_state(c.c))
}

// Listener wraps nxp_listener.
type Listener struct {
	l *C.nxp_listener
}

// Listen starts a server listener.
func Listen(cfg *Config, addr string, port uint16) (*Listener, Result) {
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
	return &Listener{l: listener}, Result{Code: 0}
}

// Close stops the listener.
func (l *Listener) Close() {
	if l.l != nil {
		C.nxp_listener_close(l.l)
		l.l = nil
	}
}

// Error code constants
const (
	OK              = 0
	ErrInvalid      = -1
	ErrOutOfMemory  = -2
	ErrCryptoFail   = -8
	ErrHandshake    = -9
	ErrCongestion   = -20
)
