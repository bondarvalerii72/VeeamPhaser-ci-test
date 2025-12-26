#include <time.h>
#include <cstdint>
#include <sys/types.h>
#include <unordered_map>

class Progress {
    public:
    Progress(size_t fsize, off_t start_offset = 0) : m_fsize(fsize), m_start_offset(start_offset) {
        clock_gettime(CLOCK_MONOTONIC, &m_start_time);
        m_prev_time = m_start_time;
    }
    void update(off_t offset, bool final = false);
    void found(const char* key = "results") { m_found_map[key]++; }
    void finish() { update(m_fsize, true); }

    private:
    timespec m_start_time, m_prev_time;
    size_t m_fsize;
    off_t m_start_offset;
    std::unordered_map<const char*, size_t> m_found_map; // XXX not owning the key!
    size_t m_spinner_idx = 0;
};
