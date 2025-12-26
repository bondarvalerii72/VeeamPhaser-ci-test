#include <gtest/gtest.h>
#include <string>
#include <cstdint>
#include "core/structs.hpp"

TEST(VBlockDescTest, ToStringFormatting) {
    // Prepare a test instance
    VBlockDesc block = {
        0x12345678, // clustsize
        0x12,       // type
        { 0x0011223344556677, 0xefcdab8967452301 }, // hash
        0x9876543210,   // id
        0x123456789ABC, // vib_offset
        0x0FEDCBA98765, // qw2
        0x34            // t2
    };

    // Expected output string
    std::string expected_output = 
        "<VBlockDesc size=12345678, type=12, hash=77665544332211000123456789abcdef, "
        "id=9876543210, vib_offset=123456789abc, qw2=fedcba98765, t2=34>";

    // Compare the actual output to the expected output
    EXPECT_EQ(block.to_string(), expected_output);
}
