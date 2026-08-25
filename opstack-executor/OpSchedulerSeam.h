// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Engine-facing OP seam. executeBlock exists only for the scheduler concept check.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommitments.h>
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpDepositEncode.h>
#include <cstdint>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::evm::engine
{

namespace detail
{
}  // namespace detail

/// Re-exports the engine newPayload surface as dependent names on SchedulerType.
template <class Storage>
class OpSchedulerSeam
{
public:
    explicit OpSchedulerSeam(bcos::evm::opstack::OpForkFlags forkFlags) : m_forkFlags(forkFlags) {}

    using BlockEnv = bcos::protocol::BlockHeader;
    using ExecuteResult = OpExecuteBlockResult;
    using ConsensusError = OpConsensusError;
    using StorageError = OpStorageError;
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison.
    static bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }

    /// First mismatching field name, or nullopt.
    static std::optional<std::string> mismatchedFieldOf(
        const OpBlockCommitments& computed, const OpBlockCommitments& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes (needed before execution).
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    /// Jovian is active (blobGasUsed is DA footprint; Isthmus keeps it 0).
    [[nodiscard]] bool isJovianActive() const noexcept { return m_forkFlags.jovianActive; }

    OpSchedulerSeam(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam(OpSchedulerSeam&&) = delete;
    OpSchedulerSeam& operator=(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam& operator=(OpSchedulerSeam&&) = delete;
    ~OpSchedulerSeam() = default;

    /// Concept check only. OP mode never calls this.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error("OpSchedulerSeam::executeBlock: not supported in OP mode");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

    /// Fixture L1-attributes deposit: Isthmus 176 zero bytes; Jovian selector + zeros to 178.
    static bcos::bytes synthesizeL1AttributesEnvelope(bool jovianActive)
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
        return encodeDepositEnvelope(deposit);
    }

private:
    bcos::evm::opstack::OpForkFlags m_forkFlags;
};

}  // namespace bcos::evm::engine
