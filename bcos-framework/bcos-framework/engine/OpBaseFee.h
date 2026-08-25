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
 * @brief OP-Stack EIP-1559 next-block baseFee — the SINGLE implementation shared by
 *        the engine (block production) and the RPC (eth_feeHistory's predicted entry).
 */

#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/Common.h>
#include <optional>

namespace bcos::engine
{

/// Next-block baseFee for an OP-Stack chain, mirroring op-geth
/// consensus/misc/eip1559/eip1559.go (CalcBaseFee with the Holocene/Jovian
/// extraData parameters). This used to exist as TWO hand-mirrored copies — the
/// engine's authoritative calcOpBaseFee and an RPC port for eth_feeHistory —
/// maintained by a "MUST mirror any change" comment; they had already drifted
/// in mechanism (flag vs extraData length sniffing for the Jovian tail). It
/// now lives here, in bcos-framework, the one layer both consumers link.
///
/// Parameters are read from the parent header's extraData:
///   9 bytes  = Holocene: version || denominator(u32 BE) || elasticity(u32 BE)
///   17 bytes = Jovian adds an 8-byte minBaseFee floor (u64 BE)
/// A parent with fewer than 9 bytes gets the Holocene defaults (8/2) — the
/// engine never produces such headers (its own payload validation enforces
/// the shape), but the RPC may observe foreign/malformed ones and must not
/// read out of bounds.
///
/// @param parent        the parent block header (extraData carries the 1559
///                      parameters; blobGasUsed carries the Jovian DA
///                      footprint).
/// @param parentIsJovian whether the Jovian rules apply to the parent. The
///                      engine passes its scheduler feature flag; the RPC
///                      passes its extraData length sniff (>= 17). The fork
///                      DETECTION stays with the caller — only the formula is
///                      shared.
/// @return the predicted baseFee of the child block.
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

    // Jovian minBaseFee floor — only meaningful when the 8-byte tail exists.
    std::optional<bcos::u256> minBaseFee;
    if (parentIsJovian && extra.size() >= 17)
    {
        uint64_t floor = 0;
        for (std::size_t i = 0; i < 8; ++i)
        {
            floor = (floor << 8) | static_cast<uint64_t>(extra[9 + i]);
        }
        minBaseFee = bcos::u256(floor);
    }

    // The arithmetic is done entirely in u256 — op-geth computes in big.Int,
    // and the previous engine copy's u64 narrowing bought nothing but a
    // truncation surface.
    bcos::u256 const gasTarget = parent.gasLimit() / elasticity;

    // Jovian meters baseFee on max(gasUsed, DA footprint); the DA footprint
    // lives in the blobGasUsed header slot (op-geth eip1559.go:99-107).
    bcos::u256 gasMetered = parent.gasUsed();
    if (parentIsJovian && parent.blobGasUsed().has_value() && *parent.blobGasUsed() > gasMetered)
    {
        gasMetered = *parent.blobGasUsed();
    }

    bcos::u256 const parentBaseFee = parent.baseFee().value_or(bcos::u256(0));
    if (gasMetered == gasTarget)
    {
        return parentBaseFee;
    }

    bcos::u256 result;
    if (gasMetered > gasTarget)
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

    // Jovian minBaseFee floor (op-geth eip1559.go:86-91).
    if (minBaseFee.has_value() && result < *minBaseFee)
    {
        result = *minBaseFee;
    }
    return result;
}

}  // namespace bcos::engine
