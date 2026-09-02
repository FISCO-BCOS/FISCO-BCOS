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
 * @file OpBaseFee.h
 * @brief OP-Stack next-block baseFee (op-geth CalcBaseFee). Types-only in this
 * slice: the in-tree caller is CalcOpBaseFeeTest. EngineServiceImpl still has a
 * file-local decoder; #5538 should include this header and delete that copy.
 */

#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace bcos::engine
{

/// Canyon EIP-1559 parameters (op-geth params/config.go). Mirrors the anonymous-namespace
/// copies in EngineServiceImpl.cpp (these constants, decodeEip1559Params, and
/// validateOptimismExtraDataShape) — #5538 consolidates all three copies onto this
/// header so the engine INVALID gate and the next-base-fee calc share one decoder.
/// Unused by calcOpBaseFee itself.
inline constexpr std::uint32_t c_eip1559DenominatorCanyon = 250;
inline constexpr std::uint32_t c_eip1559ElasticityCanyon = 6;

/// Decode the 8-byte Holocene eip1559Params / extraData[1:9] pair (u32 BE denom, u32 BE
/// elasticity).
inline std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(
    std::span<const bcos::byte> params)
{
    if (params.size() != 8)
    {
        throw std::invalid_argument("eip1559Params must be 8 bytes");
    }
    auto denominator = bcos::fromBigEndian<std::uint32_t>(params.first(4));
    auto elasticity = bcos::fromBigEndian<std::uint32_t>(params.subspan(4, 4));
    return {denominator, elasticity};
}

/// Next-block baseFee (op-geth CalcBaseFee). Holocene-active and later only:
/// a pre-Holocene parent has empty extraData and must use the prior 1559 constants
/// in the caller, not this helper. extraData layout (version byte first):
///   9 bytes  = Holocene: 0x00 || denominator(u32 BE) || elasticity(u32 BE)
///   17 bytes = Jovian:   0x01 || denominator || elasticity || minBaseFee(u64 BE)
/// Fail-closed everywhere (no 8/2 default): empty, short, wrong-version, or zero
/// denom/elasticity extraData throws; a Holocene+/Jovian parent missing baseFee
/// (or a Jovian parent missing blobGasUsed) throws — op-geth dereferences those
/// fields and would panic on nil, so silence is never an option; and the u256
/// delta multiply is overflow-guarded where op-geth relies on unbounded big.Int.
/// The caller decides parentIsJovian from the fork schedule; the minBaseFee floor
/// is only read from exactly-17-byte extraData carrying 0x01.
inline bcos::u256 calcOpBaseFee(bcos::protocol::BlockHeader const& parent, bool parentIsJovian)
{
    auto const extra = parent.extraData();
    if (extra.size() != 9 && extra.size() != 17)
    {
        throw std::invalid_argument(
            "OP parent extraData must be 9 (Holocene) or 17 (Jovian) bytes");
    }
    auto const expectedVersion =
        extra.size() == 17 ? static_cast<bcos::byte>(0x01) : static_cast<bcos::byte>(0x00);
    if (extra[0] != expectedVersion)
    {
        throw std::invalid_argument("OP parent extraData version byte does not match length");
    }
    auto [denominator32, elasticity32] =
        decodeEip1559Params(std::span<const bcos::byte>(extra.data(), extra.size()).subspan(1, 8));
    if (denominator32 == 0 || elasticity32 == 0)
    {
        throw std::invalid_argument(
            "OP parent extraData must encode a non-zero EIP-1559 denominator and elasticity");
    }
    uint64_t const denominator = denominator32;
    uint64_t const elasticity = elasticity32;

    // Jovian minBaseFee — requires exactly the engine's stamped/validated Jovian layout
    // (17 bytes, version byte 0x01). A bare >=17 gate would read a floor out of a buffer
    // the extraData validation would have rejected.
    std::optional<bcos::u256> minBaseFee;
    if (parentIsJovian && extra.size() == 17 && extra[0] == 0x01)
    {
        minBaseFee = bcos::u256(
            bcos::fromBigEndian<std::uint64_t>(std::span<const bcos::byte>(extra).subspan(9, 8)));
    }

    bcos::u256 const gasTarget = parent.gasLimit() / elasticity;
    if (gasTarget == 0) [[unlikely]]
    {
        throw std::invalid_argument("invalid OP base-fee parameters: zero gas target");
    }

    // Jovian meters max(gasUsed, blobGasUsed DA footprint). op-geth dereferences
    // header.BlobGasUsed on the Jovian path; a Jovian parent without it is corrupt,
    // so fail closed instead of silently under-counting the DA footprint.
    bcos::u256 gasMetered = parent.gasUsed();
    if (parentIsJovian)
    {
        if (!parent.blobGasUsed().has_value())
        {
            throw std::invalid_argument("Jovian OP parent header is missing blobGasUsed");
        }
        if (*parent.blobGasUsed() > gasMetered)
        {
            gasMetered = *parent.blobGasUsed();
        }
    }

    // op-geth dereferences parent.BaseFee and panics on nil; a Holocene+ parent
    // without a base fee is a corrupt header — fail closed rather than pricing the
    // next block at 0.
    if (!parent.baseFee().has_value())
    {
        throw std::invalid_argument("OP parent header is missing baseFee");
    }
    bcos::u256 const parentBaseFee = *parent.baseFee();
    // op-geth computes with unbounded big.Int; guard the fixed-width u256 multiply
    // so an extreme (corrupt or adversarial) parent header fails closed instead of
    // wrapping mod 2^256.
    bcos::u256 const u256Max = ~bcos::u256(0);
    bcos::u256 result;
    if (gasMetered == gasTarget)
    {
        // Exact target: the fee holds steady (delta 0) — still subject to the Jovian
        // minBaseFee floor below, like every other arm.
        result = parentBaseFee;
    }
    else if (gasMetered > gasTarget)
    {
        // baseFee increases: max(1, parentBaseFee * delta / gasTarget / denominator)
        bcos::u256 const delta = gasMetered - gasTarget;
        if (parentBaseFee > u256Max / delta) [[unlikely]]
        {
            throw std::invalid_argument("OP base-fee delta computation overflows u256");
        }
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        result = parentBaseFee + (deltaFee > 0 ? deltaFee : bcos::u256(1));
        // The multiply guard cannot see the final add; deltaFee near the maximum
        // would wrap exactly here, where big.Int would keep going.
        if (result < parentBaseFee) [[unlikely]]
        {
            throw std::invalid_argument("OP base-fee increase overflows u256");
        }
    }
    else
    {
        // baseFee decreases: parentBaseFee - parentBaseFee * delta / gasTarget / denominator
        bcos::u256 const delta = gasTarget - gasMetered;
        if (parentBaseFee > u256Max / delta) [[unlikely]]
        {
            throw std::invalid_argument("OP base-fee delta computation overflows u256");
        }
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        result = deltaFee < parentBaseFee ? parentBaseFee - deltaFee : bcos::u256(0);
    }

    // Jovian minBaseFee floor — applies to all three arms.
    if (minBaseFee.has_value() && result < *minBaseFee)
    {
        result = *minBaseFee;
    }
    return result;
}

/// Built-in OP driver gas limit: the chain's configured value (from the ledger's
/// SystemConfig), falling back to 30M only when nothing is configured (0). A real OP
/// chain takes its gas limit from the L1 SystemConfig; the built-in CL stands in with
/// the configured value, not a hard-coded override.
inline constexpr std::uint64_t c_defaultDriverGasLimit = 30'000'000ull;

inline std::uint64_t resolveDriverGasLimit(std::uint64_t configuredGasLimit)
{
    return configuredGasLimit == 0 ? c_defaultDriverGasLimit : configuredGasLimit;
}

}  // namespace bcos::engine
