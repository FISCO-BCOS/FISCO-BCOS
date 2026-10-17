/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file OpFeeParams.cpp
 */

#include <opstack-executor/OpFeeParams.h>

#include <cassert>

namespace bcos::executor_v1::opstack
{
namespace
{
// Read [byteOff, byteOff+len) of a big-endian word as an unsigned integer (len <= 8).
uint64_t readBE(const evmc::bytes32& w, size_t byteOff, size_t len) noexcept
{
    assert(len <= 8 && byteOff + len <= sizeof(w.bytes));
    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i)
    {
        v = (v << 8) | w.bytes[byteOff + i];
    }
    return v;
}
}  // namespace

OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot5, const evmc::bytes32& slot6, const evmc::bytes32& slot7,
    const evmc::bytes32& slot8) noexcept
{
    return OpFeeParams{
        .l1_base_fee = intx::be::load<intx::uint256>(slot1),
        .base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 16, 4)),
        .blob_base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 20, 4)),
        .blob_base_fee = intx::be::load<intx::uint256>(slot7),
        .operator_fee_scalar = static_cast<uint32_t>(readBE(slot8, 20, 4)),
        .operator_fee_constant = readBE(slot8, 24, 8),
        .da_footprint_gas_scalar = static_cast<uint16_t>(readBE(slot8, 18, 2)),
        .l1_fee_overhead = intx::be::load<intx::uint256>(slot5),
        .l1_fee_scalar = intx::be::load<intx::uint256>(slot6),
    };
}
}  // namespace bcos::executor_v1::opstack
