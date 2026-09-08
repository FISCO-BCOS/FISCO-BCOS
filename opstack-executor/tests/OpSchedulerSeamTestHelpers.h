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
 * @file OpSchedulerSeamTestHelpers.h
 * @brief Shared test helpers for the scheduler-seam suites
 */

// Test-only helpers for the OpSchedulerSeam. The synthetic L1-attributes deposit
// envelope is a fixture — it must NOT live in the production seam header (part-5
// wiring would otherwise find a ready-made "synthesize" path whose deposit semantics
// are wrong for production: zero sourceHash / 1M gas / all-zero calldata).

#pragma once

#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpDepositEncode.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <span>

namespace bcos::evm::engine::testutil
{
/// Fixture L1-attributes deposit: Isthmus 176 zero bytes; Jovian selector + zeros to 178.
inline bcos::bytes synthesizeL1AttributesEnvelope(bool jovianActive)
{
    namespace op = bcos::evm::opstack;
    evmc::bytes data(op::IsthmusL1AttributesLen, 0);
    if (jovianActive)
    {
        data.resize(op::JovianL1AttributesLen, 0);
        std::copy(op::JovianL1AttributesSelector.begin(), op::JovianL1AttributesSelector.end(),
            data.begin());
    }
    op::DepositTx deposit{.source_hash = evmc::bytes32{},
        .from = op::OP_DEPOSITOR,
        .to = op::OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1'000'000,
        .is_system_tx = false,
        .data = std::move(data)};
    return bcos::evm::opstack::encodeDepositEnvelope(deposit);
}

/// Minimal CheckpointStorage stub shared by the scheduler test fixtures (both suites live in
/// this directory, so one definition keeps the stub from drifting between them).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // this fixture never needs historical checkpoints.
    }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

/// Big-endian field assertions over a synthesized L1-attributes calldata buffer, shared by the
/// Isthmus and Jovian layout tests.
inline void checkBE(evmc::bytes const& calldata, size_t offset, uint64_t value)
{
    std::array<uint8_t, 8> be{};
    bcos::toBigEndian(value, be);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
}
inline void checkBE256(evmc::bytes const& calldata, size_t offset, intx::uint256 const& value)
{
    std::array<uint8_t, 32> be{};
    intx::be::store(std::span<uint8_t, 32>(be.data(), be.size()), value);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
}
inline void checkU32(evmc::bytes const& calldata, size_t offset, uint32_t value)
{
    std::array<uint8_t, 4> be{static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + 4), be.begin(), be.end());
}
}  // namespace bcos::evm::engine::testutil
