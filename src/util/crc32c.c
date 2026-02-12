/*
 * NXP CRC32C - Implementation
 *
 * Table-driven CRC32C (Castagnoli) with polynomial 0x1EDC6F41.
 * This is the same CRC used by iSCSI, SCTP, and Btrfs.
 * Table is computed once at initialization from the polynomial.
 */
#include "crc32c.h"

#include <stdbool.h>

/* CRC32C polynomial in reflected (reversed) form */
#define CRC32C_POLY 0x82F63B78u

static uint32_t crc32c_table[256];
static bool     crc32c_table_init = false;

static void crc32c_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32C_POLY;
            } else {
                crc >>= 1;
            }
        }
        crc32c_table[i] = crc;
    }
    crc32c_table_init = true;
}

uint32_t nxp_crc32c_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!crc32c_table_init) {
        crc32c_init_table();
    }

    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

uint32_t nxp_crc32c(const uint8_t *data, size_t len) {
    return nxp_crc32c_update(0, data, len);
}
