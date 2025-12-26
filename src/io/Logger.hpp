#pragma once
#include <unordered_set>
#include <mutex>
#include <memory>
#include <filesystem>
#include "utils/to_hexdump.hpp" // includes <spdlog/spdlog.h>

// hash function for fmt::string_view<char> to use in unordered_set
namespace std {
template <>
    struct hash<fmt::basic_string_view<char>> {
        size_t operator()(const fmt::basic_string_view<char>& s) const noexcept {
            return std::hash<std::string_view>{}(std::string_view(s.data(), s.size()));
        }
    };
}

class Logger {
public:
    using level = spdlog::level::level_enum;

    class ConsoleLevelGuard {
        Logger& logger;
        spdlog::level::level_enum prev;

        public:
        ConsoleLevelGuard(Logger& logger, spdlog::level::level_enum new_level)
            : logger(logger), prev(logger.console_level()) {
                logger.set_console_level(new_level);
            }

        ~ConsoleLevelGuard() {
            logger.set_console_level(prev);
        }
    };

    // Constructor: accepts a shared pointer to an spdlog logger
    explicit Logger(std::shared_ptr<spdlog::logger> logger)
        : m_logger(std::move(logger)) {}

    void set_verbosity(int verbosity);
    void set_banner(const std::string banner){ m_banner = banner; }
    void set_arguments(int argc, char* argv[]);
    void set_arguments(const std::vector<std::string>&);
    void set_dedup_limit(int limit){ m_dedup_limit = limit; }

    template <typename... Args>
    inline void log(spdlog::level::level_enum lvl, fmt::format_string<Args...> format, Args &&... args) {
        m_logger->log(lvl, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void trace(fmt::format_string<Args...> format, Args&&... args) {
        m_logger->trace(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void debug(fmt::format_string<Args...> format, Args&&... args) {
        m_logger->debug(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void info(fmt::format_string<Args...> format, Args&&... args) {
        m_logger->info(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void warn(fmt::format_string<Args...> format, Args&&... args) {
        if (m_dedup_limit > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);  // Ensure thread safety

            int n = m_logged_messages[format]++; // do not count the arguments, just the format string
            if (n >= m_dedup_limit) {
                if (n == m_dedup_limit) {
                    std::string message = fmt::format(format, std::forward<Args>(args)...);
                    m_logger->warn("{} [repeated {} times. suppressing]", message, m_dedup_limit);
                }
                return;
            }
        }
        m_logger->warn(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void error(fmt::format_string<Args...> format, Args&&... args) {
        if (m_dedup_limit > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);  // Ensure thread safety

            int n = m_logged_messages[format]++; // do not count the arguments, just the format string
            if (n >= m_dedup_limit) {
                if (n == m_dedup_limit) {
                    std::string message = fmt::format(format, std::forward<Args>(args)...);
                    m_logger->error("{} [repeated {} times. suppressing]", message, m_dedup_limit);
                }
                return;
            }
        }
        m_logger->error(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void critical(fmt::format_string<Args...> format, Args&&... args) {
        m_logger->critical(format, std::forward<Args>(args)...);
    }

    // Custom warn_once method
    template <typename... Args>
    void warn_once(fmt::format_string<Args...> format, Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mtx);  // Ensure thread safety

        if (m_logged_messages.find(format) == m_logged_messages.end()) {
            m_logger->warn(format, std::forward<Args>(args)...);  // Log the warning
            m_logged_messages[format]++;
        }
    }

    // add a second output stream to the logger
    bool add_file(const std::filesystem::path& fname);

    // show the banner and arguments
    void start();

    // get/set console level
    spdlog::level::level_enum console_level() const;
    void set_console_level(spdlog::level::level_enum level);
    void with_console_level(spdlog::level::level_enum level, std::function<void()> func){
        ConsoleLevelGuard guard(*this, level);
        func(); // exception safe — restore runs in all cases
                // catch + rethrow breaks stack straces, RAII is better
    }

    // Log a message to the file only
    // XXX may cause skipping of messages if multiple threads are logging and throughput is high
    template <typename... Args>
    void file_only(spdlog::level::level_enum level, fmt::format_string<Args...> format, Args&&... args) {
        spdlog::level::level_enum prev_level = console_level();
        set_console_level(static_cast<spdlog::level::level_enum>(level+1));
        m_logger->log(level, format, std::forward<Args>(args)...);
        set_console_level(prev_level);
    }

private:
    std::shared_ptr<spdlog::logger> m_logger;                  // Wrapped spdlog logger
    std::unordered_map<fmt::string_view, int> m_logged_messages;
    mutable std::mutex m_mtx;                                  // Mutex for thread safety
    std::string m_banner;
    std::filesystem::path m_fname;
    std::vector<std::string> m_arguments;
    int m_dedup_limit = 0;
};

// Custom formatter for std::filesystem::path, which is not supported by spdlog by default
template <>
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(path.string(), ctx);
    }
};
