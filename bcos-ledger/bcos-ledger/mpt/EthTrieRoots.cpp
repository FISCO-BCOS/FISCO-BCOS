/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthTrieRoots.cpp
 * @brief EthTrieRoots — index-keyed (non-secure) trie root computation
 * @date 2026/8/18
 */
#include "EthTrieRoots.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <charconv>
#include <cstdint>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

bcos::h256 computeIndexedTrieRoot(std::span<bcos::bytesConstRef const> items)
{
    if (items.empty())
    {
        return emptyRootHash();
    }

    // Key each item by its RLP-encoded index, packed into ONE flat buffer (an
    // rlp(uint64) key is at most 9 bytes) and referenced from the ref-pair vector.
    // The scratch `key` is hoisted out of the loop and cleared each iteration — a
    // cleared std::vector keeps its capacity, so the per-key encode reuses one
    // buffer instead of heap-allocating per item. Two O(N) index vectors remain
    // (keySpans + keyRefs), plus one nibble-path allocation per entry inside
    // computeRawTrieRootImpl. The values stay VIEWS into the caller's `items`,
    // which outlive the call; computeRawTrieRoot sorts by ENCODED KEY BYTES
    // internally (rlp(0)=0x80 > rlp(1)=0x01, so NOT numeric index order), so no
    // ordering or value copying is needed here. The root-only entry point is used
    // deliberately: the tx/receipt/withdrawal tries are never persisted, so
    // accumulating the node map would only be thrown away.
    bcos::bytes keyBytes;
    keyBytes.reserve(items.size() * 2);
    std::vector<std::pair<size_t, size_t>> keySpans;  // (offset, length) into keyBytes
    keySpans.reserve(items.size());
    bcos::bytes key;
    for (size_t i = 0; i < items.size(); ++i)
    {
        key.clear();
        codec::rlp::encode(key, static_cast<uint64_t>(i));
        keySpans.emplace_back(keyBytes.size(), key.size());
        keyBytes.insert(keyBytes.end(), key.begin(), key.end());
    }
    std::vector<std::pair<bcos::bytesConstRef, bcos::bytesConstRef>> keyRefs;
    keyRefs.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto [offset, length] = keySpans[i];
        keyRefs.emplace_back(bcos::bytesConstRef(keyBytes.data() + offset, length), items[i]);
    }
    return computeRawTrieRoot(keyRefs);
}

namespace
{
/// Parse cumulativeGasUsed: 0x-hex via safeFromQuantity, otherwise decimal. Bare digits must
/// not go to safeFromQuantity (it treats them as hex).
[[nodiscard]] uint64_t parseCumulativeGasUsed(std::string_view s)
{
    if (s.size() > 1 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
    {
        if (auto v = bcos::safeFromQuantity(s))
        {
            return *v;
        }
    }
    else
    {
        uint64_t value = 0;
        auto const* const begin = s.data();
        auto const* const end = begin + s.size();
        auto const [ptr, ec] = std::from_chars(begin, end, value, 10);
        if (ec == std::errc{} && ptr == end)
        {
            return value;
        }
    }
    BOOST_THROW_EXCEPTION(
        EthReceiptEncodeError{} << bcos::errinfo_comment(
            "receipt cumulativeGasUsed is neither hex nor decimal: " + std::string(s)));
}

/// Patch a placeholder list-header byte at headerPos with the canonical header for payloadLen
/// (short form for < 56 bytes, long form otherwise — the long form inserts the length bytes,
/// shifting the already-written payload once per list, not once per field).
void patchRlpListHeader(bcos::bytes& buf, size_t headerPos, size_t payloadLen)
{
    if (payloadLen < 56)
    {
        buf[headerPos] = static_cast<bcos::byte>(0xc0 + payloadLen);
        return;
    }
    bcos::bytes lenBytes;
    auto v = payloadLen;
    while (v > 0)
    {
        lenBytes.insert(lenBytes.begin(), static_cast<bcos::byte>(v & 0xff));
        v >>= 8;
    }
    buf[headerPos] = static_cast<bcos::byte>(0xf7 + lenBytes.size());
    buf.insert(
        buf.begin() + static_cast<ptrdiff_t>(headerPos) + 1, lenBytes.begin(), lenBytes.end());
}

/// RLP list of logs: [address, [topics...], data] each, whole collection wrapped in a list
/// (byte-identical to evmone's rlp::encode_container over vector<Log>). Writes each log's
/// bytes once, with header backfill — no per-log intermediate buffers.
void encodeLogsList(bcos::bytes& to, gsl::span<const bcos::protocol::LogEntry> logs)
{
    auto const listStart = to.size();
    to.push_back(0xc0);  // placeholder (patched below)
    auto const payloadStart = to.size();
    for (const auto& log : logs)
    {
        auto const logStart = to.size();
        to.push_back(0xc0);  // placeholder for this log's list header
        bcos::codec::rlp::encode(to, log.address());
        auto const topicsStart = to.size();
        to.push_back(0xc0);  // placeholder for the topics list header
        for (const auto& topic : log.topics())
        {
            bcos::codec::rlp::encode(to, topic);
        }
        patchRlpListHeader(to, topicsStart, to.size() - topicsStart - 1);
        bcos::codec::rlp::encode(to, log.data());
        patchRlpListHeader(to, logStart, to.size() - logStart - 1);
    }
    patchRlpListHeader(to, listStart, to.size() - payloadStart);
}
}  // namespace

bcos::bytes encodeReceiptLeaf(
    bcos::protocol::TransactionReceipt const& receipt, std::uint8_t txType)
{
    constexpr std::uint8_t c_legacyTxType = 0x00;
    constexpr std::uint8_t c_depositTxType = 0x7e;

    // RLP bool semantics: true -> 0x01, false -> 0x80 (raw push; the UnsignedByte encode path
    // mis-handles bool). Payload = rlp([status, cumGas, bloom, logs]) + (deposit) [nonce, version].
    bool const success = (receipt.status() == 0);
    uint64_t const cumGas = parseCumulativeGasUsed(receipt.cumulativeGasUsed());
    auto const bloom = receipt.logsBloom();
    if (bloom.size() != 256)
    {
        BOOST_THROW_EXCEPTION(
            EthReceiptEncodeError{} << bcos::errinfo_comment(
                "receipt logsBloom must be 256 bytes, got " + std::to_string(bloom.size())));
    }

    bcos::bytes payload;
    payload.push_back(success ? 0x01 : 0x80);
    codec::rlp::encode(payload, cumGas);
    codec::rlp::encode(payload, bloom);
    encodeLogsList(payload, receipt.logEntries());

    if (txType == c_depositTxType)
    {
        auto const& meta = receipt.opStackMeta();
        if (!meta || !meta->deposit_nonce || !meta->deposit_receipt_version)
        {
            BOOST_THROW_EXCEPTION(
                EthReceiptEncodeError{} << bcos::errinfo_comment(
                    "deposit receipt is missing its deposit nonce/receipt version"));
        }
        codec::rlp::encode(payload, *meta->deposit_nonce);
        codec::rlp::encode(payload, *meta->deposit_receipt_version);
    }

    bcos::bytes out;
    out.reserve(payload.size() + 4);
    // Typed raw-byte prefix (legacy has none; deposit's 0x7e is the EIP-2718 type byte).
    if (txType != c_legacyTxType)
    {
        out.push_back(txType);
    }
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace bcos::ledger::mpt
