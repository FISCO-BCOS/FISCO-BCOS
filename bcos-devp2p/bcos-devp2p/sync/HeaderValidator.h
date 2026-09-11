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
 *        gas-limit bounds, difficulty/nonce/uncle/extra-data rules, fork-gated
 *        field presence for Shanghai/Cancun).
 * @date 2026/8/18
 */
#pragma once

#include "Block.h"
#include <bcos-framework/protocol/BlobSchedule.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-utilities/Common.h>
#include <limits>
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
constexpr uint64_t kGasPerBlob = 1U << 17;  // 131072 (BlobTxBlobGasPerBlob)
constexpr uint64_t kBlobBaseCost = 1U << 13;  // 8192 (EIP-7918 reserve-price factor)

// EIP-7840 blob schedule per fork: the canonical table (blob counts + update
// fraction) lives in bcos-framework/protocol/BlobSchedule.h, shared with the v2
// executor so the two cannot drift. The gas-denominated view below is what the
// validation math uses.
struct BlobSchedule
{
    uint64_t targetBlobGas;          // target blob gas per block
    uint64_t maxBlobGas;             // maximum blob gas per block
    uint64_t baseFeeUpdateFraction;  // EIP-4844/7840 blob fee update fraction
};

constexpr BlobSchedule toGasSchedule(protocol::BlobScheduleConfig const& _params)
{
    return {_params.targetBlobs * kGasPerBlob, _params.maxBlobs * kGasPerBlob,
        _params.baseFeeUpdateFraction};
}

constexpr BlobSchedule kCancunBlobSchedule = toGasSchedule(protocol::CANCUN_BLOB_SCHEDULE);
constexpr BlobSchedule kPragueBlobSchedule = toGasSchedule(protocol::PRAGUE_BLOB_SCHEDULE);
// Osaka keeps the Prague schedule (EIP-7918 only changes the excess update rule).
constexpr BlobSchedule kOsakaBlobSchedule = kPragueBlobSchedule;
constexpr BlobSchedule kBpo1BlobSchedule = toGasSchedule(protocol::BPO1_BLOB_SCHEDULE);
constexpr BlobSchedule kBpo2BlobSchedule = toGasSchedule(protocol::BPO2_BLOB_SCHEDULE);

