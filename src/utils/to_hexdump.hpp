#pragma once
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

class Hexdump {
    public:
    Hexdump(const void *ptr, size_t buflen) : m_ptr(ptr), m_size(buflen) {}
    Hexdump indent(int level){ m_indent = level; return *this; }
    Hexdump prefix(const std::string& prefix){ m_prefix = prefix; return *this; }

    std::string to_string(size_t width) const;
    operator std::string() const;

    private:
    const void *m_ptr;
    size_t m_size;
    int m_indent = 0;
    std::string m_prefix;
};

static Hexdump to_hexdump(const void *ptr, size_t buflen){
    return Hexdump(ptr, buflen);
}

static Hexdump to_hexdump(const std::vector<uint8_t>& str){
    return to_hexdump(str.data(), str.size());
}

static Hexdump to_hexdump(const std::string& str, size_t max_size=0){
    max_size = max_size ? std::min(max_size, str.size()) : str.size();
    return to_hexdump(str.data(), max_size);
}

// Define a custom fmt::formatter specialization for Hexdump, especially for spdlog transparent integration
namespace fmt {
    template <>
    struct formatter<Hexdump> : formatter<std::string> {
        // This function will format the Hexdump object as a string
        template <typename FormatContext>
        auto format(Hexdump& hd, FormatContext& ctx) const {
            return formatter<std::string>::format(static_cast<std::string>(hd.prefix("\n").indent(4)), ctx);  // Using operator std::string
        }
    };
}

