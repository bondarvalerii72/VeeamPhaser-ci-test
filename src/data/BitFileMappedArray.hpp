#include <iostream>
#include <filesystem>
#include <fstream>
#include <mio/mmap.hpp>
#include <cstdint>

class BitFileMappedArray {
    size_t m_bit_size;
    mio::mmap_sink m_mmap;

    static std::filesystem::path ensure_file_size(const std::filesystem::path& fname, std::size_t size_bytes) {
        if (!std::filesystem::exists(fname)) {
            std::ofstream(fname).close(); // create file if missing
            std::filesystem::resize_file(fname, size_bytes);
        } else if (std::filesystem::file_size(fname) != size_bytes) {
            throw std::runtime_error("File size does not match expected size");
        }
        return fname;
    }

public:
    BitFileMappedArray(const std::filesystem::path& fname, size_t bits) :
            m_bit_size(bits),
            m_mmap(ensure_file_size(fname, size_bytes()).native())
    {
    }

    bool get(size_t index) const {
        if (index >= m_bit_size) throw std::out_of_range("Bit index out of range");
        size_t byte = index / 8;
        size_t bit = 7 - index % 8;
        return m_mmap[byte] & (1 << bit);
    }

    void set(size_t index, bool value) {
        if (index >= m_bit_size) throw std::out_of_range("Bit index out of range");
        size_t byte = index / 8;
        size_t bit = 7 - index % 8;
        if (value)
            m_mmap[byte] |= (1 << bit);
        else
            m_mmap[byte] &= ~(1 << bit);
    }

    // sets all bits in range to 1
    void set_range(size_t start, size_t end) {
        if (start >= m_bit_size || end > m_bit_size || start >= end)
            throw std::out_of_range("Invalid bit range: " + std::to_string(start) + " to " + std::to_string(end) + 
                                     " (size: " + std::to_string(m_bit_size) + ")");

        size_t start_byte = start / 8;
        size_t end_byte = (end - 1) / 8;

        size_t start_bit = 7 - start % 8;
        size_t end_bit = 7 - ((end - 1) % 8);

        uint8_t* ptr = data();

        // --- Case 1: Same byte
        if (start_byte == end_byte) {
            uint8_t mask = ((1u << (end_bit - start_bit + 1)) - 1) << start_bit;
            ptr[start_byte] |= mask;
            return;
        }

        // --- Case 2: First (partial) byte
        if (start_bit != 0) {
            uint8_t mask = 0xFFu << start_bit;
            ptr[start_byte++] |= mask;
        }

        // --- Case 3: Full middle bytes
        for (size_t i = start_byte; i < end_byte; ++i)
            ptr[i] = 0xFF;

        // --- Case 4: Last (partial) byte
        if ((end_bit + 1) != 0) {
            uint8_t mask = (1u << (end_bit + 1)) - 1;
            ptr[end_byte] |= mask;
        }
    }

    uint8_t* data() { return reinterpret_cast<uint8_t*>(m_mmap.data()); }
    const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(m_mmap.data()); }

    size_t size_bits() const { return m_bit_size; }
    size_t size_bytes() const { return (m_bit_size+7) / 8; }

    // void flush() { m_mmap.sync(mio::sync_type::sync); }
};

