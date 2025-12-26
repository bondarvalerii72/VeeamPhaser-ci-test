/**
 * @file Reader.cpp
 * @brief Implementation of cross-platform file and device reader.
 *
 * This file provides unified file and block device reading across Windows, Linux,
 * and macOS. It handles platform-specific device size detection, sector alignment
 * for raw device access, and thread-safe reads. Supports both regular files and
 * block devices with automatic size detection and alignment handling.
 */

#include "Reader.hpp"
#include "utils/common.hpp"
#include <spdlog/fmt/bundled/core.h>

#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/fs.h>
#elif __APPLE__
#include <sys/disk.h>
#elif __WIN32__
#include <windows.h>
#include <winioctl.h>

static size_t get_dev_size(const std::filesystem::path& fname) {
    HANDLE hFile = CreateFileW(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(fmt::format("CreateFile(\"{}\"): {}", fname, fmtLastError()));
    }

    DWORD bytesReturned;
    DISK_GEOMETRY_EX dgEx;
    BOOL success = DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &dgEx, sizeof(dgEx), &bytesReturned, NULL);
    CloseHandle(hFile);

    if (!success) {
        throw std::runtime_error(fmt::format("DeviceIoControl(IOCTL_DISK_GET_DRIVE_GEOMETRY_EX): {}", fmtLastError()));
    }

    return dgEx.DiskSize.QuadPart;
}

static size_t get_dev_align(const std::filesystem::path& fname){
    HANDLE hFile = CreateFileW(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(fmt::format("CreateFile(\"{}\"): {}", fname, GetLastError()));
    }

    DWORD bytesReturned;
    DISK_GEOMETRY dg;
    BOOL success = DeviceIoControl(hFile, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &bytesReturned, NULL);
    CloseHandle(hFile);

    if (!success) {
        throw std::runtime_error(fmt::format("DeviceIoControl(IOCTL_DISK_GET_DRIVE_GEOMETRY): {}", GetLastError()));
    }

    return dg.BytesPerSector;
}
#endif

size_t Reader::get_size(const std::filesystem::path& fname) {
    size_t size = 0;
    struct stat st;
    if( stat(fname.string().c_str(), &st) == -1 ) {
#ifdef __WIN32__
        return get_dev_size(fname);
#else
        throw std::runtime_error(fmt::format("fstat(\"{}\"): {}", fname, strerror(errno)));
#endif
    }

    if (S_ISREG(st.st_mode)) {
        // regular file
        size = st.st_size;
    } else if (S_ISBLK(st.st_mode)) {
        // block device
        int fd = open(fname.string().c_str(), O_RDONLY);
        if (fd == -1) {
            throw std::runtime_error(fmt::format("open(\"{}\"): {}", fname.string(), strerror(errno)));
        }
#ifdef __linux__
        // Use BLKGETSIZE64 for Linux
        if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
            close(fd);
            throw std::runtime_error(fmt::format("ioctl({:x}, BLKGETSIZE64, {}): {}", fd, (void*)&size, strerror(errno)));
        }
#elif __APPLE__
        // Use DIOCGMEDIASIZE for macOS
        if (ioctl(fd, DKIOCGETBLOCKSIZE, &size) == -1) {
            close(fd);
            throw std::runtime_error(fmt::format("ioctl({:x}, DKIOCGETBLOCKSIZE, {}): {}", fd, (void*)&size, strerror(errno)));
        }

        uint64_t blockCount = 0;
        if (ioctl(fd, DKIOCGETBLOCKCOUNT, &blockCount) == -1) {
            close(fd);
            throw std::runtime_error(fmt::format("ioctl({:x}, DKIOCGETBLOCKCOUNT, {}): {}", fd, (void*)&blockCount, strerror(errno)));
        }
        size *= blockCount; // Total size = block size * block count
#endif
        close(fd);
    }
    return size;
}

#ifdef O_BINARY
#define OPEN_MODE O_RDONLY|O_BINARY
#else
#define OPEN_MODE O_RDONLY
#endif

/**
 * @brief Constructs a Reader for the specified file or device.
 *
 * Opens the file/device for reading and determines its size. For Windows block
 * devices (paths starting with "\\\\.\\"), also determines sector alignment requirements.
 *
 * @param fname Path to file or device to open.
 * @throws std::runtime_error If file cannot be opened or size cannot be determined.
 */
