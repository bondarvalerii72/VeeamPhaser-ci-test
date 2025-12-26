#include <gtest/gtest.h>
#include "Veeam/VBK/digest_t.hpp"

using namespace Veeam::VBK;

TEST(digest_t, default){
    digest_t digest;
    std::array<uint8_t, 16> expected = {0};
    EXPECT_EQ(0, memcmp(&digest, expected.data(), expected.size()));
}

TEST(digest_t, zero_init){
    digest_t digest = 0;
    std::array<uint8_t, 16> expected = {0};
    EXPECT_EQ(0, memcmp(&digest, expected.data(), expected.size()));
}

TEST(digest_t, format){
    digest_t digest = 0;
    EXPECT_EQ("00000000000000000000000000000000", fmt::format("{}", digest));

    digest = {0xd872560a361bd8b6ULL, 0x2c3e15390f43270cULL};
    EXPECT_EQ("b6d81b360a5672d80c27430f39153e2c", fmt::format("{}", digest));
}

TEST(digest_t, parse){
    digest_t digest = 0;
    EXPECT_EQ(digest_t(), digest_t::parse("00000000000000000000000000000000"));

    digest = {0xd872560a361bd8b6ULL, 0x2c3e15390f43270cULL};
    EXPECT_EQ(digest, digest_t::parse("b6d81b360a5672d80c27430f39153e2c"));
}

TEST(digest_t, double_u64_ctor){
    static constexpr digest_t digest {0xd872560a361bd8b6ULL, 0x2c3e15390f43270cULL};
    std::array<uint8_t, 16> expected {0xb6, 0xd8, 0x1b, 0x36, 0x0a, 0x56, 0x72, 0xd8,  0x0c, 0x27, 0x43, 0x0f, 0x39, 0x15, 0x3e, 0x2c};
    EXPECT_EQ(0, memcmp(&digest, expected.data(), expected.size()));
}
