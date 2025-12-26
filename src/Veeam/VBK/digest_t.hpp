#pragma once
#include <cstdint>
#include <spdlog/fmt/bundled/core.h>

namespace Veeam::VBK {

// required to be packed or windows version not buildable
struct __attribute__((packed)) digest_t {
    __uint128_t value = 0;

    constexpr digest_t() = default;
    constexpr digest_t(__uint128_t v) : value(v) {}
    constexpr digest_t(uint64_t lo, uint64_t hi) : value(((__uint128_t)hi << 64) | lo) {}

    // required for literal type
    ~digest_t() = default;

    // for use in if statements like if(digest) or if(!digest)
    constexpr operator bool() const {
        return value != 0;
    }

    // Equality comparison (needed for hash containers)
    constexpr bool operator==(const digest_t& other) const {
        return value == other.value;
    }

    // Less-than comparison (needed for ordered containers)
    constexpr bool operator<(const digest_t& other) const {
        return value < other.value;
    }

    // for (uint8_t byte : md5) ...
    const uint8_t* begin() const { return reinterpret_cast<const uint8_t*>(&value); }
    const uint8_t* end() const { return reinterpret_cast<const uint8_t*>(&value) + sizeof(value); }

    static digest_t parse(const std::string& hex) {
        digest_t result = 0;
        uint8_t* pbyte = reinterpret_cast<uint8_t*>(&result);

        size_t max_bytes = std::min(hex.size() / 2, sizeof(digest_t));
        for (size_t i = 0; i < max_bytes; ++i) {
            char hi = hex[2 * i];
            char lo = hex[2 * i + 1];

            auto from_hex = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                throw std::invalid_argument("Invalid hex character");
            };

            pbyte[i] = (from_hex(hi) << 4) | from_hex(lo);
        }

        return result;
    }
};

}

// hash specialization for digest_t
namespace std {
template <>
    struct hash<Veeam::VBK::digest_t> {
        std::size_t operator()(const Veeam::VBK::digest_t& d) const {
            return std::hash<__uint128_t>{}(d.value);
        }
    };
}

// fmt formatter for digest_t
template <>
struct fmt::formatter<Veeam::VBK::digest_t> {
    constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
        auto format(const Veeam::VBK::digest_t& digest, FormatContext& ctx) const {
            uint64_t lo = static_cast<uint64_t>(digest.value);
            uint64_t hi = static_cast<uint64_t>(digest.value >> 64);
            lo = __builtin_bswap64(lo);
            hi = __builtin_bswap64(hi);
            return fmt::format_to(ctx.out(), "{:016x}{:016x}", lo, hi);
        }
};
