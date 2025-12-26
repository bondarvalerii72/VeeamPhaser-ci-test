#include <gtest/gtest.h>
#include "io/Reader.cpp"
#include <fstream>

TEST(Reader, open_not_existing_file) {
    EXPECT_THROW(Reader reader("not_existing_file"), std::runtime_error);
}

TEST(Reader, normal_read) {
    char buf[0x1000];
    memset(buf, 0, 0x1000);

    std::ofstream file("test.bin", std::ios::binary);
    file.write(buf, 0x1000);
    file.close();

    Reader reader("test.bin");
    char buf2[0x1000];
    EXPECT_EQ(0x1000, reader.read_at(0, buf2, 0x1000));
    EXPECT_EQ(0, memcmp(buf, buf2, 0x1000));
}

TEST(Reader, partial_read) {
    char buf[0x1000];
    memset(buf, 0, 0x1000);

    std::ofstream file("test.bin", std::ios::binary);
    file.write(buf, 0x1000);
    file.close();

    Reader reader("test.bin");
    char buf2[0x1000];
    EXPECT_EQ(0x1000, reader.read_at(0, buf2, 0x2000));
    EXPECT_EQ(0, memcmp(buf, buf2, 0x1000));
}

TEST(Reader, partial_read2) {
    char buf[0x1000];
    memset(buf, 0, 0x1000);

    std::ofstream file("test.bin", std::ios::binary);
    file.write(buf, 0x1000);
    file.close();

    Reader reader("test.bin");
    char buf2[0x1000];
    EXPECT_EQ(0x0f00, reader.read_at(0x100, buf2, 0x2000));
    EXPECT_EQ(0, memcmp(buf, buf2, 0x0f00));
}

TEST(Reader, read_past_eof){
    std::ofstream file("test.bin", std::ios::binary);
    file.close();

    Reader reader("test.bin");
    EXPECT_EQ(0, reader.read_at(0, nullptr, 0x1000));
    EXPECT_EQ(0, reader.read_at(1, nullptr, 0x1000));
    EXPECT_EQ(0, reader.read_at(0x1000, nullptr, 0x1000));
}

TEST(Reader, size) {
    char buf[0x1000];
    memset(buf, 0, 0x1000);

    std::ofstream file("test.bin", std::ios::binary);
    file.write(buf, 0x1000);
    file.close();

    Reader reader("test.bin");
    EXPECT_EQ(0x1000, reader.size());
}

TEST(Reader, align){
    std::ofstream file("test.bin", std::ios::binary);
    file.close();

    Reader reader("test.bin");
    EXPECT_EQ(0, reader.get_align());
}
