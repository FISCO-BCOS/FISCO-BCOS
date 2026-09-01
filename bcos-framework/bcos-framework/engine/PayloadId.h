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
 * @file PayloadId.h
 * @brief Deterministic payload-ID derivation, byte-aligned with op-geth's
 *        BuildPayloadArgs.Id() (miner/payload_building.go).
 */

#pragma once

#include "bcos-framework/engine/Types.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace bcos::engine
{
namespace detail
{
/// Local RLP subset for payload-ID hashing only. bcos-framework cannot depend on
/// bcos-codec (layering); these helpers match go-ethereum rlp.encBuffer / codec
/// RLPEncode.h for the withdrawals list that BuildPayloadArgs.Id() hashes.
/// Minimal big-endian encoding of an unsigned integer (no leading zero bytes;
/// zero -> empty). Mirrors RLP integer semantics used by the Ethereum clients.
inline void appendCompactBigEndian(bcos::bytes& out, u256 const& value)
{
    std::array<uint8_t, 32> buf{};
    u256 v = value;
    for (int i = 31; i >= 0; --i)
    {
        buf[static_cast<size_t>(i)] = static_cast<uint8_t>(v & 0xff);
        v >>= 8;
    }
    size_t start = 0;
    while (start < 32 && buf[start] == 0)
    {
        ++start;
    }
    out.insert(out.end(), buf.begin() + static_cast<ptrdiff_t>(start), buf.end());
}

/// RLP header for a payload of @p payloadLength bytes (@p isList selects the
/// list vs string base). Emits the short form (< 56 bytes) or the long form
/// (>= 56 bytes: base + size-of-length + length bytes), matching go-ethereum's
/// rlp.encBuffer: short form base 0x80/0xc0 + len; long form 0xb7/0xf7 +
/// len-of-len + big-endian length.
inline void rlpAppendHeader(bcos::bytes& out, bool isList, size_t payloadLength)
{
    if (payloadLength < 56)
    {
        out.push_back(static_cast<uint8_t>((isList ? 0xc0 : 0x80) + payloadLength));
        return;
    }
    bcos::bytes lengthBytes;
    appendCompactBigEndian(lengthBytes, u256{payloadLength});
    out.push_back(static_cast<uint8_t>((isList ? 0xf7 : 0xb7) + lengthBytes.size()));
    out.insert(out.end(), lengthBytes.begin(), lengthBytes.end());
}

/// RLP-encode one unsigned integer (integer semantics: 0 -> 0x80, single byte
/// < 0x80 -> itself, otherwise string header + minimal BE bytes).
inline void rlpAppendU256(bcos::bytes& out, u256 const& value)
{
    if (value == 0)
    {
        out.push_back(0x80);
        return;
    }
    bcos::bytes be;
    appendCompactBigEndian(be, value);
    if (be.size() == 1 && be[0] < 0x80)
    {
        out.push_back(be[0]);
        return;
    }
    rlpAppendHeader(out, false, be.size());
    out.insert(out.end(), be.begin(), be.end());
}

/// RLP-encode one WithdrawalV1: a 4-item list
/// [index, validatorIndex, address (20 bytes), amount].
inline void rlpAppendWithdrawal(bcos::bytes& out, WithdrawalV1 const& w)
{
    bcos::bytes item;
    rlpAppendU256(item, w.index);
    rlpAppendU256(item, w.validatorIndex);
    rlpAppendHeader(item, false, w.address.size());
    item.insert(item.end(), w.address.begin(), w.address.end());
    rlpAppendU256(item, w.amount);
    rlpAppendHeader(out, true, item.size());
    out.insert(out.end(), item.begin(), item.end());
}
}  // namespace detail

/// Derive an 8-byte payload ID from the payload attributes, byte-aligned with
/// op-geth's BuildPayloadArgs.Id() (miner/payload_building.go):
///
///   sha256(parentHash || timestamp(sec, u64 BE) || prevRandao ||
///          suggestedFeeRecipient || RLP(withdrawals) [|| parentBeaconBlockRoot]
///          [|| noTxPool || txCount(u64 BE) || txHash*]
///          [|| gasLimit(u64 BE)] [|| eip1559Params(8B)] [|| minBaseFee(u64 BE)])
///
/// then take the first 8 hash bytes and overwrite byte 0 with the Engine API
/// version (V1=0x1 .. V4=0x4), exactly like `copy(out[:], hasher.Sum(nil)[:8]);
/// out[0] = byte(args.Version)`.
///
/// Known divergence (documented, not a bug): op-geth also hashes an optional
/// `SlotNum` (u64 BE) between parentBeaconBlockRoot and the tx block
/// (flashblocks/Amsterdam extension). FISCO's PayloadAttributes has no
/// slotNumber field and op-node never sends one, so the field is never
/// written — byte-identical to op-geth with SlotNum == nil, which is the
/// only configuration this chain produces. If slotNumber is ever added to
/// PayloadAttributes it must be hashed here in the same position.
///
/// @param attrs    the payload attributes. attrs.timestamp is FISCO-internal
///                 milliseconds (EngineHelper converts Engine-API seconds to
///                 ms at the RPC boundary); op-geth hashes the Engine-API
///                 seconds, so it is divided by 1000 here.
/// @param parentHash the head block hash the payload builds on.
/// @param txHashes canonical keccak256 hashes of the attribute transactions,
///                 in order (the caller decodes raw envelopes to obtain them).
/// @param version  Engine API version byte (0x01..0x04).
/// @return the 8-byte payload ID as a hex string ("0x" + 16 hex digits),
///         matching the existing PayloadID = std::string representation.
inline std::string derivePayloadId(PayloadAttributes const& attrs, h256 const& parentHash,
    std::span<h256 const> txHashes, uint8_t version)
{
    using crypto::hasher::openssl::OpenSSL_SHA2_256_Hasher;
    OpenSSL_SHA2_256_Hasher hasher;
    hasher.init();

    auto updateBytes = [&hasher](auto const* data, size_t size) {
        hasher.update(std::span<std::byte const>{reinterpret_cast<std::byte const*>(data), size});
    };

    updateBytes(parentHash.data(), parentHash.size());
    // Timestamp as uint64 big-endian seconds.
    const uint64_t timestampSec = attrs.timestamp / 1000;
    {
        uint64_t ts = timestampSec;
        uint8_t timestampBE[8];
        for (int i = 7; i >= 0; --i)
        {
            timestampBE[static_cast<size_t>(i)] = static_cast<uint8_t>(ts & 0xff);
            ts >>= 8;
        }
        updateBytes(timestampBE, sizeof(timestampBE));
    }
    updateBytes(attrs.prevRandao.data(), attrs.prevRandao.size());
    updateBytes(attrs.suggestedFeeRecipient.data(), attrs.suggestedFeeRecipient.size());

    // RLP list of withdrawals; empty list is 0xc0.
    // Header length depends on the item payload, so encode items first, then stream
    // the header and items into the hasher without a third concatenated buffer.
    {
        bcos::bytes withdrawalsPayload;
        if (attrs.withdrawals.has_value())
        {
            for (auto const& w : *attrs.withdrawals)
            {
                detail::rlpAppendWithdrawal(withdrawalsPayload, w);
            }
        }
        bcos::bytes withdrawalsHeader;
        detail::rlpAppendHeader(withdrawalsHeader, true, withdrawalsPayload.size());
        updateBytes(withdrawalsHeader.data(), withdrawalsHeader.size());
        updateBytes(withdrawalsPayload.data(), withdrawalsPayload.size());
    }

    if (attrs.parentBeaconBlockRoot.has_value())
    {
        updateBytes(attrs.parentBeaconBlockRoot->data(), attrs.parentBeaconBlockRoot->size());
    }

    // If noTxPool or any txs: noTxPool byte, tx count (u64 BE), then each tx hash.
    const bool noTxPool = attrs.noTxPool.value_or(false);
    if (noTxPool || !txHashes.empty())
    {
        const uint8_t noTxPoolByte = noTxPool ? 1 : 0;
        updateBytes(&noTxPoolByte, 1);
        uint8_t txCountBE[8];
        uint64_t txCount = txHashes.size();
        for (int i = 7; i >= 0; --i)
        {
            txCountBE[static_cast<size_t>(i)] = static_cast<uint8_t>(txCount & 0xff);
            txCount >>= 8;
        }
        updateBytes(txCountBE, sizeof(txCountBE));
        for (auto const& txHash : txHashes)
        {
            updateBytes(txHash.data(), txHash.size());
        }
    }

    if (attrs.gasLimit.has_value())
    {
        uint8_t gasLimitBE[8];
        uint64_t gasLimit = *attrs.gasLimit;
        for (int i = 7; i >= 0; --i)
        {
            gasLimitBE[static_cast<size_t>(i)] = static_cast<uint8_t>(gasLimit & 0xff);
            gasLimit >>= 8;
        }
        updateBytes(gasLimitBE, sizeof(gasLimitBE));
    }
    if (attrs.eip1559Params.has_value())
    {
        updateBytes(attrs.eip1559Params->data(), attrs.eip1559Params->size());
    }
    if (attrs.minBaseFee.has_value())
    {
        uint8_t minBaseFeeBE[8];
        uint64_t minBaseFee = *attrs.minBaseFee;
        for (int i = 7; i >= 0; --i)
        {
            minBaseFeeBE[static_cast<size_t>(i)] = static_cast<uint8_t>(minBaseFee & 0xff);
            minBaseFee >>= 8;
        }
        updateBytes(minBaseFeeBE, sizeof(minBaseFeeBE));
    }

    std::array<uint8_t, 32> digest{};
    hasher.final(std::span<std::byte>{reinterpret_cast<std::byte*>(digest.data()), digest.size()});

    // copy(out[:], hasher.Sum(nil)[:8]); out[0] = byte(args.Version)
    std::array<uint8_t, 8> out{};
    std::copy(digest.begin(), digest.begin() + 8, out.begin());
    out[0] = version;

    return bcos::toHex(out, "0x");
}
}  // namespace bcos::engine
