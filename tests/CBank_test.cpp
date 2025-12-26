#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "Veeam/VBK.hpp"
#include "test_utils.hpp"

using namespace Veeam::VBK;

TEST(CBank, xx__valid_fast) {
    std::ifstream f(find_fixture("00189000.bank"), std::ios::binary);
    ASSERT_TRUE(f.is_open());

    char data[PAGE_SIZE];
    f.read(data, PAGE_SIZE);
    CBank* bank = (CBank*)data;
    ASSERT_TRUE(bank->valid_fast());
}

TEST(CBank, encrypted__valid_fast) {
    std::ifstream f(find_fixture("encrypted_bank_hdr.bin"), std::ios::binary);
    ASSERT_TRUE(f.is_open());

    char data[PAGE_SIZE];
    f.read(data, PAGE_SIZE);
    CBank* bank = (CBank*)data;
    ASSERT_TRUE(bank->valid_fast());
}

TEST(CBank, encrypted__is_encrypted) {
    std::ifstream f(find_fixture("encrypted_bank_hdr.bin"), std::ios::binary);
    ASSERT_TRUE(f.is_open());

    char data[PAGE_SIZE];
    f.read(data, PAGE_SIZE);
    CBank* bank = (CBank*)data;
    ASSERT_TRUE(bank->is_encrypted());
}

TEST(CBank, encrypted__encr_size) {
    std::ifstream f(find_fixture("encrypted_bank_hdr.bin"), std::ios::binary);
    ASSERT_TRUE(f.is_open());

    char data[PAGE_SIZE];
    f.read(data, PAGE_SIZE);
    CBank* bank = (CBank*)data;
    ASSERT_EQ(bank->encr_size(), 0x20010);
}
