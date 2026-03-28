#pragma once


#include <utility>
namespace internal
{


template <typename Key, typename Value>
struct raw_pair 
{
    Key first;
    Value second;

    template <typename K, typename V>
    raw_pair(K&& k, V&& v)
        : first(std::forward<K>(k)),
          second(std::forward<V>(v)) {}

    raw_pair(const std::pair<Key, Value> &p)
        : first(p.first), second(p.second) {}

    template <typename K, typename ...Args>
    raw_pair(K &&k, Args&&... args)
        : first(std::forward<K>(k)), 
          second(std::forward<Args>(args)...) {}

    raw_pair() = default;
    ~raw_pair() = default;

    raw_pair(const raw_pair&) = default;
    raw_pair(raw_pair&&) noexcept = default;
    raw_pair& operator=(const raw_pair&) = default;
    raw_pair& operator=(raw_pair&&) noexcept = default;
};

}; // namespace internal
