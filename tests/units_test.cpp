#include <gtest/gtest.h>
#include "utils/units.hpp"

TEST(bytes2human, should_return_0_for_0) {
    EXPECT_EQ("0", bytes2human(0));
}

TEST(bytes2human, default_unit) {
    EXPECT_EQ("0 bytes", bytes2human(0, " bytes"));
}

TEST(bytes2human, _3mb) {
    EXPECT_EQ("3072Kb", bytes2human(3*1024*1024));
}

TEST(bytes2human, min_unit) {
    EXPECT_EQ("3Mb", bytes2human(3*1024*1024, "", 1024*1024));
}

TEST(bytes2human, _4mb) {
    EXPECT_EQ("4Mb", bytes2human(4*1024*1024));
}

TEST(bytes2human, _4gb) {
    EXPECT_EQ("4Gb", bytes2human(4ULL*1024*1024*1024));
}

TEST(bytes2human, _4tb) {
    EXPECT_EQ("4Tb", bytes2human(4ULL*1024*1024*1024*1024));
}
