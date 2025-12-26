#include <gtest/gtest.h>
#include "Veeam/VBK.hpp"

using namespace Veeam::VBK;

TEST(PhysPageId, default_ctor) {
    PhysPageId ppi;
    EXPECT_FALSE(ppi.valid());
    EXPECT_EQ(-1, ppi.bank_id);
    EXPECT_EQ(-1, ppi.page_id);
}

TEST(PhysPageId, ctor) {
    PhysPageId ppi(1, 2);
    EXPECT_TRUE(ppi.valid());
    EXPECT_EQ(1, ppi.bank_id);
    EXPECT_EQ(2, ppi.page_id);
}

TEST(PhysPageId, string_ctor){
    PhysPageId ppi("12:22");
    EXPECT_TRUE(ppi.valid());
    EXPECT_EQ(0x12, ppi.bank_id);
    EXPECT_EQ(0x22, ppi.page_id);
}

TEST(PhysPageId, valid){
    PhysPageId ppi;
    EXPECT_FALSE(ppi.valid());

    ppi = PhysPageId("12:22");
    EXPECT_TRUE(ppi.valid());
}

TEST(PhysPageId, operator_bool){
    PhysPageId ppi;
    EXPECT_FALSE(ppi);

    ppi = PhysPageId("12:22");
    EXPECT_TRUE(ppi);
}
