#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>

#include "../flat_hash_map.h"

struct uint128_t { uint64_t x, y; uint128_t(uint64_t x_, uint64_t y_) : x(x_), y(y_) {}};
struct Spy 
{
    uint64_t val;

    Spy(uint64_t v = 0) : val(v) 
    {
        std::cout << "  [Construct] " << val << "\n";
    }

    // Copy Constructor (AVOID)
    Spy(const Spy& o) : val(o.val) 
    {
        std::cout << "  [COPY] " << val << "\n";
    }

    // Move Constructor (PREFER)
    Spy(Spy&& o) noexcept : val(o.val) 
    {
        o.val = 0;
        std::cout << "  [MOVE] " << val << "\n";
    }

    Spy& operator=(const Spy& o) { val = o.val; std::cout << "  [Copy Assign]\n"; return *this; }
    Spy& operator=(Spy&& o) { val = o.val; o.val = 0; std::cout << "  [Move Assign]\n"; return *this; }

    ~Spy() { if (val != 0) std::cout << "  [Destruct] " << val << "\n"; }

    bool operator==(const Spy& o) const { return val == o.val; }
};

namespace std {
    template<> struct hash<Spy> {
        size_t operator()(const Spy& s) const { return hash<uint64_t>{}(s.val); }
    };
}

int main() {/*{


    flat_hash_map<Spy, int> map;

    Spy s(200);
    std::cout << "Inserting lvalue pair\n";
    map.insert({s, 1});
    std::cout << "Inserting rvalue pair\n";
    map.insert({Spy(500), 1});
    std::cout << "Inserting lvalue\n";
    map.insert(s, 1);
    std::cout << "Inserting rvalue\n";
    map.insert(Spy(100), 1);
    std::cout << "Emplacing\n";
    map.emplace(300, 3);
}*/
    const size_t BOUND = 10'000'000;
    //const size_t BOUND = 1'000'00;
    flat_hash_map<uint64_t, uint128_t, std::hash<uint64_t>> map;

    std::vector<uint64_t> keys(BOUND);
    std::iota(keys.begin(), keys.end(), 1);
    std::shuffle(keys.begin(), keys.end(), std::mt19937{std::random_device{}()});

    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        map.insert({keys[i], {keys[i] * 2, keys[i] * 3}});
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "inserted " << BOUND << " uint64_t pairs in " << duration.count() << " ms\n";

    std::shuffle(keys.begin(), keys.end(), std::mt19937{42});

    uint64_t checksum = 0;
    start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < BOUND; ++i) {
        auto val = map.find(keys[i]);
        if (val != map.end()) checksum += val->second.x + val->second.y;
    }
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "found " << BOUND << " uint64_t pairs in " << duration.count() << " ms (sum: " << checksum << ")\n";
    uint64_t expected = BOUND * (BOUND + 1);
    if (checksum != expected) {
        std::cout << "Expected: " << expected << " (Diff: " << (int64_t)(expected - checksum) << ")\n";
    }
}

/*
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
