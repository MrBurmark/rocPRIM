// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef ROCPRIM_DEVICE_DETAIL_DEVICE_RADIX_SORT_HPP_
#define ROCPRIM_DEVICE_DETAIL_DEVICE_RADIX_SORT_HPP_

#include <type_traits>

#include "../../detail/config.hpp"
#include "../../detail/various.hpp"
#include "../../detail/radix_sort.hpp"

#include "../../intrinsics.hpp"
#include "../../functional.hpp"
#include "../../types.hpp"

#include "../../block/block_discontinuity.hpp"
#include "../../block/block_exchange.hpp"
#include "../../block/block_load.hpp"
#include "../../block/block_load_func.hpp"
#include "../../block/block_scan.hpp"
#include "../../block/block_radix_sort.hpp"

BEGIN_ROCPRIM_NAMESPACE

namespace detail
{

template<
    unsigned int BlockSize,
    unsigned int ItemsPerThread,
    unsigned int RadixBits,
    bool DescendingIn,
    class KeyIn
>
__device__
void fill_digit_counts(const KeyIn * keys_input,
                       unsigned int size,
                       unsigned int * batch_digit_counts,
                       unsigned int bit,
                       unsigned int current_radix_bits,
                       unsigned int blocks_per_full_batch,
                       unsigned int full_batches)
{
    constexpr unsigned int items_per_block = BlockSize * ItemsPerThread;
    constexpr unsigned int radix_size = 1 << RadixBits;

    constexpr unsigned int warp_size =::rocprim::warp_size();
    constexpr unsigned int warps_no = BlockSize / warp_size;
    static_assert(BlockSize % warp_size == 0, "BlockSize must be divisible by warp size");
    static_assert(radix_size <= BlockSize, "Radix size must not exceed BlockSize");

    using key_in_codec = radix_key_codec<KeyIn, DescendingIn>;
    using bit_key_type = typename key_in_codec::bit_key_type;

    __shared__ struct
    {
        unsigned int digit_counts[warps_no][radix_size];
    } storage;

    const unsigned int radix_mask = (1u << current_radix_bits) - 1;

    const unsigned int flat_id = ::rocprim::flat_block_thread_id();
    const unsigned int batch_id = ::rocprim::flat_block_id();
    const unsigned int warp_id = ::rocprim::warp_id();

    if(flat_id < radix_size)
    {
        for(unsigned int w = 0; w < warps_no; w++)
        {
            storage.digit_counts[w][flat_id] = 0;
        }
    }
    ::rocprim::syncthreads();

    unsigned int block_offset;
    unsigned int blocks_per_batch;
    if(batch_id < full_batches)
    {
        blocks_per_batch = blocks_per_full_batch;
        block_offset = batch_id * blocks_per_batch;
    }
    else
    {
        blocks_per_batch = blocks_per_full_batch - 1;
        block_offset = batch_id * blocks_per_batch + full_batches;
    }
    block_offset *= items_per_block;

    for(unsigned int bi = 0; bi < blocks_per_batch; bi++)
    {
        KeyIn keys[ItemsPerThread];
        unsigned int valid_count;
        // Use loading into a striped arrangement because an order of items is irrelevant,
        // only totals matter
        if(block_offset + items_per_block <= size)
        {
            valid_count = items_per_block;
            block_load_direct_striped<BlockSize>(flat_id, keys_input + block_offset, keys);
        }
        else
        {
            valid_count = size - block_offset;
            block_load_direct_striped<BlockSize>(flat_id, keys_input + block_offset, keys, valid_count);
        }
        bit_key_type bit_keys[ItemsPerThread];
        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            bit_keys[i] = key_in_codec::encode(keys[i]);
        }

        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            const unsigned int digit = (bit_keys[i] >> bit) & radix_mask;
            const unsigned int pos = i * BlockSize + flat_id;
            unsigned long long same_digit_lanes_mask = ::rocprim::ballot(pos < valid_count);
            for(unsigned int b = 0; b < RadixBits; b++)
            {
                const unsigned int bit_set = digit & (1u << b);
                const unsigned long long bit_set_mask = ::rocprim::ballot(bit_set);
                same_digit_lanes_mask &= (bit_set ? bit_set_mask : ~bit_set_mask);
            }
            const unsigned int same_digit_count = ::rocprim::bit_count(same_digit_lanes_mask);
            const unsigned int prev_same_digit_count = ::rocprim::masked_bit_count(same_digit_lanes_mask);
            if(prev_same_digit_count == 0)
            {
                // Write the number of lanes having this digit,
                // if the current lane is the first (and maybe only) lane with this digit.
                storage.digit_counts[warp_id][digit] += same_digit_count;
            }
        }

        block_offset += items_per_block;
    }
    ::rocprim::syncthreads();

    if(flat_id < radix_size)
    {
        unsigned int digit_count = 0;
        for(unsigned int w = 0; w < warps_no; w++)
        {
            digit_count += storage.digit_counts[w][flat_id];
        }
        batch_digit_counts[batch_id * radix_size + flat_id] = digit_count;
    }
}

