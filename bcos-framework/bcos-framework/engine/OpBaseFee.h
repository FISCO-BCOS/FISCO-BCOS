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
 * @brief OP-Stack next-block baseFee (op-geth CalcBaseFee) and extraData shape checks.
 */

#pragma once

#include "Errors.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace bcos::engine
{
[[noreturn]] inline void throwOpBaseFeeError(std::string message)
{
    BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} << bcos::errinfo_comment{std::move(message)});
}

/// Canyon EIP-1559 parameters (op-geth params/config.go).
inline constexpr std::uint32_t c_eip1559DenominatorCanyon = 250;
inline constexpr std::uint32_t c_eip1559ElasticityCanyon = 6;

/// Holocene extraData is 9 bytes (0x00 || denom || elasticity);
/// Jovian extraData is 17 bytes (0x01 || same || minBaseFee).
inline constexpr std::size_t c_holoceneExtraDataBytes = 9;
inline constexpr std::size_t c_jovianExtraDataBytes = 17;
inline constexpr bcos::byte c_holoceneExtraDataVersion = 0x00;
inline constexpr bcos::byte c_jovianExtraDataVersion = 0x01;
inline constexpr std::size_t c_eip1559ParamsBytes = 8;

/// Decode the 8-byte Holocene eip1559Params / extraData[1:9] pair (u32 BE denom, u32 BE
/// elasticity).
inline std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(
    std::span<const bcos::byte> params)
{
    if (params.size() != 8)
    {
        BOOST_THROW_EXCEPTION(
            InvalidEngineEncoding{} << bcos::errinfo_comment{"eip1559Params must be 8 bytes"});
    }
    auto denominator = bcos::fromBigEndian<std::uint32_t>(params.first(4));
    auto elasticity = bcos::fromBigEndian<std::uint32_t>(params.subspan(4, 4));
    return {denominator, elasticity};
}

/// Check extraData length, version byte, and non-zero EIP-1559 pair.
/// Empty extraData is allowed when `allowEmpty` is true (pre-Holocene payloads).
inline std::optional<std::string> validateOpExtraDataShape(
    std::span<const bcos::byte> extraData, bool allowEmpty = true)
{
    if (extraData.empty())
    {
        return allowEmpty ? std::nullopt :
                            std::optional<std::string>{"must be 9 (Holocene) or 17 (Jovian) bytes"};
    }
    if (extraData.size() != c_holoceneExtraDataBytes && extraData.size() != c_jovianExtraDataBytes)
    {
        return "must be 9 (Holocene) or 17 (Jovian) bytes, got " + std::to_string(extraData.size());
    }
    auto const expectedVersion = extraData.size() == c_jovianExtraDataBytes ?
                                     c_jovianExtraDataVersion :
                                     c_holoceneExtraDataVersion;
    if (extraData[0] != expectedVersion)
    {
        return std::string("version byte does not match length");
    }
    auto [denominator, elasticity] =
        decodeEip1559Params(extraData.subspan(1, c_eip1559ParamsBytes));
    if (denominator == 0 || elasticity == 0)
    {
        return std::string("must encode a non-zero EIP-1559 denominator and elasticity");
    }
    return std::nullopt;
}

