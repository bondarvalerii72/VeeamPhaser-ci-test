#include "test_utils.hpp"
#include "core/structs.hpp"

TEST(BlockDescriptor, empty){
    BlockDescriptor bd;
    memset(&bd, 0, sizeof(BlockDescriptor));
    EXPECT_TRUE(bd.empty()) << bd.to_string();
}

TEST(BlockDescriptor, empty_ff){
    BlockDescriptor bd;
    memset(&bd, 0xff, sizeof(BlockDescriptor));
    EXPECT_TRUE(bd.empty()) << bd.to_string();
}

TEST(BlockDescriptor, valid_compressed){
    BlockDescriptor bd;
    memset(&bd, 0, sizeof(BlockDescriptor));

    bd.location = BL_BLOCK_IN_BLOB;
    bd.usageCnt = 1;
    bd.offset = 0x662b000;
    bd.allocSize = 0x102000;
    bd.dedup = 1;
    bd.digest = 0x6745a75922054cd2;
    bd.compType = CT_LZ4;
    bd.srcSize   = 0x100000;
    bd.compSize  = 0x1234;
    bd.allocSize = 0x2000;

    EXPECT_TRUE(bd.valid()) << bd.to_string();
}

TEST(BlockDescriptor, valid_overcompressed){
    BlockDescriptor bd;
    memset(&bd, 0, sizeof(BlockDescriptor));

    bd.location = BL_BLOCK_IN_BLOB;
    bd.usageCnt = 1;
    bd.offset = 0x662b000;
    bd.allocSize = 0x102000;
    bd.dedup = 1;
    bd.digest = 0x6745a75922054cd2;
    bd.compType = CT_LZ4;
    bd.srcSize   = 0x100000;
    bd.compSize  = 0x100101; // greater than srcSize
    bd.allocSize = 0x102000;

    EXPECT_TRUE(bd.valid()) << bd.to_string();
}