template<
    unsigned int BlockSize,
    unsigned int ItemsPerThread,
    unsigned int RadixBits
>
__device__
void scan_batches(unsigned int * batch_digit_counts,
                  unsigned int * digit_counts,
                  unsigned int batches)
{
    constexpr unsigned int radix_size = 1 << RadixBits;

    using scan_type = typename ::rocprim::block_scan<unsigned int, BlockSize>;

    const unsigned int digit = ::rocprim::flat_block_id();
    const unsigned int flat_id = ::rocprim::flat_block_thread_id();

    unsigned int values[ItemsPerThread];
    for(unsigned int i = 0; i < ItemsPerThread; i++)
    {
        const unsigned int batch_id = flat_id * ItemsPerThread + i;
        values[i] = (batch_id < batches ? batch_digit_counts[batch_id * radix_size + digit] : 0);
    }

    unsigned int digit_count;
    scan_type().exclusive_scan(values, values, 0, digit_count);

    for(unsigned int i = 0; i < ItemsPerThread; i++)
    {
        const unsigned int batch_id = flat_id * ItemsPerThread + i;
        if(batch_id < batches)
        {
            batch_digit_counts[batch_id * radix_size + digit] = values[i];
        }
    }

    if(flat_id == 0)
    {
        digit_counts[digit] = digit_count;
    }
}

template<unsigned int RadixBits>
__device__
void scan_digits(unsigned int * digit_counts)
{
    constexpr unsigned int radix_size = 1 << RadixBits;

    using scan_type = typename ::rocprim::block_scan<unsigned int, radix_size>;

    const unsigned int flat_id = ::rocprim::flat_block_thread_id();

    unsigned int value = digit_counts[flat_id];
    scan_type().exclusive_scan(value, value, 0);
    digit_counts[flat_id] = value;
}

// Wrapping functions that allow to call proper methods (with or without values)
// (a variant with values is enabled only when Value is not empty_type)
template<class Sort, class Key, class Value, unsigned int ItemsPerThread>
__device__ inline
void sort_block(Sort sort,
                Key (&keys)[ItemsPerThread],
                Value (&values)[ItemsPerThread],
                typename Sort::storage_type& storage,
                unsigned int begin_bit,
                unsigned int end_bit)
{
    sort.sort(keys, values, storage, begin_bit, end_bit);
}

template<class Sort, class Key, unsigned int ItemsPerThread>
__device__ inline
void sort_block(Sort sort,
                Key (&keys)[ItemsPerThread],
                ::rocprim::empty_type (&values)[ItemsPerThread],
                typename Sort::storage_type& storage,
                unsigned int begin_bit,
                unsigned int end_bit)
{
    (void) values;
    sort.sort(keys, storage, begin_bit, end_bit);
}

template<
    unsigned int BlockSize,
    unsigned int ItemsPerThread,
    unsigned int RadixBits,
    bool DescendingIn,
    bool DescendingOut,
    class KeyIn,
    class KeyOut,
    class Value
