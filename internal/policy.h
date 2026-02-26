#pragma once

#include "raw_pair.h"

namespace internal
{

template <
    typename Key, 
    typename Value,
    typename Hash,
    typename Equal
>
struct flat_map_policy : private Hash, private Equal
{
    #define GROUP_SIZE 16
    using slot_type = raw_pair<Key, Value>;

    static constexpr size_t start_size = 16;
    static constexpr float resize_factor = 0.8;
    // Quadratic
    static constexpr size_t probe_step = 0;
    static constexpr size_t probe_jump= GROUP_SIZE;
    // Linear
    //static constexpr size_t probe_step = GROUP_SIZE;
    //static constexpr size_t probe_jump= 0;

    size_t
    hash_key(const Key &k) 
    {
        return static_cast<const Hash &>(*this)(k);
    }

    static const Key &
    get_key(slot_type &slot)
    {
        return slot.key;
    }

    bool
    equal_to(const Key &a, const Key &b)
    {
        return static_cast<const Equal &>(*this)(a, b);
    }

    template <typename K, typename... Args>
    static const K &
    extract_key(const K &key, const Args&...)
    {
        return key;
    }

    template <typename K, typename V>
    static const K &
    extract_key(const raw_pair<K, V> &p)
    {
        return p.key;
    }

    template <typename K, typename V>
    static const K &
    extract_key(const std::pair<K, V> &p)
    {
        return p.first;
    }

    template <typename... Args>
    static void
    construct(slot_type *p, Args&&... args)
    {
        ::new (static_cast<void *>(p)) slot_type(std::forward<Args>(args)...);
    }

    static void destroy(slot_type *p) { p->~slot_type(); }

    static void
    transfer(slot_type *dst, slot_type *src)
    {
        construct(dst, std::move(*src));
        destroy(src);
    }
};


}; // namespace internal
