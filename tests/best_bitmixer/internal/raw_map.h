
#pragma once

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>


namespace internal
{
// TODO experiment with this, more uniform, and fast
//#define MIX(x) (((((x) >> 31) ^ (x)) * 0xff51afd7ed558ccdull))
//inline uint64_t MIX(uint64_t x) { return(((((x) >> 31) ^ (x)) * 0xff51afd7ed558ccdull));  }

// #define TAKEN         0b0*******
#define EMPTY    0x80 // 0b10000000
#define DELETED  0xFF // 0b11111111
#define END      0xF0 // 0b11110000
#define PAIR_EXISTS_HERE(tag) ((tag) >= 0)
#define GROUP_SIZE 16

// TODO not all compilers support this
// only useful in find()?
#define LIKELY(x)   x // __builtin_expect(!!(x), 1)
#define UNLIKELY(x) x // __builtin_expect(!!(x), 0)


// TODO reentrant constructor/destructor thing

struct neon_tag_match
{
    static inline neon_tag_match 
    load(int8_t *tags)
    {
        neon_tag_match n;
        n.tags_ = vld1q_s8(tags);
        return n;
    }

    // if vec != 0
    // max: 0x1111111111111111
    // min: 0x0000000000000001
    static inline uint64_t
    vec_to_mask(uint8x16_t vec)
    {
        return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(
               vreinterpretq_u16_u8(vec), 4)), 0)
               & 0x1111111111111111ull;
    }

    inline uint64_t
    has_tag(int8_t tag)
    {
        int8x16_t tag_vec = vdupq_n_s8(tag);
        uint8x16_t res = vceqq_s8(tag_vec, tags_);
        if (vmaxvq_u8(res)) // != 0
            return vec_to_mask(res);
        return 0;
    }

private:
    int8x16_t tags_;
};


// jump - how much we increment by
// step - how much we step to the next group
template <size_t jump, size_t step>
struct probe_seq
{
    probe_seq(uint64_t hash_idx, size_t mask)
        : mask_(mask), step_(step)
    {
        offset_ = hash_idx & mask;
        offset_ &= ~(GROUP_SIZE - 1);
    }

    inline void 
    next()
    {
        step_ += jump;
        offset_ += step_;
        offset_ &= mask_;
    }

    inline size_t
    peek() { return (offset_ + step_ + jump) & mask_; }

    inline size_t
    index() { return offset_; }

private:
    size_t mask_;
    size_t step_;
    size_t offset_;
};


template <typename slot_type, typename Policy>
class raw_iterator
{
    Policy policy_;

public:
    auto& operator*() const { return *at_slot_; }
    auto& operator->() const { return at_slot_; }

    auto& operator++()
    {
        do { ++at_slot_; ++at_tag_; ++idx_; } while (*at_tag_ < 0);
        return *this;
    }

    bool operator==(const raw_iterator &o) const
    { 
        return idx_ == o.idx_;
    }

     bool operator!=(const raw_iterator &o) const
    { 
        return idx_ != o.idx_;
    }

    raw_iterator(slot_type *s, int8_t *t, size_t idx)
        : at_slot_(s), at_tag_(t), idx_(idx) {}

private:
    slot_type *at_slot_;
    int8_t *at_tag_;
    size_t idx_;
};


template <class Policy, typename MIX>
class raw_map
{
    MIX mix_;
    Policy policy_;

#define TAG(hash) (mix_(hash) >> 57)
#define IDX(hash) (mix_(hash) & 0x7FFFFFFFFFFFFFFF)

public:
    using slot_type = typename Policy::slot_type;
    using iterator = raw_iterator<slot_type, Policy>;
    using const_iterator = raw_iterator<const slot_type, Policy>;

    static constexpr size_t probe_step = Policy::probe_step;
    static constexpr size_t probe_jump = Policy::probe_jump;
    static constexpr float resize_factor = Policy::resize_factor;
    static constexpr size_t start_size = Policy::start_size;

    raw_map() : cur_size_(0), max_size_(start_size)
    {
        init_fields(data_, tags_, start_size);
    }

    raw_map(raw_map &&o) noexcept // MOVE
        : data_(o.data_), tags_(o.tags_), 
        cur_size_(o.cur_size_), max_size_(o.max_size_) 
    {
        o.max_size_ = o.cur_size_ = 0;
        o.tags_ = o.data_ = nullptr;
    }

    raw_map(const raw_map &o) 
        : cur_size_(o.cur_size_), max_size_(o.max_size_)
    {
        init_fields(data_, tags_, max_size_);
        std::memcpy(tags_, o.tags_, max_size_ * sizeof(int8_t));
        if constexpr (std::is_trivially_copyable_v<slot_type *>)
            std::memcpy(data_, o.data_, max_size_ * sizeof(slot_type));
        else
            for (size_t i = 0; i < max_size_; ++i)
                if (PAIR_EXISTS_HERE(tags_[i]))
                    data_[i] = o.data_[i];
    }

    ~raw_map() { clear(); free_fields(); }


    inline iterator 
    begin()
    {
        size_t idx = 0;
        int8_t *start = tags_;
        while (start[idx] < 0)
            idx++;
        return { data_[idx], tags_ + idx };
    }

    inline iterator 
    end() const
    {
        return { &data_[max_size_], &tags_[max_size_], max_size_};
    }

    inline iterator 
    iterator_at(size_t index) const
    {
        return { data_ + index, tags_ + index, index };
    }

    inline probe_seq<probe_jump, probe_step>
    probe(uint64_t hash_idx) const
    { 
        using probe_seq_ = probe_seq<probe_jump, probe_step>;
        return probe_seq_(hash_idx, max_size_ - 1); 
    }

