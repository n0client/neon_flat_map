#pragma once


#include <utility>
namespace internal
{


template <typename Key, typename Value>
struct raw_pair 
{
    Key key;
    Value value;

    template <typename K, typename V>
    raw_pair(K&& k, V&& v)
        : key(std::forward<K>(k)),
          value(std::forward<V>(v)) {}

    raw_pair(const std::pair<Key, Value> &p)
        : key(p.first), value(p.second) {}

    raw_pair() = default;
    ~raw_pair() = default;

    raw_pair(const raw_pair&) = default;
    raw_pair(raw_pair&&) noexcept = default;
    raw_pair& operator=(const raw_pair&) = default;
    raw_pair& operator=(raw_pair&&) noexcept = default;
};

}; // namespace internal
