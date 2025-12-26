#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <fstream>

#include "utils/common.hpp"
#include "utils/Progress.hpp"
#include "io/Reader.hpp"
#include "io/Writer.hpp"

class DblBufScanner {
    public:

    explicit DblBufScanner(const std::string& fname, off_t start = 0, size_t block_size = 8*1024*1024) :
            m_fname(fname), m_block_size(block_size), m_start(start), m_reader(fname), m_progress(m_reader.size(), start) {
        m_buffers[0].resize(m_block_size);
        m_buffers[1].resize(m_block_size);
    }

    void scan() {
        start();
        join();
        finish();
    }

    protected:
    void virtual start() {
        m_read_thread = std::thread(&DblBufScanner::read_thr_proc, this);
        m_scan_thread = std::thread(&DblBufScanner::scan_thr_proc, this);
    }

    private:
    void join() {
        if (m_read_thread.joinable()) m_read_thread.join();
        if (m_scan_thread.joinable()) m_scan_thread.join();
    }

    protected:
    const std::string m_fname;
    const size_t m_block_size;
    const off_t m_start;

    private:
    buf_t m_buffers[2];
    bool m_buf_ready[2] = {false, false};
    off_t m_offsets[2];

    std::mutex m_buffer_mutex;
    std::condition_variable m_buffer_cv;

    std::atomic<bool> m_done = false;

    protected:
    Reader m_reader;

    private:
    Progress m_progress;

    std::thread m_read_thread;
    std::thread m_scan_thread;

    size_t try_read_by_sector(off_t pos, uint8_t* buf, size_t size) {
        size_t sector_size = m_reader.get_align();
        if (sector_size <= 1) {
            sector_size = 512;
        }

        memset(buf, 0, size);
        size_t nread = 0;
        while(nread < size) {
            try {
                size_t nr = m_reader.read_at(pos + nread, buf + nread, std::min(size - nread, sector_size));
                if (nr == 0)
                    break;
                nread += nr;
            } catch (const Reader::ReadError& e) {
                logger->trace("{:#x}: {}", pos + nread, e.what());
                nread += sector_size; // skip sector
            }
        }
        return nread;
    }

    void read_thr_proc() {
        off_t pos = m_start;
        int buf_idx = 0;
        while (pos < (ssize_t)m_reader.size()){
            { // a block for a lock
                std::unique_lock<std::mutex> lock(m_buffer_mutex);
                m_buffer_cv.wait(lock, [&] { return !m_buf_ready[buf_idx]; });
                m_progress.update(pos);

                if( m_buffers[buf_idx].size() < m_block_size ) {
                    m_buffers[buf_idx].resize(m_block_size);
                }

                size_t nread = 0;
                try {
                    nread = m_reader.read_at(pos, m_buffers[buf_idx].data(), m_block_size);
                } catch (const Reader::ReadError& e) {
                    extern bool g_force;
                    logger->error("{} @ {:#x}: {}", m_fname, pos, e.what());
                    if (g_force)
                        nread = try_read_by_sector(pos, m_buffers[buf_idx].data(), m_block_size);
                    else
                        throw;
                }

                if (nread == 0) {
                    logger->error("{}: unexpected EOF at {:#x}", m_fname, pos);
                    break;
                }

                if( nread < m_buffers[buf_idx].size() ) {
                    m_buffers[buf_idx].resize(nread);
                }
                m_offsets[buf_idx] = pos;
                m_buf_ready[buf_idx] = true;
                pos += nread;
            }

            m_buffer_cv.notify_one();
            buf_idx = 1 - buf_idx;
        }
        m_done = true;
        m_buffer_cv.notify_all();
    }

    void virtual process_buf(const buf_t& buf, off_t offset) = 0;

    void scan_thr_proc() {
        int buf_idx = 0;
        while (!m_done || m_buf_ready[buf_idx]) {
            std::unique_lock<std::mutex> lock(m_buffer_mutex);
            m_buffer_cv.wait(lock, [&] { return m_buf_ready[buf_idx] || m_done; });

            if (m_buf_ready[buf_idx]) {
                process_buf(m_buffers[buf_idx], m_offsets[buf_idx]);
                m_buf_ready[buf_idx] = false; // Mark as processed
            }

            m_buffer_cv.notify_one();
            buf_idx = 1 - buf_idx;
        }
    }

    protected:
    void virtual finish() {
        m_progress.finish();
    }

    void found(const char* key){
        m_progress.found(key);
    }

    void save_file(const std::string& fname, off_t start_offset, size_t size) {
        std::filesystem::path out_fname = get_out_pathname(m_fname, fname);
        logger->trace("saving {}", out_fname);

        Writer w(out_fname);
        buf_t buf(m_block_size);

        off_t pos = start_offset;
        for(size_t remain = size; remain > 0; ){
            ssize_t nread = m_reader.read_at(pos, buf.data(), std::min(remain, m_block_size));
            if (nread == 0) {
                logger->error("save_file(\"{}\", {:#x}, {:#x}): unexpected EOF at {:#x}", fname, start_offset, size, pos);
                break;
            }
            w.write(buf.data(), nread);
            pos += nread;
            remain -= nread;
        }
    }

    // m_reader.read_at(start_offset, size) => write to "<fname_ofs>.<ext>" @ dst_offset
    void update_file(off_t fname_ofs, const char* ext, off_t dst_offset, off_t start_offset, size_t size) {
        std::filesystem::path out_fname = get_out_pathname(m_fname, fmt::format("{:012x}{}", fname_ofs, ext));
        logger->trace("updating {} @ {:x}", out_fname, dst_offset);

        Writer w(out_fname, false);
        w.seek(dst_offset);

        buf_t buf(m_block_size);

        off_t pos = start_offset;
        for(size_t remain = size; remain > 0; ){
            ssize_t nread = m_reader.read_at(pos, buf.data(), std::min(remain, m_block_size));
            if (nread == 0) {
                logger->error("update_file({:#x}, \"{}\", {:#x}, {:#x}): unexpected EOF at {:#x}", fname_ofs, ext, start_offset, size, start_offset);
                break;
            }
            w.write(buf.data(), nread);
            pos += nread;
            remain -= nread;
        }
    }
};

