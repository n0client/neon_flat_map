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
    using key_type = Key;
    using value_type = Value;

    static constexpr size_t start_size = 16;
    static constexpr float resize_factor = 0.8;
    // Quadratic
    static constexpr size_t probe_step = 0;
    static constexpr size_t probe_jump= GROUP_SIZE;
    // Linear
    //static constexpr size_t probe_step = GROUP_SIZE;
    //static constexpr size_t probe_jump= 0;

    // TODO overload for const char *, std::strng_view, std::string etc
    template <typename K>
    inline size_t
    hash_key(const K &k) const
    {
        return static_cast<const Hash &>(*this)(k);
    }

    // TODO is_transparent find, heterogeneous hashing
    template <typename K1, typename K2>
    inline bool
    equal_to(K1 &&k1, K2 &&k2) const
    {
        return static_cast<const Equal &>(*this)(
            std::forward<K1>(k1),
            std::forward<K2>(k2)
        );
    }

    inline static const Key &
    get_key(const slot_type &slot)
    {
        return slot.first;
    }

    template <typename K, typename... Args>
    inline static const K &
    extract_key(const K &k, const Args&...)
    {
        return k;
    }

    template <typename... Args>
    inline static void
    construct(slot_type *p, Args&&... args)
    {
        ::new (static_cast<void *>(p)) slot_type(std::forward<Args>(args)...);
    }

    inline static void destroy(slot_type *p) { p->~slot_type(); }

    inline static void
    transfer(slot_type *dst, slot_type *src)
    {
        construct(dst, std::move(*src));
        destroy(src);
    }
};


struct Equal
{

};

}; // namespace internal
