# SIMD Flat Hash Map

A high-performance open-addressing hash map implemented in C++ with:

- Header-only
- SIMD-accelerated tag matching (ARM NEON)
    - 7 bit tag used for filtering, and 57 bit hash used for matching exact entries
- Quadratic group-based probing (16 at once)
- Flat storage layout

I wanted to experiment with SIMD filtering, cache efficient designs, but also learn policy based template programming. This is similar to Google's implementation, since I wanted to learn concepts used in production-grade code.

Small benchmark (Mac m1 pro 10c)
clang++ -o test test_flat_hash_map.cpp     && ./test:    inserted 1000000 uint64_t pairs in 253 ms
clang++ -o test test_flat_hash_map.cpp -O3 && ./test:    inserted 1000000 uint64_t pairs in 24 ms
clang++ -o test test_flat_hash_map.cpp     && ./test:    inserted 10000000 uint64_t pairs in 2710 ms
clang++ -o test test_flat_hash_map.cpp -O3 && ./test:    inserted 10000000 uint64_t pairs in 291 ms

