/**
 * @file Progress.cpp
 * @brief Implementation of progress indicator for long-running operations.
 *
 * This file provides a progress bar/spinner that displays current position,
 * completion percentage, processing speed, estimated time remaining, and
 * counts of found items. The spinner uses Unicode characters on Unix and
 * ASCII characters on Windows for compatibility.
 */

#include "Progress.hpp"
#include "common.hpp"

#ifdef _WIN32
static constexpr std::array<std::string_view, 4> SPINNER = {
    "|", "/", "-", "\\"
};
#else
static constexpr std::array<std::string_view, 10> SPINNER = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
};
#endif

/**
 * @brief Updates and displays the progress indicator.
 *
 * Updates the progress display with current position, percentage, speed, ETA,
 * and found items count. Throttles updates to approximately 10Hz unless final
 * is true.
 *
 * @param offset Current file position.
 * @param final If true, forces update and adds newline (for completion).
 */
void Progress::update(off_t offset, bool final){
    struct timespec cur_time;
    clock_gettime(CLOCK_MONOTONIC, &cur_time);

    uint64_t dt = (cur_time.tv_sec - m_prev_time.tv_sec) * 1000000000L + (cur_time.tv_nsec - m_prev_time.tv_nsec);
    if( dt < 100000000 && !final ){
        return;
    }

    dt = (cur_time.tv_sec - m_start_time.tv_sec) * 1000000000L + (cur_time.tv_nsec - m_start_time.tv_nsec);

    std::string eta;
    m_prev_time = cur_time;
    dt /= 1000000000; // elapsed time in seconds
    if( dt == 0 ) dt = 1;

    uint64_t remaining_work = m_fsize - offset;
    uint64_t speed_of_progress = (offset-m_start_offset) / dt; // This is already calculated
    if(speed_of_progress > 0) { // Ensure speed is not 0 to avoid division by zero
        uint64_t remaining_time = remaining_work / speed_of_progress; // Calculate remaining time
        eta = seconds2human(remaining_time, 1);
    } else {
        eta = "?";
    }

    std::string found_str;
    for(const auto& [key, count] : m_found_map) {
        if(!found_str.empty()) found_str += ", ";
        found_str += fmt::format("{} {}", count, key);
    }

    if (found_str.empty()) {
        found_str = "-";
    }

    fmt::print("[{}] {:012x}/{} = {:.1f}%, {}Mb/s, eta: {}, found: {}" ANSI_CLEAR_EOL "{}",
        SPINNER[m_spinner_idx++],
        offset,
        seconds2human(dt),
        100.0*offset/m_fsize,
        (offset-m_start_offset)/dt/1024/1024,
        eta,
        found_str,
        final ? "\n" : "\r"
        );
    fflush(stdout);

    if (m_spinner_idx >= SPINNER.size()) {
        m_spinner_idx = 0;
    }
}
