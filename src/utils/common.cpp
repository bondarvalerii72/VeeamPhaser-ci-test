/**
 * @file common.cpp
 * @brief Implementation of common utilities and global variables.
 *
 * This file provides a collection of utility functions used throughout the application
 * including path manipulation, string formatting, glob matching, file sanitization,
 * crash handling with stack traces, and platform-specific implementations (Windows
 * memmem, error formatting). It also defines global variables for logging, argument
 * parsing, and program configuration.
 */

#include "common.hpp"
#include "../../dist/version.h"

int verbosity = 0;
bool g_force = false;
bool verbosity_changed = false;
int g_hexdump_width = 0; // 0 = auto

std::shared_ptr<Logger> logger = std::make_shared<Logger>(spdlog::get(""));
argparse::ArgumentParser program(APP_NAME, "", argparse::default_arguments::help);

// begin stack trace generation on error
#include <backtrace.h>

/**
 * @brief Backtrace error callback for logging libbacktrace errors.
 * @param msg Error message.
 * @param errnum Error number.
 */
void backtrace_error_cb(void *, const char *msg, int errnum) {
    logger->critical("Error: {} (Error number: {})", msg, errnum);
}

int backtrace_full_cb(void *, uintptr_t pc, const char *filename, int lineno, const char *function) {
    logger->critical("     {} {}:{} ({})", (void *)pc, filename, lineno, function);
    return 0;  // Continue processing the backtrace
}

void signal_handler(int sig) {
    logger->critical("Signal {} received, printing backtrace...", sig);

    // Create the backtrace state (this will handle errors too)
    backtrace_state *state = backtrace_create_state(NULL, 0, backtrace_error_cb, NULL);

    // Capture and print the backtrace (provide the full cb and error cb)
    backtrace_full(state, 0, backtrace_full_cb, backtrace_error_cb, NULL);

    // Exit after handling the signal (to avoid further issues)
    exit(1);  // Exit with failure status
}
// end stack trace generation on error

#ifdef _WIN32
#include <windows.h>

void* memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len) {
    if (needle_len == 0) {
        return (void*)haystack;  // If needle length is 0, return the beginning of haystack
    }
    if (haystack_len < needle_len) {
        return nullptr;
    }

    const char* h = static_cast<const char*>(haystack);
    const char* n = static_cast<const char*>(needle);

    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (std::memcmp(h + i, n, needle_len) == 0) {
            return (void*)(h + i);
        }
    }

    return nullptr;  // Return nullptr if not found
}

std::string fmtLastError() {
    char buf[256];
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, buf, sizeof(buf), NULL);
    return buf;
}
#endif

std::string filter_unprintable(const std::string& str){
    std::string result;
    for( char c : str ){
        if( c >= 0x20 && c <= 0x7e ){
            result += c;
        } else {
            result += '.';
        }
    }
    return result;
}

bool path_ends_with(const fs::path& p, const fs::path::string_type& suffix) {
    if (p.native().size() < suffix.size()) {
        return false;
    }
    return p.native().compare(p.native().size() - suffix.size(), suffix.size(), suffix) == 0;
}

fs::path::string_type str2nstr(const std::string& str) {
#ifdef _WIN32
    fs::path::string_type result(str.size(), 0);
    std::mbstowcs(&result[0], str.c_str(), str.size());
    return result;
#else
    return str;
#endif
}

// %f - filename, %b - basename, %d - dirname, %p - path, %P - pathname, %t - timestamp
fs::path expand_dir_tpl(const std::string& tpl, const fs::path& in_fname){
    fs::path::string_type result;

    for(size_t i=0; i<tpl.size(); i++){
        if( tpl[i] == '%' && i+1 < tpl.size() ){
            i++; // skip '%'
            switch(tpl[i]){
                case 'f': result += in_fname.filename().native(); break;
                case 'b': result += in_fname.stem().native(); break;
                case 'd': result += in_fname.parent_path().filename().native(); break;
                case 'p': result += in_fname.parent_path().native(); break;
                case 'P': result += in_fname.native(); break;
                case 't': {
                    // 20250125_235959
                    char buf[0x20];
                    time_t t = time(nullptr);
                    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", localtime(&t));
                    result += str2nstr(buf);
                    break;
                }
                case '%': result += '%'; break; // escaped '%'
                default:
                          throw std::runtime_error(fmt::format("Invalid template char: {}", tpl[i]));
                          
            }
        } else {
            result += tpl[i];
        }
    }

    return result;
}

#ifdef _WIN32
std::wstring operator"" _n(const char* str, std::size_t len) {
    std::wstring wstr(len, 0);
    std::mbstowcs(&wstr[0], str, len);
    return wstr;
}
#else
std::string operator"" _n(const char* str, std::size_t len) {
    return std::string(str, len);
}
#endif

