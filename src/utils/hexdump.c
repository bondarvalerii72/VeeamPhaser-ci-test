#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>

#define HEXDUMP_WIDTH 32

void hexdump(const void *ptr, size_t buflen, const char* prefix) {
    uint8_t *buf = (uint8_t*)ptr;
    uint8_t last_line[HEXDUMP_WIDTH];
    int last_line_valid = 0;
    int duplicate_count = 0;

    for (size_t i = 0; i < buflen; i += HEXDUMP_WIDTH) {
        uint8_t current_line[HEXDUMP_WIDTH] = {0};
        size_t line_size = (i + HEXDUMP_WIDTH < buflen) ? HEXDUMP_WIDTH : (buflen - i);

        // Copy the current line
        memcpy(current_line, &buf[i], line_size);

        // Check if the current line is the same as the last line
        if (last_line_valid && memcmp(current_line, last_line, HEXDUMP_WIDTH) == 0) {
            // Duplicate line
            if (duplicate_count == 0) {
                printf("%s*\n", prefix); // Print "*" only once
            }
            duplicate_count++;
            continue;
        }

        // Print the current line
        printf("%s%08zx: ", prefix, i);  // Print the offset

        for (size_t j = 0; j < HEXDUMP_WIDTH; j++) {
            if (HEXDUMP_WIDTH > 16 && j == 16)
                printf(" ");  // Extra space after 16 bytes for better readability
                
            if (i + j < buflen)
                printf("%02x ", buf[i + j]);
            else
                printf("   ");  // Fill space if less than HEXDUMP_WIDTH bytes in a line
        }

        printf(" ");

        for (size_t j = 0; j < HEXDUMP_WIDTH; j++) {
            if (i + j < buflen)
                printf("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
        }

        printf("\n");

        // Update the last line and reset the duplicate count
        memcpy(last_line, current_line, HEXDUMP_WIDTH);
        last_line_valid = 1;
        duplicate_count = 0;
    }
}
