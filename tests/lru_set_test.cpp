#include <gtest/gtest.h>
#include "data/lru_set.hpp"
#include <cstdint>

// Test fixture (optional)
class LruSetTest : public ::testing::Test {
protected:
    lru_set<__int128_t> cache{3};
};

TEST_F(LruSetTest, InsertAndContains) {
    __int128_t a = 1;
    __int128_t b = 2;

    EXPECT_FALSE(cache.contains(a));
    cache.insert(a);
    EXPECT_TRUE(cache.contains(a));

    EXPECT_FALSE(cache.contains(b));
    cache.insert(b);
    EXPECT_TRUE(cache.contains(b));
}

TEST_F(LruSetTest, LruEviction) {
    __int128_t a = 1;
    __int128_t b = 2;
    __int128_t c = 3;
    __int128_t d = 4;

    cache.insert(a);
    cache.insert(b);
    cache.insert(c);

    // All should be present
    EXPECT_TRUE(cache.contains(a));
    EXPECT_TRUE(cache.contains(b));
    EXPECT_TRUE(cache.contains(c));

    // Insert d → evicts LRU (a)
    cache.insert(d);

    EXPECT_FALSE(cache.contains(a));  // a evicted
    EXPECT_TRUE(cache.contains(b));
    EXPECT_TRUE(cache.contains(c));
    EXPECT_TRUE(cache.contains(d));
}

TEST_F(LruSetTest, ContainsPromotesKey) {
    __int128_t a = 1;
    __int128_t b = 2;
    __int128_t c = 3;
    __int128_t d = 4;

    cache.insert(a);
    cache.insert(b);
    cache.insert(c);

    // Access a to promote it (a is MRU now)
    EXPECT_TRUE(cache.contains(a));

    // Insert d → evict LRU (which should be b now, not a)
    cache.insert(d);

    EXPECT_TRUE(cache.contains(a));   // a preserved due to promotion
    EXPECT_FALSE(cache.contains(b));  // b evicted
    EXPECT_TRUE(cache.contains(c));
    EXPECT_TRUE(cache.contains(d));
}

TEST_F(LruSetTest, ClearEmptiesCache) {
    __int128_t a = 1;
    __int128_t b = 2;

    cache.insert(a);
    cache.insert(b);

    EXPECT_FALSE(cache.empty());
    cache.clear();
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.size(), 0u);

    EXPECT_FALSE(cache.contains(a));
    EXPECT_FALSE(cache.contains(b));
}
