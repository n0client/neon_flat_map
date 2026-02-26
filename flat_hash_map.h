#pragma once

#include <functional>

#include "./internal/raw_map.h"
#include "./internal/policy.h"

template <
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename Equal = std::equal_to<Key>
>
class flat_hash_map
{
public:

    using policy = internal::flat_map_policy<Key, Value, Hash, Equal>;
    using raw_map = internal::raw_map<policy>;

    flat_hash_map() : map() {}

    auto 
    insert(const Key &key, const Value &value)
    {
        return map.do_emplace(hash(key), key, value);
    }

    auto
    insert(const std::pair<Key, Value> &p)
    {
        return map.do_emplace(hash(p.first), p);
    }

private:
    raw_map map;
    Hash hash;
};
