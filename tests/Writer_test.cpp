#include <gtest/gtest.h>
#include "io/Writer.cpp"
#include <fstream>

const char* test_fname = "testfile.tmp";

class WriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove(test_fname);
    }

    void TearDown() override {
        std::filesystem::remove(test_fname);
    }
};

TEST_F(WriterTest, create_new) {
    {
        Writer w(test_fname);
        w.write("test", 4);
    }

    std::ifstream f(test_fname, std::ios::binary);
    char buf[0x20];
    f.read(buf, 0x20);
    EXPECT_EQ(f.gcount(), 4);
    EXPECT_EQ(0, memcmp(buf, "test", 4));
}

TEST_F(WriterTest, rewrite_existing) {
    {
        std::ofstream f(test_fname);
        f << "test";
    }

    {
        Writer w(test_fname, false);
        w.write("p", 1);
    }

    std::ifstream f(test_fname, std::ios::binary);
    char buf[0x20];
    f.read(buf, 0x20);
    EXPECT_EQ(f.gcount(), 4);
    EXPECT_EQ(0, memcmp(buf, "pest", 4));
}

TEST_F(WriterTest, truncate_existing) {
    {
        std::ofstream f(test_fname);
        f << "test";
    }

    {
        Writer w(test_fname);
        w.write("p", 1);
    }

    std::ifstream f(test_fname, std::ios::binary);
    char buf[0x20];
    f.read(buf, 0x20);
    EXPECT_EQ(f.gcount(), 1);
    EXPECT_EQ(0, memcmp(buf, "p", 1));
}

TEST_F(WriterTest, seek_and_write_existing) {
    {
        std::ofstream f(test_fname);
        f << "test";
    }

    {
        Writer w(test_fname, false);
        w.seek(10);
        w.write("bar", 3);
    }

    std::ifstream f(test_fname, std::ios::binary);
    char buf[0x20];
    f.read(buf, 0x20);
    EXPECT_EQ(f.gcount(), 13);
    EXPECT_EQ(0, memcmp(buf, "test\0\0\0\0\0\0bar", 13));
}

TEST_F(WriterTest, seek_tell) {
    Writer w(test_fname);
    EXPECT_EQ(w.tell(), 0);

    w.write("test", 4);
    EXPECT_EQ(w.tell(), 4);

    w.seek(10);
    EXPECT_EQ(w.tell(), 10);

    w.seek(5, SEEK_CUR);
    EXPECT_EQ(w.tell(), 15);
}
