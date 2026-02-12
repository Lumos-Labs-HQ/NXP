/*
 * NXP Lock-Free SPSC Ring Buffer - Implementation
 */
#include "ring_buffer.h"

#include <stdlib.h>
#include <string.h>

static size_t next_power_of_2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

nxp_ring_buffer *nxp_ring_buffer_create(size_t capacity) {
    if (capacity == 0) capacity = 4096;
    capacity = next_power_of_2(capacity);

    nxp_ring_buffer *rb = (nxp_ring_buffer *)malloc(sizeof(nxp_ring_buffer));
    if (rb == nullptr) return nullptr;

    rb->data = (uint8_t *)malloc(capacity);
    if (rb->data == nullptr) {
        free(rb);
        return nullptr;
    }

    rb->capacity = capacity;
    rb->mask     = capacity - 1;
    atomic_init(&rb->write_pos, 0);
    atomic_init(&rb->read_pos, 0);

    return rb;
}

void nxp_ring_buffer_destroy(nxp_ring_buffer *rb) {
    if (rb == nullptr) return;
    free(rb->data);
    free(rb);
}

size_t nxp_ring_buffer_write(nxp_ring_buffer *rb, const uint8_t *data, size_t len) {
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_relaxed);
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_acquire);

    size_t available = rb->capacity - (w - r);
    if (len > available) {
        len = available;
    }
    if (len == 0) return 0;

    size_t w_idx = w & rb->mask;
    size_t first = rb->capacity - w_idx;

    if (first >= len) {
        memcpy(rb->data + w_idx, data, len);
    } else {
        memcpy(rb->data + w_idx, data, first);
        memcpy(rb->data, data + first, len - first);
    }

    atomic_store_explicit(&rb->write_pos, w + len, memory_order_release);
    return len;
}

size_t nxp_ring_buffer_read(nxp_ring_buffer *rb, uint8_t *buf, size_t len) {
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_acquire);
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_relaxed);

    size_t available = w - r;
    if (len > available) {
        len = available;
    }
    if (len == 0) return 0;

    size_t r_idx = r & rb->mask;
    size_t first = rb->capacity - r_idx;

    if (first >= len) {
        memcpy(buf, rb->data + r_idx, len);
    } else {
        memcpy(buf, rb->data + r_idx, first);
        memcpy(buf + first, rb->data, len - first);
    }

    atomic_store_explicit(&rb->read_pos, r + len, memory_order_release);
    return len;
}

size_t nxp_ring_buffer_readable(const nxp_ring_buffer *rb) {
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_acquire);
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_acquire);
    return w - r;
}

size_t nxp_ring_buffer_writable(const nxp_ring_buffer *rb) {
    size_t w = atomic_load_explicit(&rb->write_pos, memory_order_acquire);
    size_t r = atomic_load_explicit(&rb->read_pos, memory_order_acquire);
    return rb->capacity - (w - r);
}
