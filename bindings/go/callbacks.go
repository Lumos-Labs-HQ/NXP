package nxp

/*
#include "nxp/nxp.h"
#include "nxp/nxp_connection.h"
#include "nxp/nxp_listener.h"
#include "nxp/nxp_stream.h"
*/
import "C"

import (
	"runtime/cgo"
	"sync"
	"time"
	"unsafe"
)

// ── Event Loop Goroutine ─────────────────────────────

var (
	eventLoopOnce   sync.Once
	eventLoopStopCh chan struct{}
)

// startEventLoop launches a background goroutine that drives the C event loop.
// It is auto-started on first Dial() or ListenNXP() call.
func startEventLoop() {
	eventLoopOnce.Do(func() {
		eventLoopStopCh = make(chan struct{})
		go func() {
			ticker := time.NewTicker(1 * time.Millisecond)
			defer ticker.Stop()
			for {
				select {
				case <-eventLoopStopCh:
					return
				case <-ticker.C:
					C.nxp_poll()
				}
			}
		}()
	})
}

// stopEventLoop signals the background goroutine to stop.
func stopEventLoop() {
	if eventLoopStopCh != nil {
		select {
		case <-eventLoopStopCh:
			// Already closed
		default:
			close(eventLoopStopCh)
		}
	}
}

// ── Callback Trampolines ─────────────────────────────
//
// These //export functions are called from C (via nxp_poll → event callbacks).
// They recover the Go object from cgo.Handle and dispatch events to channels.
//
// IMPORTANT: All channel sends use select+default to avoid blocking the
// C event loop thread. If the channel is full, the event is dropped.

//export goOnNewConn
func goOnNewConn(ln *C.nxp_listener, conn *C.nxp_conn, ud unsafe.Pointer) {
	h := cgo.Handle(uintptr(ud))
	srv, ok := h.Value().(*Server)
	if !ok || srv == nil {
		return
	}

	// Create a Connection wrapper for the accepted connection.
	// Server-accepted connections don't own a Config (the listener does).
	wrapped := &Connection{
		raw:            &Conn{c: conn},
		scheme:         srv.scheme,
		host:           srv.addr,
		port:           srv.port,
		readyCh:        make(chan struct{}),
		doneCh:         make(chan struct{}),
		streamAcceptCh: make(chan *NXPStream, 16),
		server:         true,
	}

	// Register a cgo.Handle for connection callbacks
	connHandle := cgo.NewHandle(wrapped)
	wrapped.handle = connHandle

	// Register connected/closed callbacks on this server-accepted connection
	wrapped.raw.SetCallbacks(unsafe.Pointer(uintptr(connHandle)))

	// Register stream accept callback on this connection
	wrapped.raw.SetStreamAcceptCb(unsafe.Pointer(uintptr(connHandle)))

	srv.mu.Lock()
	defer srv.mu.Unlock()
	if srv.closed {
		connHandle.Delete()
		C.nxp_conn_close(conn, 0)
		return
	}

	select {
	case srv.acceptCh <- wrapped:
	default:
		// Accept backlog full — reject connection
		connHandle.Delete()
		C.nxp_conn_close(conn, 0)
	}
}

//export goOnConnected
func goOnConnected(conn *C.nxp_conn, ud unsafe.Pointer) {
	h := cgo.Handle(uintptr(ud))
	c, ok := h.Value().(*Connection)
	if !ok || c == nil {
		return
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	if !c.established {
		c.established = true
		close(c.readyCh)
	}
}

//export goOnClosed
func goOnClosed(conn *C.nxp_conn, ud unsafe.Pointer) {
	h := cgo.Handle(uintptr(ud))
	c, ok := h.Value().(*Connection)
	if !ok || c == nil {
		return
	}

	c.mu.Lock()
	if !c.closed {
		c.closed = true
		close(c.doneCh)
		close(c.streamAcceptCh)
	}
	c.mu.Unlock()

	// C library is done with this connection — release the cgo handle.
	h.Delete()
}

//export goOnStreamAccept
func goOnStreamAccept(conn *C.nxp_conn, stream *C.nxp_stream, ud unsafe.Pointer) {
	h := cgo.Handle(uintptr(ud))
	c, ok := h.Value().(*Connection)
	if !ok || c == nil {
		C.nxp_stream_close(stream)
		return
	}

	wrapped := &NXPStream{
		raw:  &RawStream{s: stream},
		conn: c,
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed {
		C.nxp_stream_close(stream)
		return
	}

	select {
	case c.streamAcceptCh <- wrapped:
	default:
		C.nxp_stream_close(stream)
	}
}
