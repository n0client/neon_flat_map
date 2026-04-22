#include <cassert>
#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>

#include "./ankerl/unordered_dense.h"

struct uint128_t { uint64_t x, y; uint128_t(uint64_t x_, uint64_t y_) : x(x_), y(y_) {}};

#ifndef BOUND
#define BOUND 10'000'000;
#endif

#ifndef MULT1
#define MULT1 2
#endif

#ifndef MULT2
#define MULT2 3
#endif

int main() 
{
    ankerl::unordered_dense::map<uint64_t, uint128_t> map;

    std::vector<uint64_t> keys(BOUND);
    std::iota(keys.begin(), keys.end(), 1);
    std::shuffle(keys.begin(), keys.end(), std::mt19937{std::random_device{}()});

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        map.insert({keys[i], {keys[i] * MULT1, keys[i] * MULT2}});
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "inserted " << BOUND << " uint128_t pairs in " << duration.count() << " ms\n";

    std::shuffle(keys.begin(), keys.end(), std::mt19937{42});

    uint64_t checksum = 0;
    start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        auto val = map.find(keys[i]);
        if (val != map.end()) checksum += val->second.x + val->second.y;
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "found " << BOUND << " uint128_t pairs in " << duration.count() << " ms\n";
    uint64_t expected = BOUND * (BOUND + 1) / 2 * (MULT1 + MULT2);
    if (checksum != expected)
        std::cout << "Expected: " << expected << " (Diff: " << (int64_t)(expected - checksum) << ")\n";

    start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        map.erase(keys[i]);
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "erased " << BOUND << " uint128_t pairs in " << duration.count() << " ms\n";

    for (size_t i = 0; i < BOUND; ++i)
    {
        assert(map.find(keys[i]) == map.end());
    }

    return 0;
}


    /*
    // yippee it worked
    std::unordered_map<size_t, size_t> checksum;
    for (size_t i = 0; i < BOUND; ++i)
        checksum.insert({keys[i], keys[i]*2});
    size_t count = 0;
    for (size_t i = 0; i < map.map.max_size_; ++i)
    {
        if (map.map.tags_[i] < 0) continue;

        auto it = checksum.find(map.map.data_[i].key);
        if (it == checksum.end() || map.map.data_[i].key * 2 != map.map.data_[i].value) break;
        count++;
    }
    std::cout << "checksum " << count << "\n";
    */
