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
 * @file HeaderValidator.h
 * @brief Ethereum PoS header validation (EIP-1559 base fee, EIP-4844 blob gas,
 *        gas-limit bounds, difficulty/uncle/extra-data rules).
 * @date 2026/8/18
 */
#pragma once

#include "Block.h"
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-utilities/Common.h>
#include <string>

namespace bcos::devp2p::sync
{
// Consensus constants (EIP-1559 / EIP-4844 / PoS).
constexpr uint64_t kMinGasLimit = 5000;
constexpr uint64_t kGasLimitBoundDivisor = 1024;
constexpr uint64_t kElasticityMultiplier = 2;
constexpr uint64_t kBaseFeeMaxChangeDenominator = 8;
constexpr u256 kInitialBaseFee{1000000000};  // 1 gwei
constexpr uint64_t kMaxExtraDataSize = 32;
constexpr uint64_t kGasPerBlob = 1U << 17;  // 131072

// EIP-7840 blob schedule (target/max blob gas per block) per fork. Keep in sync
// with CANCUN_BLOB_PARAMS / PRAGUE_BLOB_PARAMS in
// ethereum-executor/EthereumTransition.h.
struct BlobSchedule
{
    uint64_t targetBlobGas;
    uint64_t maxBlobGas;
};
constexpr BlobSchedule kCancunBlobSchedule{3 * kGasPerBlob, 6 * kGasPerBlob};
constexpr BlobSchedule kPragueBlobSchedule{6 * kGasPerBlob, 9 * kGasPerBlob};

// Minimal chain configuration for header validation (timestamp-based forks).
struct ChainConfig
{
    uint64_t chainId{1};
    // Fork activation timestamps; 0 means "active from genesis".
    uint64_t londonTime{0};
    uint64_t shanghaiTime{0};
    uint64_t cancunTime{0};
    uint64_t pragueTime{0};
    // First PoS (merge) block number. Blocks below it are PoW and keep their
    // PoW difficulty/ommers; blocks at or above it must satisfy PoS rules
    // (difficulty == 0, no ommers). 0 (default) means "no merge yet": every
    // block is validated as PoS (the pre-merge-PoW test-chain default).
    uint64_t mergeBlock{0};
    u256 initialBaseFee{kInitialBaseFee};
};

struct HeaderValidationResult
{
    bool valid{true};
    std::string error;
};

// True if a fork activating at `_forkTime` activates in the block with the
// given parent/header timestamps.
inline bool isForkBlock(uint64_t _forkTime, int64_t _parentTimestamp, int64_t _headerTimestamp)
{
    if (_forkTime == 0)
    {
        return false;  // active from genesis
    }
    return static_cast<uint64_t>(_parentTimestamp) < _forkTime &&
           static_cast<uint64_t>(_headerTimestamp) >= _forkTime;
}

inline bool isForkActive(uint64_t _forkTime, int64_t _timestamp)
{
    return _forkTime == 0 || static_cast<uint64_t>(_timestamp) >= _forkTime;
}

// EIP-7840: the blob schedule in effect for the block at `_timestamp`.
// TODO: Osaka (EIP-7918) also changes the excess-blob-gas update rule (base-fee
// floor); add an osakaTime field to ChainConfig and branch here once the
// initializer can supply the Osaka activation time.
inline BlobSchedule blobScheduleFor(ChainConfig const& _config, int64_t _timestamp)
{
    if (isForkActive(_config.pragueTime, _timestamp))
    {
        return kPragueBlobSchedule;
    }
    return kCancunBlobSchedule;
}

// EIP-1559: the base fee of the next block (the block after `_parent`).
inline u256 computeNextBaseFee(bcos::protocol::EthBlockHeaderData const& _parent)
{
    auto parentGasTarget = _parent.gasLimit / kElasticityMultiplier;
    u256 expected = _parent.baseFee.value_or(0);
    if (_parent.gasUsed == parentGasTarget)
    {
        return expected;
    }
    if (_parent.gasUsed > parentGasTarget)
    {
        auto delta = expected * (_parent.gasUsed - parentGasTarget) / parentGasTarget /
                     kBaseFeeMaxChangeDenominator;
        return expected + (delta > 1 ? delta : 1);
    }
    auto delta = expected * (parentGasTarget - _parent.gasUsed) / parentGasTarget /
                 kBaseFeeMaxChangeDenominator;
    return expected > delta ? expected - delta : 0;
}

// EIP-4844: the excess blob gas of the next block (the block after `_parent`),
// under the blob schedule active at that block.
inline u256 computeNextExcessBlobGas(
    bcos::protocol::EthBlockHeaderData const& _parent, BlobSchedule const& _schedule)
{
    u256 parentExcess = _parent.excessBlobGas.value_or(0);
    u256 parentBlobGasUsed = _parent.blobGasUsed.value_or(0);
    u256 total = parentBlobGasUsed + parentExcess;
    return total > _schedule.targetBlobGas ? total - _schedule.targetBlobGas : 0;
}

namespace detail
{
inline std::optional<std::string> validateBaseFee(
    bcos::protocol::EthBlockHeaderData const& _header,
    bcos::protocol::EthBlockHeaderData const& _parent, ChainConfig const& _config)
{
    bool londonActive = isForkActive(_config.londonTime, _header.timestamp);
    if (londonActive && !_header.baseFee.has_value())
    {
        return "missing baseFeePerGas (London active)";
    }
    if (_header.baseFee.has_value() && !_parent.baseFee.has_value())
    {
        // Parent pre-London: only valid on the London activation block.
        if (!isForkBlock(_config.londonTime, _parent.timestamp, _header.timestamp))
        {
            return "baseFeePerGas present but the parent is pre-London";
        }
        if (*_header.baseFee != _config.initialBaseFee)
        {
            return "baseFeePerGas must equal the initial base fee at London";
        }
    }
    else if (_header.baseFee.has_value() && _parent.baseFee.has_value())
    {
        if (*_header.baseFee != computeNextBaseFee(_parent))
        {
            return "baseFeePerGas does not match the EIP-1559 recomputation";
        }
    }
    return std::nullopt;
}

inline std::optional<std::string> validateBlobGas(
    bcos::protocol::EthBlockHeaderData const& _header,
    bcos::protocol::EthBlockHeaderData const& _parent, ChainConfig const& _config)
{
    auto const schedule = blobScheduleFor(_config, _header.timestamp);
    if (_header.blobGasUsed.has_value())
    {
        if (*_header.blobGasUsed > schedule.maxBlobGas ||
            *_header.blobGasUsed % kGasPerBlob != 0)
        {
            return "invalid blobGasUsed";
        }
    }
    if (_header.excessBlobGas.has_value())
    {
        if (!_parent.excessBlobGas.has_value())
        {
            // Parent pre-Cancun: only valid on the Cancun activation block (excess = 0).
            if (!isForkBlock(_config.cancunTime, _parent.timestamp, _header.timestamp))
            {
                return "excessBlobGas present but the parent is pre-Cancun";
            }
            if (*_header.excessBlobGas != 0)
            {
                return "excessBlobGas must be zero at Cancun activation";
            }
        }
        else if (*_header.excessBlobGas != computeNextExcessBlobGas(_parent, schedule))
        {
            return "excessBlobGas does not match the EIP-4844 recomputation";
        }
    }
    return std::nullopt;
}
}  // namespace detail

// Validate `_header` against its parent per Ethereum consensus rules.
// Chain-continuity (number/parentHash) is expected to have been checked by the
// caller; this focuses on the per-header field rules. Blocks before the merge
// (PoW, `number < mergeBlock`) follow PoW rules — difficulty is non-zero and
// ommers are allowed — while merged blocks follow PoS rules.
inline HeaderValidationResult validateHeaderPoS(
    bcos::protocol::EthBlockHeaderData const& _header,
    bcos::protocol::EthBlockHeaderData const& _parent, ChainConfig const& _config)
{
    const bool pos = _config.mergeBlock == 0 ||
                     static_cast<uint64_t>(_header.number) >= _config.mergeBlock;
    if (pos)
    {
        // PoS: difficulty must be zero and no ommers.
        if (_header.difficulty != 0)
        {
            return {false, "PoS difficulty must be zero"};
        }
        if (_header.uncleHash != emptyOmmersHash())
        {
            return {false, "PoS blocks must have no ommers"};
        }
    }
    else
    {
        // PoW: on a plain PoW chain difficulty is always non-zero. EXCEPTION —
        // Terminal Total Difficulty (TTD): on merge chains (Sepolia: TTD 17e15,
        // reached at block 1450409), the first block at/after the TTD carries
        // difficulty 0 while still being pre-merge (PoW) until the merge block
        // (1735371). So a zero difficulty is legal here; ommers remain allowed.
    }
    // Extra-data vanity bound.
    if (_header.extraData.size() > kMaxExtraDataSize)
    {
        return {false, "extraData exceeds the 32-byte PoS bound"};
    }
    // Number continuity and strictly-increasing timestamp.
    if (_header.number != _parent.number + 1)
    {
        return {false, "header number must equal the parent number + 1"};
    }
    if (_header.timestamp <= _parent.timestamp)
    {
        return {false, "timestamp must be strictly greater than the parent"};
    }
    // Gas limit bounds: >= MIN_GAS_LIMIT and |Δ| < parent / 1024.
    if (_header.gasLimit < kMinGasLimit)
    {
        return {false, "gasLimit below the 5000 minimum"};
    }
    u256 parentGasLimit = _parent.gasLimit;
    u256 delta = _header.gasLimit > parentGasLimit ? _header.gasLimit - parentGasLimit :
                                                     parentGasLimit - _header.gasLimit;
    if (delta >= parentGasLimit / kGasLimitBoundDivisor)
    {
        return {false, "gasLimit differs from the parent by more than 1/1024"};
    }
    if (_header.gasUsed > _header.gasLimit)
    {
        return {false, "gasUsed exceeds gasLimit"};
    }

    // EIP-1559 base fee and EIP-4844 blob gas.
    if (auto err = detail::validateBaseFee(_header, _parent, _config))
    {
        return {false, *err};
    }
    if (auto err = detail::validateBlobGas(_header, _parent, _config))
    {
        return {false, *err};
    }

    return {true, {}};
}
}  // namespace bcos::devp2p::sync
