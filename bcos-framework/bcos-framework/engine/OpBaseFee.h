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
#include <bcos-framework/engine/OpEip1559Params.h>
#include <bcos-framework/engine/OpForkId.h>
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

// The OP EIP-1559 parameters are a CHAIN property (kLegacyOpEip1559Params in
// OpEip1559Params.h is the single legacy default), not constants: op-geth reads them from the
// chain config (params/config.go:1349-1368) and they price every pre-Holocene block. The old
// hardcoded 6/50/250 here priced a denom-8 chain's pre-Canyon blocks with the wrong denominator.

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

/// Check a header's extraData against the layout its own fork requires
/// (op-geth ValidateOptimismExtraData): empty before Holocene, exactly 9 bytes at
/// Holocene/Isthmus, exactly 17 at Jovian and later. The genesis block is the one
/// spec exception to the pre-Holocene rule; genesis never goes through newPayload,
/// so that carve-out belongs to genesis loading, not here.
inline std::optional<std::string> validateOpExtraDataForLayout(
    std::span<const bcos::byte> extraData, OpExtraDataLayout layout)
{
    switch (layout)
    {
    case OpExtraDataLayout::Empty:
        return extraData.empty() ? std::nullopt :
                                   std::optional<std::string>{"must be empty before Holocene"};
    case OpExtraDataLayout::Holocene9:
        if (extraData.size() != c_holoceneExtraDataBytes)
        {
            // Wording pinned by the invalid-vector manifest (opstack-executor e2e).
            return "must be exactly 9 bytes on the OP path (Isthmus)";
        }
        // Name the expected version byte here: the shared shape rule below can only
        // say "does not match length", while this caller knows which fork it expected.
        if (extraData[0] != c_holoceneExtraDataVersion)
        {
            return "version byte must be 0x00 on the OP path (Holocene/Isthmus)";
        }
        return validateOpExtraDataShape(extraData, /*allowEmpty=*/false);
    case OpExtraDataLayout::Jovian17:
        if (extraData.size() != c_jovianExtraDataBytes)
        {
            return "must be exactly 17 bytes on the OP path (Jovian)";
        }
        if (extraData[0] != c_jovianExtraDataVersion)
        {
            return "version byte must be 0x01 on the OP path (Jovian+)";
        }
        return validateOpExtraDataShape(extraData, /*allowEmpty=*/false);
    }
    return "unknown extraData layout";
}

/// One EIP-1559 fee step, shared by both clocks below (op-geth calcBaseFeeInner):
/// parentBaseFee +/- max(1, parentBaseFee * |gasMetered - gasTarget| / gasTarget / denominator).
/// Inputs to one EIP-1559 fee step. Named fields rather than three positional u256
/// amounts: they are the same type, and transposing metered and target would silently
/// invert the direction of the fee change.
struct OpFeeStepParams
{
    bcos::u256 parentBaseFee{};
    bcos::u256 gasMetered{};
    bcos::u256 gasTarget{};
    uint64_t denominator{};
};

[[nodiscard]] inline bcos::u256 opNextBaseFeeStep(OpFeeStepParams const& params)
{
    bcos::u256 const& parentBaseFee = params.parentBaseFee;
    bcos::u256 const& gasMetered = params.gasMetered;
    bcos::u256 const& gasTarget = params.gasTarget;
    uint64_t const denominator = params.denominator;
    if (gasMetered == gasTarget)
    {
        // Exact target: the fee holds steady (delta 0).
        return parentBaseFee;
    }
    // op-geth computes with unbounded big.Int; guard the fixed-width u256 multiply
    // so an extreme (corrupt or adversarial) parent header fails closed instead of
    // wrapping mod 2^256.
    bcos::u256 const u256Max = ~bcos::u256(0);
    if (gasMetered > gasTarget)
    {
        // baseFee increases: max(1, parentBaseFee * delta / gasTarget / denominator)
        bcos::u256 const delta = gasMetered - gasTarget;
        if (parentBaseFee > u256Max / delta) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} << bcos::errinfo_comment{
                                      "OP base-fee delta computation overflows u256"});
        }
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        bcos::u256 const result = parentBaseFee + (deltaFee > 0 ? deltaFee : bcos::u256(1));
        // The multiply guard cannot see the final add; deltaFee near the maximum
        // would wrap exactly here, where big.Int would keep going.
        if (result < parentBaseFee) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(InvalidEngineEncoding{}
                                  << bcos::errinfo_comment{"OP base-fee increase overflows u256"});
        }
        return result;
    }
    // baseFee decreases: parentBaseFee - parentBaseFee * delta / gasTarget / denominator
    bcos::u256 const delta = gasTarget - gasMetered;
    if (parentBaseFee > u256Max / delta) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} << bcos::errinfo_comment{
                                  "OP base-fee delta computation overflows u256"});
    }
    bcos::u256 deltaFee = parentBaseFee * delta;
    deltaFee /= gasTarget;
    deltaFee /= denominator;
    return deltaFee < parentBaseFee ? parentBaseFee - deltaFee : bcos::u256(0);
}

