// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Engine-facing OP seam. executeBlock exists only for the scheduler concept check.

#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpCommon.h>  // OpConsensusError / OpStorageError
#include <opstack-executor/OpEthCommitments.h>  // OpEthExecuteBlockResult / opEthCommitmentsOf / opEthMismatchedFieldOf
#include <opstack-executor/OpEthL1Attributes.h>  // OpEthL1BlockInfo / synthesizeOpEthL1AttributesEnvelope
#include <opstack-executor/OpForkSpec.h>  // OpFork / opForkTimestampSec
#include <cstdint>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::engine
{

/// Re-exports the engine newPayload surface as dependent names on SchedulerType.
template <class Storage>
class OpSchedulerSeam
{
public:
    using OpEthL1BlockInfo = bcos::executor_v1::opstack::OpEthL1BlockInfo;

    explicit OpSchedulerSeam(
        bcos::ledger::OpForkSchedule forkSchedule, OpEthL1BlockInfo l1BlockInfo)
      : m_forkSchedule(forkSchedule), m_l1BlockInfo(std::move(l1BlockInfo))
    {}

    using BlockEnv = bcos::protocol::BlockHeader;
    using ExecuteResult = bcos::executor_v1::opstack::OpEthExecuteBlockResult;
    using ConsensusError = OpConsensusError;
    using StorageError = OpStorageError;
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;
    static bcos::executor_v1::opstack::OpEthBlockCommitments commitmentsOf(
        const ExecuteResult& result)
    {
        return bcos::executor_v1::opstack::opEthCommitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison.
    static bcos::executor_v1::opstack::OpEthBlockCommitments announcedOpEthCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::executor_v1::opstack::announcedOpEthCommitmentsOf(
            payload, transactionsRoot, ethHeader);
    }

    /// First mismatching field name, or nullopt.
    static std::optional<std::string> opEthMismatchedFieldOf(
        const bcos::executor_v1::opstack::OpEthBlockCommitments& computed,
        const bcos::executor_v1::opstack::OpEthBlockCommitments& announced)
    {
        return bcos::executor_v1::opstack::opEthMismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes (needed before execution).
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return bcos::executor_v1::opstack::computeOpEthTransactionsRoot(rawTxBytes);
    }

    /// Jovian semantics or later for a block whose internal (millisecond) timestamp is
    /// @p internalTimestampMs — blobGasUsed is the DA footprint, the operator fee uses the
    /// ×100 formula and extraData is the 17-byte Jovian shape; Isthmus keeps blobGasUsed 0.
    /// Derived from the fork the schedule resolves rather than a single flag: Karst is a
    /// superset of Jovian and leaves the L1-attributes / DA-footprint shape unchanged, and
    /// OpFork is declared in fork order (ledger/OpForkSchedule.h), so `>= Jovian` is the
    /// predicate. The CALLER picks which block's timestamp to pass: op-geth keys base fee on
    /// the parent (eip1559.go CalcBaseFee), op-node keys the L1-attributes layout and the
    /// payload attributes on the child (derive/l1_block_info.go, derive/attributes.go).
    [[nodiscard]] bool isJovianActive(int64_t internalTimestampMs) const noexcept
    {
        return forkAt(internalTimestampMs) >= bcos::ledger::OpFork::Jovian;
    }

    /// Karst semantics for a block whose internal (millisecond) timestamp is
    /// @p internalTimestampMs: Jovian's fee and receipt rules on an Osaka EVM base. Used by
    /// the engine's getPayload method-version gate (V5 is Karst-only, V4 is pre-Karst).
    [[nodiscard]] bool isKarstActive(int64_t internalTimestampMs) const noexcept
    {
        return forkAt(internalTimestampMs) >= bcos::ledger::OpFork::Karst;
    }

    /// Synthesize the L1-attributes deposit envelope from the configured L1 info.
    /// Refuses the unset snapshot sentinel (number/time/hash all zero) and an unset
    /// SystemConfig (zero baseFeeScalar or batcherHash) so a missing CL snapshot cannot
    /// mint a plausible L1-attributes deposit.
    ///
    /// The calldata layout is keyed on the CHILD L2 block's timestamp (op-node
    /// derive/l1_block_info.go L1InfoDeposit(..., l2Timestamp)) with ONE exception: op-node
    /// gates it on `isJovianButNotFirstBlock`, i.e.
    /// `IsJovian(ts) && !IsJovianActivationBlock(ts)` (l1_block_info.go:462-470), so the
    /// ACTIVATION block itself still emits the previous fork's 176-byte Isthmus layout — that
    /// is the block in which the L1Block predeploy is upgraded, and it cannot already speak
    /// the new ABI. `IsJovianActivationBlock(t)` is `IsJovian(t) && !IsJovian(t - blockTime)`,
    /// and `t - blockTime` is exactly the parent's timestamp on an OP chain's fixed cadence,
    /// so passing the parent lets this reproduce op-node's rule without the schedule having to
    /// carry a block_time.
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(
        int64_t l2InternalTimestampMs, int64_t parentInternalTimestampMs) const
    {
        return bcos::executor_v1::opstack::synthesizeOpEthL1AttributesEnvelope(
            m_forkSchedule, m_l1BlockInfo, l2InternalTimestampMs, parentInternalTimestampMs);
    }

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

private:
    /// Single conversion point from internal milliseconds to the schedule's seconds.
    [[nodiscard]] bcos::ledger::OpFork forkAt(int64_t internalTimestampMs) const noexcept
    {
        return bcos::ledger::resolveOpFork(m_forkSchedule,
            bcos::executor_v1::opstack::opForkTimestampSec(internalTimestampMs));
    }

    bcos::ledger::OpForkSchedule m_forkSchedule;
    OpEthL1BlockInfo m_l1BlockInfo;
};

}  // namespace bcos::evm::engine
