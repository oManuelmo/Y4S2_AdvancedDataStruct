#include "bloom_filter.hpp"
#include <cassert>
#include <iostream>

void testBloomFilter() {
    std::cout << "Testing Bloom Filter...\n";
    
    // Test optimal params calculation
    auto params = optimalBloomParams(10000, 0.01);
    assert(params.bitCount > 0);
    assert(params.hashCount > 0);
    assert(params.fpRate <= 0.01);
    
    // Test basic operations
    BloomFilter filter(1000, 3);
    filter.add("test1");
    filter.add("test2");
    
    assert(filter.contains("test1"));
    assert(filter.contains("test2"));
    assert(!filter.contains("test3"));
    
    // Test false positive rate
    BloomFilter filter2(10000, 7);
    for (int i = 0; i < 1000; i++) {
        filter2.add("item_" + std::to_string(i));
    }
    
    int falsePositives = 0;
    int tests = 10000;
    for (int i = 1000; i < 1000 + tests; i++) {
        if (filter2.contains("item_" + std::to_string(i))) {
            falsePositives++;
        }
    }
    
    double fpRate = static_cast<double>(falsePositives) / tests;
    std::cout << "  False positive rate: " << fpRate * 100 << "%\n";
    assert(fpRate < 0.05);  // Should be reasonably low
    
    std::cout << "✓ Bloom filter tests passed\n";
}

void testCountingBloomFilter() {
    std::cout << "\nTesting Counting Bloom Filter...\n";
    
    CountingBloomFilter filter(1000, 3);
    
    // Add and verify
    filter.add("test1");
    filter.add("test2");
    assert(filter.contains("test1"));
    assert(filter.contains("test2"));
    
    // Remove and verify
    filter.remove("test1");
    assert(!filter.contains("test1"));
    assert(filter.contains("test2"));
    
    std::cout << "✓ Counting Bloom filter tests passed\n";
}

void testScalableBloomFilter() {
    std::cout << "\nTesting Scalable Bloom Filter...\n";

    // Very small initial capacity to force growth.
    scalableBloomFilter filter(/*initialExpectedInsertions=*/10, /*targetFpRate=*/0.01);

    // Insert more than the initial layer capacity.
    for (int i = 0; i < 50; i++) {
        filter.add("item_" + std::to_string(i));
    }

    // Must contain everything inserted.
    for (int i = 0; i < 50; i++) {
        assert(filter.contains("item_" + std::to_string(i)));
    }

    // Should have grown to multiple layers.
    assert(filter.layerCount() > 1);
    assert(filter.insertions() == 50);

    // Basic sanity: unlikely to contain unrelated items at this scale.
    int falsePositives = 0;
    int tests = 5000;
    for (int i = 1000; i < 1000 + tests; i++) {
        if (filter.contains("other_" + std::to_string(i))) {
            falsePositives++;
        }
    }
    double fpRate = static_cast<double>(falsePositives) / tests;
    std::cout << "  Observed FP rate: " << fpRate * 100 << "%\n";
    assert(fpRate < 0.05);

    std::cout << "✓ Scalable Bloom filter tests passed\n";
}

int main() {
    testBloomFilter();
    testCountingBloomFilter();
    testScalableBloomFilter();
    return 0;
}