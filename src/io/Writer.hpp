#pragma once
#include <filesystem>

// mostly for writing sparse files transparently on windows
// also writes huge files transparently, i.e. at least on windows write() fails to write a chunk larger than 4GB
class Writer {
    public:
    Writer(const std::filesystem::path& fname, bool truncate = true);
    ~Writer();

    void seek(off_t offset, int whence = SEEK_SET) const;
    void write(const void* buf, size_t count) const;
    void write_at(off_t offset, const void* buf, size_t count) const;
    off_t tell() const;

    private:
    int m_fd = -1;
};