// get output directory, create it if it doesn't exist
// if --out-dir cmdline option was specified, return its value
// otherwise, if already in ".out" dir, return dirname(in_fname)
// otherwise, if in_fname contains "METADATA" substring, or ends with ".md", return dirname(in_fname)
// otherwise, return in_fname + ".out"
fs::path get_out_dir(fs::path in_fname, const argparse::ArgumentParser& program){
    static const std::vector<fs::path::string_type> exts { ".md"_n, ".bank"_n, ".slot"_n };

    if( in_fname.native().substr(0, 4) == "\\\\.\\"_n){
        // '\\.\PhysicalDrive0' -> 'PhysicalDrive0'
        in_fname = in_fname.native().substr(4);
    }

    if( in_fname.native().substr(0, 5) == "/dev/"_n){
        // '/dev/sda1' -> 'sda1'
        in_fname = in_fname.native().substr(5);
    }

    fs::path dir;
    if( program.present("--out-dir") ){
        dir = program.get<std::string>("--out-dir");
    } else if( program.present("--out-dir-tpl") ){
        dir = expand_dir_tpl(program.get<std::string>("--out-dir-tpl"), in_fname);
    } else if( path_ends_with(in_fname.parent_path(), ".out"_n) ){
        dir = in_fname.parent_path();
    } else if( in_fname.native().find("METADATA"_n) != std::string::npos || std::any_of(exts.begin(), exts.end(), [&in_fname](const auto& ext){ return path_ends_with(in_fname, ext); }) ){
        dir = in_fname.parent_path();
    } else {
        dir = fs::path(in_fname.native() + ".out"_n);
    }
    if( !dir.empty() ){
        fs::create_directories(dir);
    }
    return dir;
}

fs::path get_out_dir(const fs::path& in_fname){
    return get_out_dir(in_fname, program);
}

std::string sanitize_fname(const fs::path& fname) {
    return sanitize_fname(fname.string());
}

std::string sanitize_fname(const std::string& fname) {
    std::string sanitized;
    size_t i = 0;

    while (i < fname.size()) {
        char c = fname[i];

        // Forbidden characters
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c < ' ' || c >= 127) {
            sanitized.push_back('_');
            ++i;
        }
        // Handle consecutive dots / slashes
        else if (c == '.' || c == '/' || c == '\\') {
            size_t start = i;
            while (i < fname.size() && fname[i] == c) {
                ++i;
            }
            size_t count = i - start;
            if (count >= 2) {
                if ( c == '.' )
                    sanitized.append(count, '_'); // replace multiple dots with underscores
                else
                    sanitized.push_back(c); // keep multiple slashes/backslashes as a single one
            } else {
                sanitized.push_back(c);
            }
        }
        // Normal character
        else {
            sanitized.push_back(c);
            ++i;
        }
    }

    return sanitized;
}

// get output pathname for a file, create directories if they don't exist
fs::path get_out_pathname(const fs::path& in_fname, const std::string& out_fname){
    fs::path out_dir = get_out_dir(in_fname);
    fs::path result = out_dir / fs::path(sanitize_fname(out_fname)).relative_path();
    fs::path parent = result.parent_path();
    if( !parent.empty() ){
        // only a case when out_fname also contains a subdir
        fs::create_directories(parent);
    }
    return result;
}

void init_log(const fs::path& src_fname, std::string log_fname){
    static bool inited = false;
    if( inited ){
        return;
    }

    inited = true;
    if( log_fname.empty() ){
        // implicit log pathname, can continue without log
        try {
            logger->add_file(get_out_pathname(src_fname, "phaser.log"));
        } catch( const fs::filesystem_error &e ){
            logger->error("Failed to create log file: {}", e.what());
        }
    } else {
        while(true){
#ifdef __MINGW64__
            if( log_fname == "/dev/null" ){
                break;
            }
#endif

            // explicit log pathname, can't continue without log
            if( !logger->add_file(log_fname) ){
                logger->critical("explicit log pathname is set, refusing to continue without log");
                exit(1);
            }
            break;
        }
    }
    logger->start();
}

bool parse_bool(const std::string value){
    if( value == "1" || value == "true" || value == "yes" ){
        return true;
    }
    if( value == "0" || value == "false" || value == "no" ){
        return false;
    }
    throw std::runtime_error("Invalid boolean value: " + value);
}

void register_common_args(argparse::ArgumentParser &parser) {
    parser.add_argument("-v", "--verbose")
        .help("increase verbosity")
        .action([&](const auto &) { ++verbosity; verbosity_changed = true; })
        .append()
        .implicit_value(true)
        .nargs(0);

    parser.add_argument("-q", "--quiet")
        .help("decrease verbosity")
        .action([&](const auto &) { --verbosity; verbosity_changed = true; })
        .append()
        .implicit_value(true)
        .nargs(0);

    parser.add_argument("-f", "--force")
        .implicit_value(true)
        .store_into(g_force)
        .help("force continue on errors");

    parser.add_argument("-H", "--hexdump-width")
        .help("set hexdump width")
        .store_into(g_hexdump_width);

    parser.add_argument("-L", "--log")
        .help("log pathname [default: out-dir/phaser.log]");
    parser.add_argument("--log-dedup-limit")
        .default_value(100)
        .help("limit duplicate log messages, 0 = no limit");
}

