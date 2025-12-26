#pragma once
#include "io/Logger.hpp"
#include "units.hpp"
#include "Veeam/VBK/digest_t.hpp"
#include "core/buf_t.hpp"

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <signal.h>

namespace fs = std::filesystem;

#include <argparse/argparse.hpp>

#define APP_NAME "VeeamPhaser"

#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define ANSI_CLEAR_EOL     "\x1b[0K"

using digest_t = Veeam::VBK::digest_t;
static_assert(sizeof(digest_t) == 16, "digest_t must be 16 bytes");

extern std::shared_ptr<Logger> logger;
void init_log(const fs::path& src_fname, std::string log_fname = "");
bool parse_bool(const std::string value);
void register_program_args(argparse::ArgumentParser &parser);
void register_common_args(argparse::ArgumentParser &parser);
void signal_handler(int sig);

std::string sanitize_fname(const std::string& fname);
std::string sanitize_fname(const fs::path& fname);

fs::path get_out_pathname(const fs::path&, const std::string&);
fs::path get_out_dir(const fs::path& in_fname);
fs::path expand_dir_tpl(const std::string& tpl, const fs::path& in_fname);

bool path_ends_with(const fs::path& p, const fs::path::string_type& suffix);
std::string filter_unprintable(const std::string& str);

bool is_glob(const std::filesystem::path&);
bool is_glob(const std::string&);
std::vector<fs::path> simple_glob_find(const fs::path& pattern);

bool is_all_zero(const void* data, size_t size);

// because there's no fnmatch() in mingw
// define for both string(linux) and wstring(windows)
template<typename StringT>
bool simple_glob_match(const StringT& pattern, const StringT& str) {
    using CharT = typename StringT::value_type;
    size_t p = 0, s = 0, star = StringT::npos, match = 0;

    while (s < str.size()) {
        if (p < pattern.size() && (pattern[p] == CharT('?') || pattern[p] == str[s])) {
            ++p; ++s;
        } else if (p < pattern.size() && pattern[p] == CharT('*')) {
            star = p++;
            match = s;
        } else if (star != StringT::npos) {
            p = star + 1;
            s = ++match;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == CharT('*')) ++p;
    return p == pattern.size();
}

#ifdef __WIN32__
void* memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len);
std::string fmtLastError();
std::wstring operator"" _n(const char* str, std::size_t len);
#else
std::string operator"" _n(const char* str, std::size_t len);
#endif