// Minimal chain configuration for header validation (timestamp-based forks).
struct ChainConfig
{
    uint64_t chainId{1};
    // Fork activation timestamps; 0 means "active from genesis". The post-Prague
    // tail (osaka/bpo1/bpo2) defaults to UINT64_MAX ("not yet active") — matching
    // NodeConfig's readOptionalTs semantics for absent tail keys — so a config
    // that does not schedule them validates with the pre-Osaka rules.
    uint64_t londonTime{0};
    uint64_t shanghaiTime{0};
    uint64_t cancunTime{0};
    uint64_t pragueTime{0};
    uint64_t osakaTime{std::numeric_limits<uint64_t>::max()};
    uint64_t bpo1Time{std::numeric_limits<uint64_t>::max()};
    uint64_t bpo2Time{std::numeric_limits<uint64_t>::max()};
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

// EIP-7840: the blob schedule in effect for the block at `_timestamp`, from the
// shared table (protocol::blobScheduleForTimestamp). Cancun / Prague / BPO1 /
// BPO2 are keyed on their activation timestamps; Osaka keeps the Prague
// schedule — EIP-7918 changes only the excess-blob-gas UPDATE rule.
inline BlobSchedule blobScheduleFor(ChainConfig const& _config, int64_t _timestamp)
{
    return toGasSchedule(protocol::blobScheduleForTimestamp(
        protocol::BlobForkTimes{.cancunTime = _config.cancunTime,
            .pragueTime = _config.pragueTime,
            .bpo1Time = _config.bpo1Time,
            .bpo2Time = _config.bpo2Time},
        static_cast<uint64_t>(_timestamp)));
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

// EIP-4844 fake_exponential: floor(factor * e^(numerator/denominator)) via the
// truncated Taylor series. Port of the spec / geth's fakeExponential: the
// accumulator starts at factor * denominator so every term keeps the
// denominator's precision through the per-step floors, and the sum is divided
// by the denominator once at the end. (Starting the accumulator at `factor`
// truncates each term to an integer and badly under-approximates — e.g. the
// spec gives floor(e^2) = 7 for (1, 2d, d), the naive form gives 6.)
// The multiply is widened to u512 as in the executor's compute_blob_gas_price;
// excess values here come from peer headers, so absurd inputs saturate (treated
// as "very expensive", the same side of the reserve-price branch geth lands on)
// instead of wrapping.
inline u256 fakeExponential(
    u256 const& _factor, u256 const& _numerator, u256 const& _denominator)
{
    u512 output = 0;
    u256 acc = _factor * _denominator;
    u256 i = 1;
    while (acc > 0)
    {
        output += acc;
        auto const product = u512(acc) * u512(_numerator);
        if (product > u512(std::numeric_limits<u256>::max()))
        {
            return std::numeric_limits<u256>::max();
        }
        acc = u256(product) / (_denominator * i);
        ++i;
    }
    auto const result = output / u512(_denominator);
    return result > u512(std::numeric_limits<u256>::max()) ?
               std::numeric_limits<u256>::max() :
               u256(result);
}

// EIP-4844 / EIP-7918: the excess blob gas of the next block (the block after
// `_parent`), under the blob schedule active at that block. From Osaka on,
// EIP-7918 replaces the plain target-delta rule whenever the blob fee is below a
// "reserve price" (8192 * baseFee > 131072 * blobBaseFee): the excess then grows
// by parentBlobGasUsed * (max - target) / max so the blob base fee keeps
// converging instead of stalling at the floor. Port of geth's
// consensus/misc/eip4844 calcExcessBlobGas.
inline u256 computeNextExcessBlobGas(bcos::protocol::EthBlockHeaderData const& _parent,
    BlobSchedule const& _schedule, bool _osakaActive)
{
    u256 const parentExcess = _parent.excessBlobGas.value_or(0);
    u256 const parentBlobGasUsed = _parent.blobGasUsed.value_or(0);
    u256 const total = parentBlobGasUsed + parentExcess;
    u256 const targetGas = _schedule.targetBlobGas;
    if (total < targetGas)
    {
        return 0;
    }
    if (_osakaActive)
    {
        // reservePrice = BlobBaseCost(8192) * baseFee; blobPrice =
        // blobBaseFee(excess) * BlobTxBlobGasPerBlob(131072) — geth's
        // BlobConfig.blobPrice.
        u256 const reservePrice = kBlobBaseCost * _parent.baseFee.value_or(0);
        u256 const blobPrice =
            fakeExponential(u256{1}, parentExcess, _schedule.baseFeeUpdateFraction) *
            kGasPerBlob;
        if (reservePrice > blobPrice)
        {
            u256 const scaledExcess =
                parentBlobGasUsed * (_schedule.maxBlobGas - _schedule.targetBlobGas) /
                _schedule.maxBlobGas;
            return parentExcess + scaledExcess;
        }
    }
    return total - targetGas;
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
        else if (*_header.excessBlobGas !=
                 computeNextExcessBlobGas(_parent, schedule,
                     isForkActive(_config.osakaTime, _header.timestamp)))
        {
            return "excessBlobGas does not match the EIP-4844/7918 recomputation";
        }
    }
    return std::nullopt;
}

// Fork-gated field presence (geth's VerifyHeader fails closed: a header that is
// missing a field its active fork requires is INVALID, not "skipped"). London's
// baseFee presence is checked in validateBaseFee; this covers Shanghai/Cancun and
// Prague's requestsHash. The VALUE of requestsHash (EIP-7685) is verified by the
// block verifier against the executed request list once that lands — presence here
// is the fail-closed gate so a peer cannot serve a post-Prague header that omits it.
inline std::optional<std::string> validateForkFieldPresence(
    bcos::protocol::EthBlockHeaderData const& _header, ChainConfig const& _config)
{
    if (isForkActive(_config.shanghaiTime, _header.timestamp) &&
        !_header.withdrawalsHash.has_value())
    {
        return "missing withdrawalsHash (Shanghai active)";
    }
    if (isForkActive(_config.cancunTime, _header.timestamp))
    {
        if (!_header.blobGasUsed.has_value())
        {
            return "missing blobGasUsed (Cancun active)";
        }
        if (!_header.excessBlobGas.has_value())
        {
            return "missing excessBlobGas (Cancun active)";
        }
        if (!_header.parentBeaconRoot.has_value())
        {
            return "missing parentBeaconBlockRoot (Cancun active)";
        }
    }
    if (isForkActive(_config.pragueTime, _header.timestamp) && !_header.requestsHash.has_value())
    {
        return "missing requestsHash (Prague active)";
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
        // PoS: difficulty must be zero, the nonce must be all-zero and no ommers.
        if (_header.difficulty != 0)
        {
            return {false, "PoS difficulty must be zero"};
        }
        if (_header.nonce != bcos::h64{})
        {
            return {false, "PoS nonce must be zero"};
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
    // Shanghai/Cancun fork-gated field presence (fail-closed).
    if (auto err = detail::validateForkFieldPresence(_header, _config))
    {
        return {false, *err};
    }

    return {true, {}};
}
}  // namespace bcos::devp2p::sync
