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
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bcos::engine
{
namespace detail
{
/// RLP-encode the withdrawals list the way go-ethereum `rlp.Encode` does:
/// a nil slice and an empty slice are both the empty list `0xc0`. Each item
/// is `[index, validatorIndex, address (20B), amount]`. Lives in engine
/// (not framework) so it can use `bcos-codec` instead of a handwritten RLP
/// subset.
inline void encodeWithdrawalsRlp(
    bcos::bytes& out, std::optional<std::vector<WithdrawalV1>> const& withdrawals)
{
    bcos::bytes items;
    if (withdrawals.has_value())
    {
        for (auto const& w : *withdrawals)
        {
            codec::rlp::encode(items, w.index, w.validatorIndex, w.address, w.amount);
        }
    }
    codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = items.size()});
    out.insert(out.end(), items.begin(), items.end());
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
        std::array<uint8_t, 8> timestampBE{};
        bcos::toBigEndian(timestampSec, timestampBE);
        updateBytes(timestampBE.data(), timestampBE.size());
    }
    updateBytes(attrs.prevRandao.data(), attrs.prevRandao.size());
    updateBytes(attrs.suggestedFeeRecipient.data(), attrs.suggestedFeeRecipient.size());

    // op-geth BuildPayloadArgs.Id() always does rlp.Encode(hasher, args.Withdrawals).
    // Go RLP encodes both a nil slice and an empty slice as the empty list 0xc0, so
    // nullopt and empty-vector withdrawals hash identically (V1 attrs have no
    // withdrawals field; V2+ send []). Do not skip the encode when nullopt — that
    // would diverge from op-geth. codec::rlp::encode(optional) emits nothing for
    // nullopt, so the empty-list case is handled here explicitly.
    {
        bcos::bytes withdrawalsRlp;
        detail::encodeWithdrawalsRlp(withdrawalsRlp, attrs.withdrawals);
        updateBytes(withdrawalsRlp.data(), withdrawalsRlp.size());
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
        std::array<uint8_t, 8> txCountBE{};
        bcos::toBigEndian(txHashes.size(), txCountBE);
        updateBytes(txCountBE.data(), txCountBE.size());
        for (auto const& txHash : txHashes)
        {
            updateBytes(txHash.data(), txHash.size());
        }
    }

    if (attrs.gasLimit.has_value())
    {
        std::array<uint8_t, 8> gasLimitBE{};
        bcos::toBigEndian(*attrs.gasLimit, gasLimitBE);
        updateBytes(gasLimitBE.data(), gasLimitBE.size());
    }
    if (attrs.eip1559Params.has_value())
    {
        updateBytes(attrs.eip1559Params->data(), attrs.eip1559Params->size());
    }
    if (attrs.minBaseFee.has_value())
    {
        std::array<uint8_t, 8> minBaseFeeBE{};
        bcos::toBigEndian(*attrs.minBaseFee, minBaseFeeBE);
        updateBytes(minBaseFeeBE.data(), minBaseFeeBE.size());
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
