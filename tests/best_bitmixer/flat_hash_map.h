#pragma once

#include <functional>

#include "./internal/raw_map.h"
#include "./internal/policy.h"

template <
    typename F,
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename Equal = std::equal_to<Key>
>
class flat_hash_map
{
public:

    using policy = internal::flat_map_policy<Key, Value, Hash, Equal>;
    using raw_map = internal::raw_map<policy, F>;
    using iterator = typename raw_map::iterator;

    flat_hash_map() : map() {}
    flat_hash_map(flat_hash_map &&o):  map(o.map) {}
    flat_hash_map(const flat_hash_map &o) : map(o.map) {}
    ~flat_hash_map() = default;

    inline size_t cur_size() const noexcept { return map.cur_size(); }
    inline size_t max_size() const noexcept { return map.max_size(); }

    inline iterator begin() const { return map.begin(); }
    inline iterator end() const { return map.end(); }

    template <typename K, typename V>
    inline std::pair<iterator, bool>
    insert(K &&k, V &&v)
    {
        return map.do_emplace(std::forward<K>(k), 
                              std::forward<V>(v));
    }

    inline std::pair<iterator, bool>
    insert(std::pair<Key, Value> &&p)
    {
        return map.do_emplace(std::move(p.first), std::move(p.second));
    }

    // TODO check return type
    template <typename ...Args>
    inline std::pair<iterator, bool>
    emplace(Args&&... a)
    {
        return map.do_emplace(std::forward<Args>(a)...);
    }

    template <typename K>
    auto
    find(const K &k) const
    {
        //auto it = map.template find_or_prepare_insert<false>(k, hash(k));
        //return map.iterator_at(it.first);
        return map.iterator_at(map.find(k));
    }

    void clear() { map.clear(); }

private:
    raw_map map;
    Hash hash;
};

