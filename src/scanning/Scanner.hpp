#include <string>
#include <cstdint>

class Scanner {
    public:
    Scanner(const std::string fname, uint64_t start_offset) : m_fname(fname), m_start_offset(start_offset) {}
    void scan();

    private:
    std::string m_fname;
    off_t m_start_offset;
    size_t m_fsize;
};
