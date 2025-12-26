#include <gtest/gtest.h>
#include "processing/MD5.hpp"

TEST(MD5, empty) {
    MD5 md5;
    auto result = md5.Calculate("", 0);
    EXPECT_EQ(result, digest_t(0x4b2008fd98c1dd4, 0x7e42f8ec980980e9));
}

TEST(MD5, test) {
    MD5 md5;
    auto result = md5.Calculate("test", 4);
    EXPECT_EQ(result, digest_t(0x73d32146cd6b8f09, 0xf6b42726834edeca));
}
