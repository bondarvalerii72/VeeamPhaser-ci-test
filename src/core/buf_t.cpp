/**
 * @file buf_t.cpp
 * @brief Implementation of buf_t utility methods for buffer operations.
 *
 * This file provides optimized buffer utility functions, particularly for checking
 * if a buffer contains all zeros. The implementation uses aligned 64-bit reads
 * for performance when possible.
 */

#include "buf_t.hpp"

/**
 * @brief Checks if the entire buffer contains only zero bytes.
 *
 * This function performs an optimized check by:
 * 1. Aligning to uint64_t boundary for faster access
 * 2. Processing 8 bytes at a time using uint64_t reads
 * 3. Checking remaining tail bytes individually
 *
 * @return True if all bytes in the buffer are zero, false otherwise.
 */
bool buf_t::is_all_zero() const {
    size_t size = this->size();
    if (size == 0)
        return true;

    const uint8_t* p = static_cast<const uint8_t*>(data());
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    size_t i = 0;

    // Align to uint64_t boundary if not already aligned
    size_t align_bytes = sizeof(uint64_t) - (addr % sizeof(uint64_t));
    if (align_bytes != sizeof(uint64_t) && align_bytes <= size) {
        for (; i < align_bytes; ++i) {
            if (p[i] != 0) return false;
        }
    }

    // Process 8 bytes at a time
    const uint64_t* p64 = reinterpret_cast<const uint64_t*>(p + i);
    size_t remaining = size - i;
    size_t chunks64 = remaining / sizeof(uint64_t);
    for (size_t j = 0; j < chunks64; ++j) {
        if (p64[j] != 0) return false;
    }

    // Check remaining tail bytes
    size_t tail_index = i + chunks64 * sizeof(uint64_t);
    for (size_t j = tail_index; j < size; ++j) {
        if (p[j] != 0) return false;
    }

    return true;
}
