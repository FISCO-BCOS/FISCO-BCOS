// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "bloom_filter.hpp"
#include "transaction.hpp"

namespace evmone::state
{

namespace
{
/// Adds an entry to the bloom filter.
/// based on
/// https://ethereum.github.io/execution-specs/autoapi/ethereum/shanghai/bloom/index.html#add-to-bloom
inline void add_to(BloomFilter& bf, const bytes_view& entry)
{
    const auto hash = keccak256(entry);

    // take the least significant 11-bits of the first three 16-bit values
    for (const auto i : {0, 2, 4})
    {
        const auto bit_to_set = ((hash.bytes[i] & 0x07) << 8) | hash.bytes[i + 1];
        const auto bit_index = 0x07FF - bit_to_set;
        const auto byte_index = bit_index / 8;
        const auto bit_pos = static_cast<uint8_t>(1 << (7 - (bit_index % 8)));
        bf.bytes[byte_index] |= bit_pos;
    }
}

}  // namespace

BloomFilter compute_bloom_filter(std::span<const Log> logs) noexcept
{
    BloomFilter res;
    for (const auto& log : logs)
    {
        add_to(res, log.addr);
        for (const auto& topic : log.topics)
            add_to(res, topic);
    }

    return res;
}

BloomFilter compute_bloom_filter(std::span<const TransactionReceipt> receipts) noexcept
{
    BloomFilter res;

    for (const auto& r : receipts)
        std::transform(
            res.bytes, std::end(res.bytes), r.logs_bloom_filter.bytes, res.bytes, std::bit_or<>());

    return res;
}

BloomFilter bloom_filter_from_bytes(const bytes_view& data) noexcept
{
    assert(data.size() == 256);
    BloomFilter res;
    std::ranges::copy(data, res.bytes);
    return res;
}

}  // namespace evmone::state
