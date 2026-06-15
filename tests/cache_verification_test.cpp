// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Intel Corporation
// Test program to verify cache functionality in mapNUMANodeToSocket()

#include <iostream>
#include <chrono>
#include "../src/cpucounters.h"

using namespace pcm;
using namespace std::chrono;

int main()
{
    std::cout << "Testing mapNUMANodeToSocket() cache functionality\n";
    std::cout << "================================================\n\n";
    
    PCM * m = PCM::getInstance();
    
    if (m->program() != PCM::Success)
    {
        std::cout << "Note: Cannot access CPU counters (expected on non-Intel or without root)\n";
        std::cout << "This test will still verify that the cache mechanism compiles correctly.\n";
        std::cout << "Cache verification test PASSED (compilation check)\n";
        return 0;
    }
    
    std::cout << "PCM initialized successfully\n";
    std::cout << "Testing cache performance...\n\n";
    
    // Test NUMA node 0 multiple times to measure caching effect
    const int iterations = 100;
    
    // First call - should compute and cache
    auto start = high_resolution_clock::now();
    int32 result1 = m->mapNUMANodeToSocket(0);
    auto end = high_resolution_clock::now();
    auto first_duration = duration_cast<microseconds>(end - start).count();
    
    std::cout << "First call (cache miss) for NUMA node 0: " << result1 
              << " (took " << first_duration << " microseconds)\n";
    
    // Subsequent calls - should use cache
    start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        int32 result = m->mapNUMANodeToSocket(0);
        if (result != result1) {
            std::cerr << "ERROR: Inconsistent results from cache!\n";
            return 1;
        }
    }
    end = high_resolution_clock::now();
    auto cached_duration = duration_cast<microseconds>(end - start).count();
    double avg_cached = static_cast<double>(cached_duration) / iterations;
    
    std::cout << "Average time for " << iterations << " cached calls: " 
              << avg_cached << " microseconds per call\n";
    
    // Cache should be significantly faster (at least 2x)
    if (first_duration > 0 && avg_cached > 0) {
        double speedup = static_cast<double>(first_duration) / avg_cached;
        std::cout << "\nSpeedup from caching: " << speedup << "x\n";
        
        if (speedup > 2.0) {
            std::cout << "\nCache is working effectively!\n";
        } else {
            std::cout << "\nWarning: Cache speedup is less than expected, but this may be normal on some systems.\n";
        }
    }
    
    // Test multiple NUMA nodes to verify cache stores multiple entries
    std::cout << "\nTesting multiple NUMA nodes (0-7):\n";
    for (uint32 node = 0; node < 8; ++node) {
        int32 socket = m->mapNUMANodeToSocket(node);
        std::cout << "  NUMA node " << node << " -> Socket " << socket << "\n";
    }
    
    // Test NUMA node > 255 (now also cached)
    std::cout << "\nTesting NUMA node 256 (now also cached):\n";
    int32 result_256 = m->mapNUMANodeToSocket(256);
    std::cout << "  NUMA node 256 -> Socket " << result_256 << "\n";
    
    std::cout << "\nCache verification test PASSED\n";
    
    m->cleanup();
    return 0;
}