    void
    clear()
    {
        if constexpr (!std::is_trivially_destructible_v<slot_type>)
            for (size_t i = 0; i < max_size_; ++i)
                if (PAIR_EXISTS_HERE(tags_[i]))
                    data_[i].~slot_type();
        std::memset(tags_, EMPTY, sizeof(int8_t) * max_size_);
    }


    template <typename K>
    inline size_t 
    find(const K &key) const
    { 
        uint64_t hash = policy_.hash_key(key);
        auto seq = probe(IDX(hash));
        uint8_t tag = TAG(hash);
        __builtin_prefetch(data_ + seq.index());
        __builtin_prefetch(tags_ + seq.index());
        while (true)
        {
            auto group = neon_tag_match::load(tags_ + seq.index());
            uint64_t mask = group.has_tag(tag);
            while ((mask))
            {
                uint8_t offset = __builtin_ctzll(mask) >> 2;
                if (policy_.equal_to(key,
                         Policy::get_key(data_[seq.index() + offset])))
                    return seq.index() + offset;
                mask &= (mask - 1);
            }

            if ((mask = group.has_tag(EMPTY)))
                return max_size_;

            seq.next();
        }
    }


    template <bool caller_may_insert = true, typename Key>
    // { index, is key new (inserted)? }
    std::pair<size_t, bool>
    find_or_prepare_insert(const Key &key, size_t hash) const
    {
        size_t deleted_idx;
        bool found_deleted = false;
        auto seq = probe(IDX(hash));
        uint8_t tag = TAG(hash);
        __builtin_prefetch(data_ + seq.index());
        __builtin_prefetch(tags_ + seq.index());
        while (true)
        {
            // Check for an existing key
            // TODO turn it into: for (idx : mask) {} - iterable bitmask
            auto group = neon_tag_match::load(tags_ + seq.index());
            uint64_t mask = group.has_tag(tag);
            while ((mask)) // TODO in order for this to be true, the MIX needs to be good
            {
                uint8_t offset = __builtin_ctzll(mask) >> 2;
                if (policy_.equal_to(key,
                         Policy::get_key(data_[seq.index() + offset])))
                    return { seq.index() + offset, false };
                mask &= (mask - 1);
            }

            // TODO Better way to check for deleted and empty
            // Check for an empty slot
            if ((mask = group.has_tag(EMPTY)))
            {
                if constexpr (caller_may_insert)
                {
                    return { seq.index() + 
                                (found_deleted ? deleted_idx : 
                                        __builtin_ctzll(mask) >> 2), 
                             true };
                }
                else return { max_size_, false };
            }

            if constexpr (caller_may_insert)
            {
                // Record the first deleted slot
                if (!found_deleted && (mask = group.has_tag(DELETED)))
                {
                    found_deleted = true;
                    deleted_idx = seq.index() + (__builtin_ctzll(mask) >> 2);
                }
            }
            seq.next();
        }
    }


    void
    insert_and_swap_unchecked(slot_type *data, int8_t *tags, 
            size_t src_idx, size_t hash)
    {
        auto seq = probe(IDX(hash));
        while (true)
        {
            auto group = neon_tag_match::load(tags + seq.index());
            size_t mask = group.has_tag(EMPTY);
            if (LIKELY(mask))
            {
                uint8_t offset = __builtin_ctzll(mask) >> 2;
                size_t idx = seq.index() + offset;
                Policy::transfer(data + idx, data_ + src_idx);
                tags[idx] = tags_[src_idx];
                return;
            }
            seq.next();
        }
    }


    void
    resize()
    {
        int8_t *tags;
        slot_type *data;
        max_size_ <<= 1;
        init_fields(data, tags, max_size_);

        __builtin_prefetch(tags_);
        __builtin_prefetch(data_);
        for (size_t i = 0; i < max_size_ >> 1; ++i)
            if (LIKELY(PAIR_EXISTS_HERE(tags_[i])))
                insert_and_swap_unchecked(data, tags, i,
                        policy_.hash_key(Policy::get_key(data_[i])));

        free_fields();
        tags_ = tags;
        data_ = data;
    }

    template <typename... Args>
    std::pair<iterator, bool>
    do_emplace(Args&&... args)
    {
        const auto &key = Policy::extract_key(args...);
        uint64_t hash = policy_.hash_key(key);

        if (UNLIKELY(cur_size_ >= max_size_ * resize_factor))
            resize();

        // { index, did it insert? }
        auto search = find_or_prepare_insert(key, hash);
        if (!search.second)
            return { iterator_at(search.first), false };

        Policy::construct(data_ + search.first, std::forward<Args>(args)...);
        tags_[search.first] = TAG(hash);
        cur_size_++;
        return { iterator_at(search.first), true };
    }

    inline size_t cur_size() const noexcept { return cur_size_; }
    inline size_t max_size() const noexcept { return max_size_; }

// tags_ has capacity max_size_ + 1 for empty signal 
private:
    slot_type *data_; // key/value pairs (map) or keys (set)
    int8_t *tags_;    // metadata
    size_t cur_size_, max_size_;

    void init_fields(slot_type *&data, int8_t *&tags, size_t size)
    {
        tags = static_cast<int8_t *>(std::malloc((size + 1) * sizeof(int8_t)));
        data = static_cast<slot_type *>(operator new((size + 1) * sizeof(slot_type)));
        std::memset(tags, EMPTY, size * sizeof(int8_t));
        tags[size] = END;
    }

    void free_fields()
    {
        std::free(tags_);
        operator delete(data_);
    }

}; // class raw_map

}; // namespace internal