namespace detail
{
/// EIP-1559 arithmetic core shared by the engine (BlockHeader) and devp2p (raw header
/// fields) entry points — op-geth consensus/misc/eip1559/eip1559.go calcBaseFeeInner plus
/// the Jovian minBaseFee floor. op-geth computes with unbounded big.Int; the fixed-width
/// u256 multiply is overflow-guarded here so an extreme (corrupt or adversarial) parent
/// header fails closed instead of wrapping mod 2^256.
inline bcos::u256 calcOpBaseFeeCore(bcos::u256 const& parentGasLimit, bcos::u256 gasMetered,
    bcos::u256 const& parentBaseFee, std::uint64_t denominator, std::uint64_t elasticity,
    std::optional<bcos::u256> const& minBaseFee)
{
    bcos::u256 const gasTarget = parentGasLimit / elasticity;
    if (gasTarget == 0) [[unlikely]]
    {
        throwOpBaseFeeError("invalid OP base-fee parameters: zero gas target");
    }

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
            throwOpBaseFeeError("OP base-fee delta computation overflows u256");
        }
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        result = parentBaseFee + (deltaFee > 0 ? deltaFee : bcos::u256(1));
        // The multiply guard cannot see the final add; deltaFee near the maximum
        // would wrap exactly here, where big.Int would keep going.
        if (result < parentBaseFee) [[unlikely]]
        {
            throwOpBaseFeeError("OP base-fee increase overflows u256");
        }
    }
    else
    {
        // baseFee decreases: parentBaseFee - parentBaseFee * delta / gasTarget / denominator
        bcos::u256 const delta = gasTarget - gasMetered;
        if (parentBaseFee > u256Max / delta) [[unlikely]]
        {
            throwOpBaseFeeError("OP base-fee delta computation overflows u256");
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
}  // namespace detail

/// Jovian metering: the base fee moves on max(gasUsed, blobGasUsed DA footprint).
/// op-geth (calcBaseFeeInner) dereferences header.BlobGasUsed on the Jovian path; a
/// Jovian parent without it is corrupt, so fail closed instead of silently
/// under-counting the DA footprint.
inline bcos::u256 opGasMetered(
    bcos::u256 parentGasUsed, std::optional<bcos::u256> const& parentBlobGasUsed, bool parentIsJovian)
{
    if (parentIsJovian)
    {
        if (!parentBlobGasUsed.has_value())
        {
            throwOpBaseFeeError("Jovian OP parent header is missing blobGasUsed");
        }
        if (*parentBlobGasUsed > parentGasUsed)
        {
            return *parentBlobGasUsed;
        }
    }
    return parentGasUsed;
}

/// Raw-header-fields next-block baseFee — op-geth eip1559.CalcBaseFee
/// (consensus/misc/eip1559/eip1559.go) lifted off protocol::BlockHeader so the devp2p
/// header-sync validator (which works on rlp-protocol's EthBlockHeaderData) shares the
/// exact arithmetic with the engine. Semantics, keyed exactly like op-geth:
///  - `parentIsHolocene` (Holocene active at the PARENT's timestamp): decode the EIP-1559
///    denominator/elasticity — and, for a Jovian parent, the minBaseFee floor — from the
///    parent's extraData (fail-closed on any shape violation, like op-geth which assumes
///    ValidateOptimismExtraData already passed on the parent).
///  - Pre-Holocene parent: `fallbackDenominator`/`fallbackElasticity` apply; op-geth
///    sources them from the chain config with the denominator keyed on the CHILD's
///    Canyon activation (BaseFeeChangeDenominator(header.Time)) — the caller resolves
///    that pair.
/// Throws InvalidEngineEncoding (fail-closed) on malformed parent data or u256 overflow.
inline bcos::u256 calcOpBaseFeeFromFields(bcos::u256 const& parentGasLimit,
    bcos::u256 const& parentGasUsed, bcos::u256 const& parentBaseFee,
    std::optional<bcos::u256> const& parentBlobGasUsed,
    std::span<const bcos::byte> parentExtraData, bool parentIsHolocene, bool parentIsJovian,
    std::uint64_t fallbackDenominator, std::uint64_t fallbackElasticity)
{
    std::uint64_t denominator = fallbackDenominator;
    std::uint64_t elasticity = fallbackElasticity;
    std::optional<bcos::u256> minBaseFee;
    if (parentIsHolocene)
    {
        if (auto shapeError = validateOpExtraDataShape(parentExtraData, /*allowEmpty=*/false))
        {
            throwOpBaseFeeError("OP parent extraData " + *shapeError);
        }
        auto [denominator32, elasticity32] =
            decodeEip1559Params(parentExtraData.subspan(1, c_eip1559ParamsBytes));
        denominator = denominator32;
        elasticity = elasticity32;
        // Jovian minBaseFee — only from exactly the Jovian layout (17 bytes, version
        // byte 0x01); a 9-byte extraData under a Jovian parent decodes params but
        // carries no floor (op-geth DecodeJovianExtraData best-effort behaviour).
        if (parentIsJovian && parentExtraData.size() == c_jovianExtraDataBytes &&
            parentExtraData[0] == c_jovianExtraDataVersion)
        {
            minBaseFee =
                bcos::u256(bcos::fromBigEndian<std::uint64_t>(parentExtraData.subspan(9, 8)));
        }
    }
    auto const gasMetered = opGasMetered(parentGasUsed, parentBlobGasUsed, parentIsJovian);
    return detail::calcOpBaseFeeCore(
        parentGasLimit, gasMetered, parentBaseFee, denominator, elasticity, minBaseFee);
}

/// Next-block baseFee (op-geth CalcBaseFee). Holocene-active and later only:
/// a pre-Holocene parent has empty extraData and must use the prior 1559 constants
/// in the caller, not this helper. extraData layout (version byte first):
/// 9 bytes = Holocene: 0x00 || denominator(u32 BE) || elasticity(u32 BE)
/// 17 bytes = Jovian: 0x01 || denominator || elasticity || minBaseFee(u64 BE)
/// Fail-closed everywhere (no 8/2 default): empty, short, wrong-version, or zero
/// denom/elasticity extraData throws; a Holocene+/Jovian parent missing baseFee
/// (or a Jovian parent missing blobGasUsed) throws — op-geth dereferences those
/// fields and would panic on nil, so silence is never an option; and the u256
/// delta multiply is overflow-guarded where op-geth relies on unbounded big.Int.
/// The caller decides parentIsJovian from the fork schedule; the minBaseFee floor
/// is only read from exactly-17-byte extraData carrying 0x01.
inline bcos::u256 calcOpBaseFee(bcos::protocol::BlockHeader const& parent, bool parentIsJovian)
{
    auto extraView = parent.extraData();
    std::span<const bcos::byte> extra{extraView.data(), extraView.size()};
    // op-geth dereferences parent.BaseFee and panics on nil; a Holocene+ parent
    // without a base fee is a corrupt header — fail closed rather than pricing the
    // next block at 0.
    if (!parent.baseFee().has_value())
    {
        throwOpBaseFeeError("OP parent header is missing baseFee");
    }
    return calcOpBaseFeeFromFields(parent.gasLimit(), parent.gasUsed(), *parent.baseFee(),
        parent.blobGasUsed(), extra, /*parentIsHolocene=*/true, parentIsJovian,
        /*fallbackDenominator=*/0, /*fallbackElasticity=*/0 /* unused: Holocene always
        decodes the parameters from the parent extraData */);
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
