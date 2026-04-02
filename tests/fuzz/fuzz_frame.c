/*
 * Simple fuzzing test for frame decoding
 */
#include "../../src/core/frame_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main() {
    srand(time(NULL));
    
    printf("Fuzzing frame decoder...\n");
    
    int iterations = 10000;
    
    for (int i = 0; i < iterations; i++) {
        // Generate random frame
        size_t size = rand() % 1500 + 1;
        uint8_t *data = malloc(size);
        
        for (size_t j = 0; j < size; j++) {
            data[j] = rand() % 256;
        }
        
        // Try to decode
        nxp_frame frame;
        size_t consumed = nxp_frame_decode(data, size, &frame);
        
        // Should not crash, just return 0 or valid size
        (void)consumed;
        
        free(data);
        
        if ((i + 1) % 1000 == 0) {
            printf("  Tested %d frames...\n", i + 1);
        }
    }
    
    printf("✅ Tested %d random frames - no crashes!\n", iterations);
    return 0;
}
