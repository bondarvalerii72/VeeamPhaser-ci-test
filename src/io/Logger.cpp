/**
 * @file Logger.cpp
 * @brief Implementation of the Logger wrapper around spdlog.
 *
 * This file provides a comprehensive logging system with support for multiple
 * output sinks (console and file), verbosity levels, argument tracking, and
 * deduplication. It wraps spdlog to provide application-specific logging features.
 */

#include "Logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/bundled/ranges.h> // for fmt::join()
#include <fstream>

/**
 * @brief Sets the logging verbosity level.
 *
 * Maps integer verbosity to spdlog levels:
 * -4 or less: off, -3: critical, -2: error, -1: warn, 0: info, 1: debug, 2+: trace
 *
 * @param verbosity Integer verbosity level.
 */
void Logger::set_verbosity(int verbosity){
    if( verbosity <= -4 ){
        m_logger->set_level(spdlog::level::off);
        return;
    }
    switch( verbosity ){
        case -3:
            m_logger->set_level(spdlog::level::critical);
            break;
        case -2:
            m_logger->set_level(spdlog::level::err);
            break;
        case -1:
            m_logger->set_level(spdlog::level::warn);
            break;
        case 0: // default level
            m_logger->set_level(spdlog::level::info);
            break;
        case 1:
            m_logger->set_level(spdlog::level::debug);
            break;
        default:
            m_logger->set_level(spdlog::level::trace);
            break;
    }
}

/**
 * @brief Stores command-line arguments for logging.
 * @param argc Argument count.
 * @param argv Argument vector.
 */
void Logger::set_arguments(int argc, char* argv[]){
    m_arguments.clear();
    for( int i = 0; i < argc; ++i ){
        m_arguments.push_back(argv[i]);
    }
}

/**
 * @brief Stores command-line arguments from a vector.
 * @param args Vector of argument strings.
 */
void Logger::set_arguments(const std::vector<std::string>& args){
    m_arguments = args;
}

/**
 * @brief Quotes an argument string if it contains spaces.
 * @note Not a comprehensive shell-escape function.
 * @param arg Argument string to potentially quote.
 * @return Quoted string if it contains spaces, original string otherwise.
 */
std::string quote_if_needed(const std::string& arg) {
    if (arg.find(' ') != std::string::npos) {
        return "\"" + arg + "\"";
    }
    return arg;
}

/**
 * @brief Quotes all arguments in a vector that contain spaces.
 * @param args Vector of argument strings.
 * @return Vector of quoted argument strings.
 */
std::vector<std::string> quote_if_needed(const std::vector<std::string>& args) {
    std::vector<std::string> quoted_args;
    quoted_args.reserve(args.size());
    for (const auto& arg : args) {
        quoted_args.push_back(quote_if_needed(arg));
    }
    return quoted_args;
}

/**
 * @brief Adds a file sink to the logger.
 *
 * Creates a file sink for logging with DEBUG or higher level. If a file is already
 * added, this operation is ignored. The file is opened in append mode.
 *
 * @param fname Path to the log file.
 * @return True if file sink was added, false if already logging to a file or on error.
 */
bool Logger::add_file(const std::filesystem::path& fname) {
    if( !m_fname.empty() ){
        // already logging to a file, ignore
        return false;
    }

    // try to open file for appending
    std::ofstream file(fname, std::ios::app);
    if( !file.is_open() ){
        m_logger->error("Failed to open log file {}, no log will be saved!", fname);
        return false;
    }
    if( file.tellp() != 0 ){
        file.write("\n\n", 2); // visual sessions separator
    }
    file.close();

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fname.string());
    
    // if current logger level is DEBUG or TRACE => file level just inherits it
    // otherwise, file level is DEBUG
    if( m_logger->level() != spdlog::level::debug && m_logger->level() != spdlog::level::trace ){
        file_sink->set_level(spdlog::level::debug);
        set_console_level(m_logger->level()); // move current level to console sink
        m_logger->set_level(spdlog::level::debug);
    }

    m_logger->sinks().push_back(file_sink);
    m_fname = fname;
    return true;
}

/**
 * @brief Logs session start information including banner and arguments.
 */
void Logger::start(){
    if( !m_banner.empty() ){
        m_logger->info("==============================================================");
        m_logger->info("{}", m_banner);
        m_logger->info("==============================================================");
    }

    m_logger->info("started as {}", fmt::join(quote_if_needed(m_arguments), " "));
    m_logger->info("logging to {}", m_fname.empty() ? "console only" : m_fname);
}

/**
 * @brief Sets the console sink logging level independently from file.
 * @param level The spdlog logging level.
 */
void Logger::set_console_level(spdlog::level::level_enum level) {
    m_logger->sinks().front()->set_level(level); // XXX assuming that first sink is console
}

/**
 * @brief Gets the current console sink logging level.
 * @return The current console logging level.
 */
spdlog::level::level_enum Logger::console_level() const {
    return m_logger->sinks().front()->level();
}
