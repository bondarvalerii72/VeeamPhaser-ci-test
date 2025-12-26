#pragma once
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/buf_t.hpp"

// an universal reader class, which can read from:
//  - file
//  - linux device (/dev/sdX)
//  - windows device (\\.\PhysicalDriveX) - sector alignment is done transparently
//
//  + also supports huge reads (>4GB) on windows
//
//  XXX seek() is not supported because sector alignment would be way too complex then
class Reader {
    public:
    Reader(const std::filesystem::path& fname);
    ~Reader();

    class ReadError : public std::runtime_error {
        public:
        explicit ReadError(const std::string& msg) : std::runtime_error(msg) {}
    };

    // either succeeds or throws an exception
    size_t read_at(off_t offset, void* buf, size_t count);
    size_t read_at(off_t offset, buf_t& buf) {
        return read_at(offset, buf.data(), buf.size());
    }

    // get size of a regular file/device
    size_t size() const { return m_size; }

    size_t get_align() const { return m_align; }

    // get size of file/*nix device/win device
    static size_t get_size(const std::filesystem::path& fname);

    private :
    ssize_t locked_read_at(void* buf, size_t count, off_t offset);

        std::filesystem::path m_fname;
        int m_fd = -1;
        size_t m_align = 0;
        size_t m_size = 0;
        std::mutex m_mutex;
};
