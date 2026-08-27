// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

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
#include <algorithm>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

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
}  // namespace bcos::evm::engine::testutil