void register_program_args(argparse::ArgumentParser &parser) {
    register_common_args(parser);

    parser.add_argument("--version")
        .action([&](const auto & /*unused*/) {
            fmt::print("{}\n", APP_VERSION);
            exit(0);
        })
        .default_value(false)
        .help("print version information and exit")
        .implicit_value(true)
        .nargs(0);

    auto &group = parser.add_mutually_exclusive_group();
    group.add_argument("-o", "--out-dir")
        .help("output dir [default: input filename + \".out\" unless already in .out dir]");
    group.add_argument("-O", "--out-dir-tpl")
        .help("output dir with template expansion: %f - filename, %b - basename, %d - dirname, %p - path, %P - pathname, %t - timestamp");
}

bool is_glob(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring s = path.native(); // Windows uses wide strings
    return s.find(L'*') != std::wstring::npos || s.find(L'?') != std::wstring::npos;
#else
    const std::string s = path.native(); // Unix-like platforms use UTF-8 strings
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
#endif
}

bool is_glob(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

void glob_recursive(
    const fs::path& real_path,         // actual path on disk
    const fs::path& logical_path,      // how we want to emit it
    const std::vector<fs::path>& pattern_parts,
    size_t index,
    std::vector<fs::path>& out_matches
) {
    if (index == pattern_parts.size())
        return;

    bool last = (index == pattern_parts.size() - 1);
    const auto& segment = pattern_parts[index];

    if (segment == "."_n) {
        // Handle "." by continuing in the same directory
        glob_recursive(real_path, logical_path / "."_n, pattern_parts, index + 1, out_matches);
        return;
    }

    if (segment == ".."_n) {
        // Handle ".." by going up one directory
        if (logical_path.has_parent_path()) {
            glob_recursive(real_path.parent_path(), logical_path.parent_path(), pattern_parts, index + 1, out_matches);
        } else if (real_path.has_parent_path()) {
            glob_recursive(real_path.parent_path(), logical_path / ".."_n, pattern_parts, index + 1, out_matches);
        }
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(real_path, ec)) {
        if (ec) continue;

        auto filename = entry.path().filename();
        if (!simple_glob_match(segment.native(), filename.native()))
            continue;

        fs::path next_real = entry.path();
        fs::path next_logical = logical_path / filename;

        if (last) {
            if (entry.is_regular_file() || entry.is_symlink()) {
                out_matches.push_back(next_logical);
            }
        } else {
            if (entry.is_directory()) {
                glob_recursive(next_real, next_logical, pattern_parts, index + 1, out_matches);
            }
        }
    }
}

std::vector<fs::path> simple_glob_find(const fs::path& pattern) {
    std::vector<std::filesystem::path> parts {pattern.begin(), pattern.end()};
    std::vector<fs::path> results;

    // Determine prefix (fixed part with no globs)
    fs::path base_real = fs::current_path();
    fs::path base_logical;
    size_t start_index = 0;

    for (; start_index < parts.size(); ++start_index) {
        const auto& part = parts[start_index];
        if (is_glob(part))
            break;
        base_real /= part;
        base_logical /= part;
    }

    if (!fs::exists(base_real))
        return results;

    if (fs::is_regular_file(base_real) || fs::is_symlink(base_real)) {
        // If the base is a file, we treat it as a single match
        results.push_back(base_logical);
        return results;
    }

    glob_recursive(base_real, base_logical, parts, start_index, results);
    return results;
}

bool is_all_zero(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    size_t i = 0;

    // Align to uint64_t boundary if not already aligned
    size_t align_bytes = sizeof(uint64_t) - (addr % sizeof(uint64_t));
    if (align_bytes != sizeof(uint64_t) && align_bytes <= size) {
        for (; i < align_bytes; ++i) {
            if (p[i] != 0) return false;
        }
    }

    // Process 8 bytes at a time
    const uint64_t* p64 = reinterpret_cast<const uint64_t*>(p + i);
    size_t remaining = size - i;
    size_t chunks64 = remaining / sizeof(uint64_t);
    for (size_t j = 0; j < chunks64; ++j) {
        if (p64[j] != 0) return false;
    }

    // Check remaining tail bytes
    size_t tail_index = i + chunks64 * sizeof(uint64_t);
    for (size_t j = tail_index; j < size; ++j) {
        if (p[j] != 0) return false;
    }

    return true;
}
