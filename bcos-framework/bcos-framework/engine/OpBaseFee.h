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
 * @brief OP-Stack next-block baseFee (op-geth CalcBaseFee). No production caller in this
 * PR — the RPC feeHistory consumer lands with split C (#5521); the engine path does not
 * call it (finding AR — the earlier "shared by engine and RPC" claim was aspirational).
 */

#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/Common.h>
#include <optional>
#include <stdexcept>

namespace bcos::engine
{

/// Next-block baseFee (op-geth CalcBaseFee). extraData layout (version byte first):
///   9 bytes  = Holocene: 0x00 || denominator(u32 BE) || elasticity(u32 BE)
///   17 bytes = Jovian:   0x01 || denominator || elasticity || minBaseFee(u64 BE)
/// Shorter extraData uses Holocene defaults (8/2). The caller decides parentIsJovian from
/// the fork schedule; the minBaseFee floor is only read from exactly-17-byte extraData
/// carrying the Jovian version byte 0x01, matching the engine's stamped/validated format.
inline bcos::u256 calcOpBaseFee(bcos::protocol::BlockHeader const& parent, bool parentIsJovian)
{
    auto const extra = parent.extraData();

    uint64_t elasticity = 2;
    uint64_t denominator = 8;
    if (extra.size() >= 9)
    {
        auto readU32BE = [&extra](std::size_t off) {
            return (static_cast<uint64_t>(extra[off]) << 24) |
                   (static_cast<uint64_t>(extra[off + 1]) << 16) |
                   (static_cast<uint64_t>(extra[off + 2]) << 8) |
                   static_cast<uint64_t>(extra[off + 3]);
        };
        denominator = readU32BE(1);
        elasticity = readU32BE(5);
    }
    if (denominator == 0 || elasticity == 0) [[unlikely]]
    {
        // Unset 1559 params: same Holocene defaults (8/2) as a short extraData tail.
        // Zero gas target below remains an arithmetic precondition.
        denominator = 8;
        elasticity = 2;
    }

    // Jovian minBaseFee — requires exactly the engine's stamped/validated Jovian layout
    // (17 bytes, version byte 0x01). Finding S4: a bare >=17 gate would read a floor out
    // of a buffer the engine's extraData validation would have rejected, and a 17-byte
    // Holocene-version buffer would wrongly engage the floor.
    std::optional<bcos::u256> minBaseFee;
    if (parentIsJovian && extra.size() == 17 && extra[0] == 0x01)
    {
        uint64_t floor = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            floor = (floor << 8) | static_cast<uint64_t>(extra[9 + i]);
        }
        minBaseFee = bcos::u256(floor);
    }

    bcos::u256 const gasTarget = parent.gasLimit() / elasticity;
    if (gasTarget == 0) [[unlikely]]
    {
        throw std::invalid_argument("invalid OP base-fee parameters: zero gas target");
    }

    // Jovian meters max(gasUsed, blobGasUsed DA footprint).
    bcos::u256 gasMetered = parent.gasUsed();
    if (parentIsJovian && parent.blobGasUsed().has_value() && *parent.blobGasUsed() > gasMetered)
    {
        gasMetered = *parent.blobGasUsed();
    }

    bcos::u256 const parentBaseFee = parent.baseFee().value_or(bcos::u256(0));
    bcos::u256 result;
    if (gasMetered == gasTarget)
    {
        // Exact target: the fee holds steady (delta 0) — but falls through to the Jovian
        // minBaseFee floor below like every other arm. Finding BT: an early return here
        // skipped the clamp and let the feeHistory trailing prediction quote below the
        // protocol floor whenever the parent sat under a raised minBaseFee.
        result = parentBaseFee;
    }
    else if (gasMetered > gasTarget)
    {
        // baseFee increases: max(1, parentBaseFee * delta / gasTarget / denominator)
        bcos::u256 deltaFee = parentBaseFee * (gasMetered - gasTarget);
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        result = parentBaseFee + (deltaFee > 0 ? deltaFee : bcos::u256(1));
    }
    else
    {
        // baseFee decreases: parentBaseFee - parentBaseFee * delta / gasTarget / denominator
        bcos::u256 deltaFee = parentBaseFee * (gasTarget - gasMetered);
        deltaFee /= gasTarget;
        deltaFee /= denominator;
        result = deltaFee < parentBaseFee ? parentBaseFee - deltaFee : bcos::u256(0);
    }

    // Jovian minBaseFee floor — applies to all three arms (finding BT).
    if (minBaseFee.has_value() && result < *minBaseFee)
    {
        result = *minBaseFee;
    }
    return result;
}

}  // namespace bcos::engine
