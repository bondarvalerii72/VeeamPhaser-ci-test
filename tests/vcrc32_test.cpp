#include <gtest/gtest.h>

extern "C" {
    uint32_t vcrc32(uint32_t crc, const char *buf, unsigned int len);
}

TEST(vcrc32, empty) {
    EXPECT_EQ(vcrc32(0, nullptr, 0), 0);
}

TEST(vcrc32, single_zero) {
    EXPECT_EQ(vcrc32(0, "\x00", 1), 0x527d5351);
}

TEST(vcrc32, two_zeroes) {
    EXPECT_EQ(vcrc32(0, "\x00\x00", 2), 0xf16177d2);
}

TEST(vcrc32, three_zeroes) {
    EXPECT_EQ(vcrc32(0, "\x00\x00\x00", 3), 0x6064a37a);
}

TEST(vcrc32, four_zeroes) {
    EXPECT_EQ(vcrc32(0, "\x00\x00\x00\x00", 4), 0x48674bc7);
}

TEST(vcrc32, hello_world) {
    EXPECT_EQ(vcrc32(0, "Hello, World!", 13), 0x4d551068);
}
