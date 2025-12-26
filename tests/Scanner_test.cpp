#include <gtest/gtest.h>
#include "scanning/Scanner.cpp"

TEST(check_hbuf, zero) {
    char buf[0x1000] = {0};

    *(uint16_t*)buf = 0;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 1;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 2;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0x5555;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffd;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffe;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 0xffff;
    EXPECT_FALSE(check_hbuf(buf));
}

TEST(check_hbuf, one) {
    char buf[0x1000];
    memset(buf, 1, sizeof(buf));

    *(uint16_t*)buf = 0;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 1;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 2;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0x5555;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffd;
    EXPECT_TRUE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffe;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 0xffff;
    EXPECT_FALSE(check_hbuf(buf));
}

// should be all false
TEST(check_hbuf, two) {
    char buf[0x1000] = {0};
    memset(buf, 2, sizeof(buf));

    *(uint16_t*)buf = 0;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 1;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 2;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffd;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 0xfffe;
    EXPECT_FALSE(check_hbuf(buf));

    *(uint16_t*)buf = 0xffff;
    EXPECT_FALSE(check_hbuf(buf));
}