/// Next-block baseFee from a Holocene-active parent's own extraData. Reachable only
/// through calcOpNextBlockBaseFee below, which owns the pre-Holocene constants path.
/// extraData layout (version byte first):
///   9 bytes  = Holocene: 0x00 || denominator(u32 BE) || elasticity(u32 BE)
///   17 bytes = Jovian:   0x01 || denominator || elasticity || minBaseFee(u64 BE)
/// Fail-closed everywhere (no 8/2 default): empty, short, wrong-version, or zero
/// denom/elasticity extraData throws; a Holocene+/Jovian parent missing baseFee
/// (or a Jovian parent missing blobGasUsed) throws — op-geth dereferences those
/// fields and would panic on nil, so silence is never an option; and the u256
/// delta multiply is overflow-guarded where op-geth relies on unbounded big.Int.
/// The caller decides parentIsJovian from the fork schedule; the minBaseFee floor
/// is only read from exactly-17-byte extraData carrying 0x01.
[[nodiscard]] inline bcos::u256 calcOpBaseFee(
    bcos::protocol::BlockHeader const& parent, bool parentIsJovian)
{
    auto extraView = parent.extraData();
    std::span<const bcos::byte> extra{extraView.data(), extraView.size()};
    if (auto shapeError = validateOpExtraDataShape(extra, /*allowEmpty=*/false))
    {
        throwOpBaseFeeError("OP parent extraData " + *shapeError);
    }
    auto [denominator32, elasticity32] =
        decodeEip1559Params(extra.subspan(1, c_eip1559ParamsBytes));
    uint64_t const denominator = denominator32;
    uint64_t const elasticity = elasticity32;

    // Jovian minBaseFee — requires exactly the engine's stamped/validated Jovian layout
    // (17 bytes, version byte 0x01). A bare >=17 gate would read a floor out of a buffer
    // the extraData validation would have rejected.
    std::optional<bcos::u256> minBaseFee;
    if (parentIsJovian && extra.size() == 17 && extra[0] == 0x01)
    {
        minBaseFee = bcos::u256(bcos::fromBigEndian<std::uint64_t>(extra.subspan(9, 8)));
    }

    bcos::u256 const gasTarget = parent.gasLimit() / elasticity;
    if (gasTarget == 0) [[unlikely]]
    {
        throwOpBaseFeeError("invalid OP base-fee parameters: zero gas target");
    }

    // Jovian meters max(gasUsed, blobGasUsed DA footprint). op-geth dereferences
    // header.BlobGasUsed on the Jovian path; a Jovian parent without it is corrupt,
    // so fail closed instead of silently under-counting the DA footprint.
    bcos::u256 gasMetered = parent.gasUsed();
    if (parentIsJovian)
    {
        if (!parent.blobGasUsed().has_value())
        {
            throwOpBaseFeeError("Jovian OP parent header is missing blobGasUsed");
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
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{}
                              << bcos::errinfo_comment{"OP parent header is missing baseFee"});
    }
    bcos::u256 const parentBaseFee = *parent.baseFee();
    bcos::u256 result = opNextBaseFeeStep(OpFeeStepParams{.parentBaseFee = parentBaseFee,
        .gasMetered = gasMetered,
        .gasTarget = gasTarget,
        .denominator = denominator});

    // Jovian minBaseFee floor — applies to all three arms.
    if (minBaseFee.has_value() && result < *minBaseFee)
    {
        result = *minBaseFee;
    }
    return result;
}

