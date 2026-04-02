/*
 * Simple fuzzing test for packet decoding
 */
#include "../../src/core/packet_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main() {
    srand(time(NULL));
    
    printf("Fuzzing packet decoder...\n");
    
    int iterations = 10000;
    int crashes = 0;
    
    for (int i = 0; i < iterations; i++) {
        // Generate random packet
        size_t size = rand() % 1500 + 1;
        uint8_t *data = malloc(size);
        
        for (size_t j = 0; j < size; j++) {
            data[j] = rand() % 256;
        }
        
        // Try to decode
        nxp_pkt_long_header hdr;
        nxp_result r = nxp_pkt_decode_long_header(data, size, &hdr);
        
        // Should not crash, just return error
        (void)r;
        
        free(data);
        
        if ((i + 1) % 1000 == 0) {
            printf("  Tested %d packets...\n", i + 1);
        }
    }
    
    printf("✅ Tested %d random packets - no crashes!\n", iterations);
    return 0;
}
