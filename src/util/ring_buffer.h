/*
 * NXP Lock-Free SPSC Ring Buffer
 *
 * Single-Producer Single-Consumer byte ring buffer.
 * Suitable for cross-thread communication (app thread -> worker thread).
 */
#ifndef NXP_RING_BUFFER_H
#define NXP_RING_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct nxp_ring_buffer {
    uint8_t       *data;
    size_t         capacity;     /* Must be power of 2 */
    size_t         mask;         /* capacity - 1 */
    atomic_size_t  write_pos;    /* Written by producer */
    atomic_size_t  read_pos;     /* Written by consumer */
} nxp_ring_buffer;

/* Create a ring buffer with the given capacity (rounded up to power of 2) */
[[nodiscard]] nxp_ring_buffer *nxp_ring_buffer_create(size_t capacity);

/* Destroy the ring buffer */
void nxp_ring_buffer_destroy(nxp_ring_buffer *rb);

/* Write data into the ring buffer (producer).
 * Returns number of bytes written (may be less than len if full). */
size_t nxp_ring_buffer_write(nxp_ring_buffer *rb, const uint8_t *data, size_t len);

/* Read data from the ring buffer (consumer).
 * Returns number of bytes read (may be less than len if not enough data). */
size_t nxp_ring_buffer_read(nxp_ring_buffer *rb, uint8_t *buf, size_t len);

/* Get number of bytes available to read */
[[nodiscard]] size_t nxp_ring_buffer_readable(const nxp_ring_buffer *rb);

/* Get number of bytes available to write */
[[nodiscard]] size_t nxp_ring_buffer_writable(const nxp_ring_buffer *rb);

/* Check if empty */
[[nodiscard]] static inline bool nxp_ring_buffer_is_empty(const nxp_ring_buffer *rb) {
    return atomic_load_explicit(&rb->write_pos, memory_order_acquire) ==
           atomic_load_explicit(&rb->read_pos, memory_order_acquire);
}

#endif /* NXP_RING_BUFFER_H */
