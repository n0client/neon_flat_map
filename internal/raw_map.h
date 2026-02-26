
#pragma once

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>


namespace internal
{

// #define TAKEN         0b0*******
#define EMPTY    0x80 // 0b10000000
#define DELETED  0xFF // 0b11111111
#define END      0xF0 // 0b11110000
#define TAKEN(tag) (tag >= 0)
// TODO experiment with this
#define MIX(x) (((((x) >> 33) ^ (x)) * 0xff51afd7ed558ccdull))
#define TAG(hash) (MIX(hash) >> 57)
#define IDX(hash) (MIX(hash) & 0x7FFFFFFFFFFFFFFF)
#define GROUP_SIZE 16

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


template <typename slot_type>
class raw_iterator
{
public:
    auto& operator*() const { return *at_slot_; }

    auto& operator->() const { return at_slot_; }

    auto& operator++()
    {
        do { ++at_slot_; ++at_tag_; } while (*at_tag_ < 0);
        return *this;
    }

    auto& operator==(const raw_iterator &o) 
    { return o.at_tag_ == at_tag_ && o.at_slot_ == at_slot_; 
    }

    auto& operator!=(const raw_iterator &o)
    { return o.at_tag_ != at_tag_ || o.at_slot_ != at_slot_;
    }

    raw_iterator(slot_type &s, int8_t *t)
        : at_tag_(t) { at_slot_ = &s; }

private:
    slot_type *at_slot_;
    int8_t *at_tag_;
};


template <class Policy>
class raw_map
{
    Policy policy_;

public:
    using slot_type = typename Policy::slot_type;
    using iterator = raw_iterator<slot_type>;
    using const_iterator = raw_iterator<const slot_type>;

    static constexpr size_t probe_step = Policy::probe_step;
    static constexpr size_t probe_jump = Policy::probe_jump;
    static constexpr float resize_factor = Policy::resize_factor;
    static constexpr size_t start_size = Policy::start_size;

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
    end()
    {   //             should be: END (0b11110000)
        return { NULL, tags_ + max_size_ };
    }

    inline iterator 
    iterator_at(size_t index)
    {
        return { data_[index], tags_ + index };
    }

    inline probe_seq<probe_jump, probe_step>
    probe(uint64_t hash_idx) 
    { 
        using probe_seq_ = probe_seq<probe_jump, probe_step>;
        return probe_seq_(hash_idx, max_size_ - 1); 
    }

    template <typename Key>
    inline size_t 
    find_offset_or_empty(const Key &key, size_t hash, size_t index)
    {
        auto group = neon_tag_match::load(tags_ + index);
        uint64_t mask = group.has_tag(TAG(hash));
        while (mask)
        {
            uint8_t offset = __builtin_ctzll(mask) >> 2;
            if (policy_.equal_to(data_[index + offset], key))
                return mask;
            mask &= (mask - 1);
        }
        return 0;
    }


    template <typename Key>
    std::pair<size_t, bool>
    find_or_prepare_insert(const Key &key, size_t hash)
    {
        size_t deleted_idx;
        bool found_deleted = false;
        auto seq = probe(IDX(hash));
        uint8_t tag = TAG(hash);
        while (true)
        {
            // Check for an existing key
            // TODO turn it into: for (idx : mask) {} - iterable bitmask
            auto group = neon_tag_match::load(tags_ + seq.index());
            uint64_t mask = group.has_tag(tag);
            while (mask)
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
                return { seq.index() + 
                            (found_deleted ? deleted_idx : 
                                    __builtin_ctzll(mask) >> 2), 
                         true };

            // Record the first deleted slot
            if ((mask = group.has_tag(DELETED)))
            {
                found_deleted = true;
                deleted_idx = seq.index() + (__builtin_ctzll(mask) >> 2);
            }
            seq.next();
        }
    }


    void
    insert_and_swap_unchecked(slot_type *data, int8_t *tags, 
            size_t src_idx, slot_type &&pair, size_t hash)
    {
        auto seq = probe(IDX(hash));
        while (true)
        {
            auto group = neon_tag_match::load(tags + seq.index());
            size_t mask = group.has_tag(EMPTY);
            if (mask)
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

        for (size_t i = 0; i < max_size_ >> 1; ++i)
            if (tags_[i] >= 0)
                insert_and_swap_unchecked(data, tags, i,
                        std::move(data_[i]), 
                        policy_.hash_key(Policy::get_key(data_[i])));

        destroy_fields();
        tags_ = tags;
        data_ = data;
    }

    template <typename... Args>
    std::pair<iterator, bool>
    do_emplace(uint64_t hash, Args&&... args)
    {
        if (cur_size_ >= max_size_ * resize_factor)
            resize();

        const auto &key = Policy::extract_key(args...);

        // { index, did it insert? }
        auto search = find_or_prepare_insert(key, hash);
        if (!search.second)
            return { iterator_at(search.first), false };

        Policy::construct(data_ + search.first, std::forward<Args>(args)...);
        tags_[search.first] = TAG(hash);
        cur_size_++;
        return { iterator_at(search.first), true };
    }

    raw_map() : cur_size_(0), max_size_(start_size)
    {
        init_fields(data_, tags_, start_size);
    }

// tags_ has capacity max_size_ + 1 for empty signal 
private:
    slot_type *data_; // key/value pairs (map) or keys (set)
    int8_t *tags_;    // metadata
    size_t cur_size_, max_size_;

    void init_fields(slot_type *&data, int8_t *&tags, size_t size)
    {
        tags = static_cast<int8_t *>(malloc((size + 1) * sizeof(int8_t)));
        data = static_cast<slot_type *>(operator new(size * sizeof(slot_type)));
        memset(tags, EMPTY, size * sizeof(int8_t));
        tags[size] = END;
    }

    void destroy_fields()
    {
        free(tags_);
        operator delete(data_);
    }

}; // class raw_map

}; // namespace internal

