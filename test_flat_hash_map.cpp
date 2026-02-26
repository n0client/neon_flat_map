#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>

#include "flat_hash_map.h"

int main() {
    const size_t BOUND = 10'000'000;
    //const size_t BOUND = 1'000'000;
    flat_hash_map<uint64_t, uint64_t> map;

    // 1. Prepare randomized data to prevent prefetcher "cheating"
    std::vector<uint64_t> keys(BOUND);
    std::iota(keys.begin(), keys.end(), 1);
    std::shuffle(keys.begin(), keys.end(), std::mt19937{std::random_device{}()});

    // --- INSERT BENCHMARK ---
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        map.insert({keys[i], keys[i] * 2});


    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "inserted " << BOUND << " uint64_t pairs in " << duration.count() << " ms\n";
}
    /*

    // 2. Shuffle again for random lookup (Tests the SIMD and cache misses)
    std::shuffle(keys.begin(), keys.end(), std::mt19937{42});

    // --- FIND BENCHMARK ---
    uint64_t checksum = 0;
    start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        auto val = map.find(keys[i]);
        if (val != NULL) checksum += *val; // Side effect to prevent compiler optimization
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "found " << BOUND << " uint64_t pairs in " << duration.count() << " ms (sum: " << checksum << ")\n";
    uint64_t expected = BOUND * (BOUND + 1);
    if (checksum != expected) {
        std::cout << "Expected: " << expected << " (Diff: " << (int64_t)(expected - checksum) << ")\n";
    }

    // --- ERASE BENCHMARK ---
    start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        map.erase(keys[i]);
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "erased " << BOUND << " uint64_t pairs in " << duration.count() << " ms\n";

    return 0;
}
*/