Reader::Reader(const std::filesystem::path& fname) : m_fname(fname) {
    m_fd = open(fname.string().c_str(), OPEN_MODE);
    if( m_fd == -1 ) {
        throw std::runtime_error(fmt::format("open(\"{}\", {:#x}): {}", fname, OPEN_MODE, strerror(errno)));
    }
    m_size = get_size(fname);
#if __WIN32__
    if( fname.native().substr(0, 4) == L"\\\\.\\" ) {
        m_align = get_dev_align(fname);
        logger->debug("Device: {}, size: {}, align: {}", fname, m_size, m_align);
    }
#endif
}

/**
 * @brief Destructor closes the file descriptor if open.
 */
Reader::~Reader() {
    if( m_fd != -1 ) {
        close(m_fd);
    }
}

/**
 * @brief Thread-safe positioned read for platforms without pread().
 *
 * Used on Windows/MinGW where pread() is not available. Uses lseek+read
 * with mutex protection for thread safety. Handles large reads by chunking
 * to avoid 32-bit limitations.
 *
 * @param buf Buffer to read into.
 * @param count Number of bytes to read.
 * @param offset File position to read from.
 * @return Number of bytes actually read.
 * @throws std::runtime_error On lseek error.
 * @throws ReadError On read error.
 */
ssize_t Reader::locked_read_at(void* buf, size_t count, off_t offset) {
    static const size_t chunk_size = (1ULL << 30); // 1 GB

    std::lock_guard<std::mutex> lock(m_mutex);
    if (lseek(m_fd, offset, SEEK_SET) == -1) {
        throw std::runtime_error(fmt::format("lseek({:#x}, {:#x}, SEEK_SET): {}", m_fd, offset, strerror(errno)));
    }

    char* out = static_cast<char*>(buf);
    size_t total_read = 0;

    while (total_read < count) {
        size_t to_read = std::min(chunk_size, count - total_read);
        ssize_t nread = ::read(m_fd, out + total_read, to_read);

        if (nread == -1) {
            if (errno == EINTR) continue; // Retry if interrupted
            throw ReadError(fmt::format("read({:#x}, {}, {}): {}", m_fd, static_cast<void*>(out + total_read), to_read, strerror(errno)));
        }

        if (nread == 0) {
            // EOF reached
            break;
        }

        total_read += static_cast<size_t>(nread);
    }

    return static_cast<ssize_t>(total_read);
}

/**
 * @brief Reads data from a specific file/device position (thread-safe).
 *
 * Performs positioned reads that are thread-safe. Automatically handles sector
 * alignment requirements for raw device access on Windows. Uses pread() on
 * Unix platforms and locked_read_at() on Windows.
 *
 * @param offset File position to read from.
 * @param buf Buffer to read into.
 * @param count Number of bytes to read.
 * @return Number of bytes actually read (may be less than count at EOF).
 * @throws std::invalid_argument If offset is negative.
 * @throws ReadError On read error.
 */
size_t Reader::read_at(off_t offset, void* buf, size_t count) {
    if( offset < 0 ){
        throw std::invalid_argument(fmt::format("offset < 0: {:#x}", offset));
    }
    if( (size_t)offset >= m_size ) {
        return 0;
    }

    if( m_align && (count % m_align || offset % m_align) ) {
        std::vector<char> tmp(count + m_align*2);
        size_t shift = offset % m_align;
        size_t nread = read_at(offset - shift, tmp.data(), (count + m_align*2) & ~(m_align-1));
        nread -= shift;
        if( (size_t)nread < count ) {
            count = nread;
        }
        memcpy(buf, tmp.data() + shift, count);
        return count;
    }
#if __WIN32__
    if( m_align ) {
        if( offset + count > m_size ) {
            if( (size_t)offset >= m_size ) {
                return 0;
            } else {
                count = m_size - offset;
            }
        }
    }

    ssize_t nread = locked_read_at(buf, count, offset);
#else
    ssize_t nread = ::pread(m_fd, buf, count, offset);
#endif
    if( nread == -1 ) {
        throw ReadError(fmt::format("read(fd {:#x}, offset {:#x}, count {:#x}): {}", m_fd, offset, count, strerror(errno)));
    }
    return nread;
}