>
__device__
void sort_and_scatter(const KeyIn * keys_input,
                      KeyOut * keys_output,
                      const Value * values_input,
                      Value * values_output,
                      unsigned int size,
                      const unsigned int * batch_digit_starts,
                      const unsigned int * digit_starts,
                      unsigned int bit,
                      unsigned int current_radix_bits,
                      unsigned int blocks_per_full_batch,
                      unsigned int full_batches)
{
    constexpr unsigned int items_per_block = BlockSize * ItemsPerThread;
    constexpr unsigned int radix_size = 1 << RadixBits;
    constexpr bool with_values = !std::is_same<Value, ::rocprim::empty_type>::value;

    using key_in_codec = radix_key_codec<KeyIn, DescendingIn>;
    using key_out_codec = radix_key_codec<KeyOut, DescendingOut>;
    using bit_key_type = typename key_in_codec::bit_key_type;
    using keys_load_type = ::rocprim::block_load<
        KeyIn, BlockSize, ItemsPerThread,
        ::rocprim::block_load_method::block_load_transpose>;
    using values_load_type = ::rocprim::block_load<
        Value, BlockSize, ItemsPerThread,
        ::rocprim::block_load_method::block_load_transpose>;
    using sort_type = ::rocprim::block_radix_sort<bit_key_type, BlockSize, ItemsPerThread, Value>;
    using discontinuity_type = ::rocprim::block_discontinuity<unsigned int, BlockSize>;
    using bit_keys_exchange_type = ::rocprim::block_exchange<bit_key_type, BlockSize, ItemsPerThread>;
    using values_exchange_type = ::rocprim::block_exchange<Value, BlockSize, ItemsPerThread>;

    __shared__ struct
    {
        union
        {
            typename keys_load_type::storage_type keys_load;
            typename values_load_type::storage_type values_load;
            typename sort_type::storage_type sort;
            typename discontinuity_type::storage_type discontinuity;
            typename bit_keys_exchange_type::storage_type bit_keys_exchange;
            typename values_exchange_type::storage_type values_exchange;
        };

        unsigned short starts[radix_size];
        unsigned short ends[radix_size];

        unsigned int block_starts[radix_size];
    } storage;

    const unsigned int radix_mask = (1u << current_radix_bits) - 1;

    const unsigned int flat_id = ::rocprim::flat_block_thread_id();
    const unsigned int batch_id = ::rocprim::flat_block_id();

    if(flat_id < radix_size)
    {
        storage.block_starts[flat_id] = digit_starts[flat_id] + batch_digit_starts[batch_id * radix_size + flat_id];
    }

    unsigned int block_offset;
    unsigned int blocks_per_batch;
    if(batch_id < full_batches)
    {
        blocks_per_batch = blocks_per_full_batch;
        block_offset = batch_id * blocks_per_batch;
    }
    else
    {
        blocks_per_batch = blocks_per_full_batch - 1;
        block_offset = batch_id * blocks_per_batch + full_batches;
    }
    block_offset *= items_per_block;

    for(unsigned int bi = 0; bi < blocks_per_batch; bi++)
    {
        KeyIn keys[ItemsPerThread];
        Value values[ItemsPerThread];
        unsigned int valid_count;
        if(block_offset + items_per_block <= size)
        {
            valid_count = items_per_block;
            keys_load_type().load(keys_input + block_offset, keys, storage.keys_load);
            if(with_values)
            {
                ::rocprim::syncthreads();
                values_load_type().load(values_input + block_offset, values, storage.values_load);
            }
        }
        else
        {
            valid_count = size - block_offset;
            // Sort will leave "invalid" (out of size) items at the end of the sorted sequence
            const KeyIn out_of_bounds = key_in_codec::decode(bit_key_type(-1));
            keys_load_type().load(keys_input + block_offset, keys, valid_count, out_of_bounds, storage.keys_load);
            if(with_values)
            {
                ::rocprim::syncthreads();
                values_load_type().load(values_input + block_offset, values, valid_count, storage.values_load);
            }
        }
        bit_key_type bit_keys[ItemsPerThread];
        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            bit_keys[i] = key_in_codec::encode(keys[i]);
        }

        if(flat_id < radix_size)
        {
            storage.starts[flat_id] = valid_count;
            storage.ends[flat_id] = valid_count;
        }

        ::rocprim::syncthreads();
        sort_block(sort_type(), bit_keys, values, storage.sort, bit, bit + current_radix_bits);

        unsigned int digits[ItemsPerThread];
        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            digits[i] = (bit_keys[i] >> bit) & radix_mask;
        }

        bool head_flags[ItemsPerThread];
        bool tail_flags[ItemsPerThread];
        ::rocprim::not_equal_to<unsigned int> flag_op;

        ::rocprim::syncthreads();
        discontinuity_type().flag_heads_and_tails(head_flags, tail_flags, digits, flag_op, storage.discontinuity);

        // Fill start and end position of subsequence for every digit
        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            const unsigned int digit = digits[i];
            const unsigned int pos = flat_id * ItemsPerThread + i;
            if(head_flags[i])
            {
                storage.starts[digit] = pos;
            }
            if(tail_flags[i])
            {
                storage.ends[digit] = pos;
            }
        }

        ::rocprim::syncthreads();
        // Rearrange to striped arrangement to have faster coalesced writes instead of
        // scattering of blocked-arranged items
        bit_keys_exchange_type().blocked_to_striped(bit_keys, bit_keys, storage.bit_keys_exchange);
        if(with_values)
        {
            ::rocprim::syncthreads();
            values_exchange_type().blocked_to_striped(values, values, storage.values_exchange);
        }

        for(unsigned int i = 0; i < ItemsPerThread; i++)
        {
            const unsigned int digit = (bit_keys[i] >> bit) & radix_mask;
            const unsigned int pos = i * BlockSize + flat_id;
            if(pos < valid_count)
            {
                const unsigned int dst = pos - storage.starts[digit] + storage.block_starts[digit];
                keys_output[dst] = key_out_codec::decode(bit_keys[i]);
                if(with_values)
                {
                    values_output[dst] = values[i];
                }
            }
        }

        ::rocprim::syncthreads();

        // Accumulate counts of the current block
        if(flat_id < radix_size)
        {
            const unsigned int digit = flat_id;
            const unsigned int start = storage.starts[digit];
            const unsigned int end = storage.ends[digit];
            if(start < valid_count)
            {
                storage.block_starts[digit] += (::rocprim::min(valid_count - 1, end) - start + 1);
            }
        }

        block_offset += items_per_block;
    }
}

} // end namespace detail

END_ROCPRIM_NAMESPACE

#endif // ROCPRIM_DEVICE_DETAIL_DEVICE_RADIX_SORT_HPP_
