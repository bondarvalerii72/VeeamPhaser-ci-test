/**
 * @file Writer.cpp
 * @brief Implementation of cross-platform file writer with sparse file support.
 *
 * This file provides unified file writing across Windows and Unix platforms.
 * On Windows, it automatically enables sparse file support to efficiently handle
 * files with large zero regions. Handles large writes by chunking and provides
 * positioned write operations.
 */

#include "Writer.hpp"
#include "utils/common.hpp"
#include <spdlog/fmt/bundled/core.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef __WIN32__
#include <windows.h>
#include <winioctl.h>

// XXX expects that filename is already sanitized
Writer::Writer(const std::filesystem::path& fname, bool truncate) {
    HANDLE hFile = CreateFileW(
        fname.c_str(), // returns const wchar_t* on windows
        GENERIC_WRITE, 
        0, 
        NULL, 
        truncate ? CREATE_ALWAYS : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(fmt::format("Writer: CreateFile(\"{}\": {}", fname.string(), fmtLastError()));
    }

    DWORD bytesReturned = 0;
    DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL);

    m_fd = _open_osfhandle((intptr_t)hFile, _O_WRONLY);
    if( m_fd == -1 ){
        throw std::runtime_error(fmt::format("Writer: _open_osfhandle: {}", strerror(errno)));
    }
}

#else

/**
 * @brief Constructs a Writer for the specified file (Unix implementation).
 *
 * Creates or opens a file for writing with appropriate permissions.
 *
 * @param fname Path to file to create/open.
 * @param truncate If true, truncates existing file; if false, opens for append.
 * @throws std::runtime_error If file cannot be created/opened.
 */
Writer::Writer(const std::filesystem::path& fname, bool truncate) {
    int mode = O_WRONLY | O_CREAT | (truncate ? O_TRUNC : 0);
    m_fd = open(fname.c_str(), mode, 0644);
    if (m_fd == -1) {
        throw std::runtime_error(fmt::format("Writer: open(\"{}\", {:#x}, 0644): {}", fname.string(), mode, strerror(errno)));
    }
}

#endif

/**
 * @brief Seeks to a position in the file.
 *
 * @param offset Offset to seek to.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @throws std::runtime_error On lseek error.
 */
void Writer::seek(off_t offset, int whence) const {
    if (lseek(m_fd, offset, whence) == -1) {
        throw std::runtime_error(fmt::format("Writer: lseek({:#x}, {:#x}, {}): {}", m_fd, offset, whence, strerror(errno)));
    }
}

/**
 * @brief Gets the current file position.
 * @return Current file offset.
 * @throws std::runtime_error On lseek error.
 */
off_t Writer::tell() const {
    off_t offset = lseek(m_fd, 0, SEEK_CUR);
    if (offset == -1) {
        throw std::runtime_error(fmt::format("Writer: lseek({:#x}, 0, SEEK_CUR): {}", m_fd, strerror(errno)));
    }
    return offset;
}

/**
 * @brief Writes data at a specific file position.
 *
 * @param offset File position to write at.
 * @param buf Buffer containing data to write.
 * @param count Number of bytes to write.
 * @throws std::runtime_error On seek or write error.
 */
void Writer::write_at(off_t offset, const void* buf, size_t count) const {
    seek(offset, SEEK_SET);
    write(buf, count);
}

/**
 * @brief Writes data to the file at the current position.
 *
 * Handles large writes by chunking into 1GB pieces. Automatically retries
 * on EINTR signal interruption.
 *
 * @param buf Buffer containing data to write.
 * @param count Number of bytes to write.
 * @throws std::runtime_error On write error or if write returns 0 bytes.
 */
void Writer::write(const void* buf, size_t count) const {
    constexpr size_t CHUNK_SIZE = 1ULL << 30; // 1 GB
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = count;

    while (remaining > 0) {
        size_t to_write = std::min(remaining, CHUNK_SIZE);
        const char* chunk_ptr = ptr;
        size_t chunk_remaining = to_write;

        while (chunk_remaining > 0) {
            ssize_t nwritten = ::write(m_fd, chunk_ptr, chunk_remaining);
            if (nwritten == -1) {
                if (errno == EINTR) {
                    continue; // Retry on signal interruption
                }
                throw std::runtime_error(fmt::format(
                    "Writer: write({:#x}, {}, {}): {}", m_fd, static_cast<const void*>(chunk_ptr), chunk_remaining, strerror(errno)));
            }
            if (nwritten == 0) {
                throw std::runtime_error(fmt::format(
                    "Writer: write({:#x}, {}, {}): write returned 0 bytes", m_fd, static_cast<const void*>(chunk_ptr), chunk_remaining));
            }

            chunk_ptr += nwritten;
            chunk_remaining -= nwritten;
        }

        ptr += to_write;
        remaining -= to_write;
    }
}

/**
 * @brief Destructor closes the file descriptor if open.
 */
Writer::~Writer() {
    if( m_fd != -1 ){
        close(m_fd);
        m_fd = -1;
    }
}