/// Which clock the next block's baseFee uses. Both flags describe the PARENT: the
/// 1559 parameter source is the parent's fork (op-geth IsOptimismHolocene(parent.Time)),
/// while the denominator's Canyon choice follows the block being built.
///
/// `parentIsHolocene`/`parentIsJovian` are derived from the parent's extraData layout
/// (OpForkId.h's extraDataLayoutFor): non-Empty means Holocene or later, Jovian17 means
/// Jovian or later. The static_assert beside that table keeps the layout boundaries
/// where the forks are, so the two notions cannot drift apart silently.
///
/// `eip1559` is WHAT the chain prices with — its declared triple (config.genesis
/// [op_eip1559], kLegacyOpEip1559Params when undeclared). Carrying it here mirrors op-geth,
/// which hands CalcBaseFee the whole *params.ChainConfig: fork times and optimism parameters
/// together. The default keeps every pre-existing caller (tests included) on the legacy
/// preset; only a caller that knows the chain's declaration changes it.
struct OpBaseFeeClock
{
    bool parentIsHolocene = false;
    bool parentIsJovian = false;
    bool newBlockIsCanyon = false;
    OpEip1559Params eip1559 = kLegacyOpEip1559Params;
};

/// Next-block baseFee for newPayload validation and payload building — the single
/// entry point, so the two callers cannot drift apart (op-geth CalcBaseFee:
/// `denominator := BaseFeeChangeDenominator(time)` on the NEW block's time, then
/// `if IsOptimismHolocene(parent.Time)` override from the PARENT's extraData).
///
/// A Holocene activation block is the constants case: it carries 9-byte extraData
/// itself, but its parent does not, so the caller must pass parentIsHolocene=false
/// (op-reth had this backwards before #13060).
[[nodiscard]] inline bcos::u256 calcOpNextBlockBaseFee(
    bcos::protocol::BlockHeader const& parent, OpBaseFeeClock clock)
{
    if (clock.parentIsHolocene)
    {
        return calcOpBaseFee(parent, clock.parentIsJovian);
    }
    // Pre-Holocene parent: empty extraData, so the CHAIN'S triple applies and the
    // parent's extraData (if any) is deliberately ignored. Denominator by the new
    // block's fork (op-geth BaseFeeChangeDenominator(time)), elasticity for every fork.
    uint64_t const denominator =
        clock.newBlockIsCanyon ? clock.eip1559.denominatorCanyon : clock.eip1559.denominator;
    uint64_t const elasticity = clock.eip1559.elasticity;
    // The declared triple is validated at config load; the legacy default is non-zero by
    // construction. This guard keeps a zero from a hand-built clock out of the division below.
    if (elasticity == 0 || denominator == 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            InvalidEngineEncoding{} << bcos::errinfo_comment{
                "invalid OP base-fee parameters: zero elasticity or denominator"});
    }
    // op-geth dereferences parent.BaseFee and panics on nil; fail closed instead.
    if (!parent.baseFee().has_value())
    {
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{}
                              << bcos::errinfo_comment{"OP parent header is missing baseFee"});
    }
    bcos::u256 const parentBaseFee = *parent.baseFee();
    bcos::u256 const gasTarget = parent.gasLimit() / elasticity;
    if (gasTarget == 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} << bcos::errinfo_comment{
                                  "invalid OP base-fee parameters: zero gas target"});
    }
    // Pre-Holocene has no DA footprint: the Jovian max(gasUsed, blobGasUsed) metering
    // is part of the Holocene path above.
    return opNextBaseFeeStep(OpFeeStepParams{.parentBaseFee = parentBaseFee,
        .gasMetered = parent.gasUsed(),
        .gasTarget = gasTarget,
        .denominator = denominator});
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
