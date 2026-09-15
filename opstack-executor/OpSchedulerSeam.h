// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Engine-facing OP seam. executeBlock exists only for the scheduler concept check.
//
// Accepts BOTH fork-schedule declarations the merged genesis surface allows:
//  - the karst line's canonical [op_fork_schedule] (ledger-codec validated, any
//    contiguous EL fork range) via the shared_ptr<opstack::OpForkSchedule> ctor, and
//  - the release line's [op_fork_timestamps] shorthand (jovian_time/karst_time on the
//    Isthmus baseline) via the bcos::ledger::OpForkSchedule ctor, which converts to the
//    canonical form so a single schedule member serves every fork query.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
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
#include <limits>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::engine
{

namespace detail
{
[[nodiscard]] inline std::optional<bcos::engine::OpForkId> tryEngineForkId(
    bcos::evm::opstack::OpFork fork) noexcept
{
    switch (fork)
    {
    case bcos::evm::opstack::OpFork::Regolith:
        return bcos::engine::OpForkId::Regolith;
    case bcos::evm::opstack::OpFork::Canyon:
        return bcos::engine::OpForkId::Canyon;
    case bcos::evm::opstack::OpFork::Ecotone:
        return bcos::engine::OpForkId::Ecotone;
    case bcos::evm::opstack::OpFork::Fjord:
        return bcos::engine::OpForkId::Fjord;
    case bcos::evm::opstack::OpFork::Granite:
        return bcos::engine::OpForkId::Granite;
    case bcos::evm::opstack::OpFork::Holocene:
        return bcos::engine::OpForkId::Holocene;
    case bcos::evm::opstack::OpFork::Isthmus:
        return bcos::engine::OpForkId::Isthmus;
    case bcos::evm::opstack::OpFork::Jovian:
        return bcos::engine::OpForkId::Jovian;
    case bcos::evm::opstack::OpFork::Karst:
        return bcos::engine::OpForkId::Karst;
    }
    // Unreachable for a valid OpFork; guards an out-of-range cast instead of
    // falling off the end of a non-void function.
    return std::nullopt;
}
}  // namespace detail

/// Re-exports the engine newPayload surface as dependent names on SchedulerType.
template <class Storage>
class OpSchedulerSeam
{
public:
    explicit OpSchedulerSeam(std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> schedule,
        bcos::evm::opstack::L1BlockInfo l1BlockInfo)
      : m_schedule(std::move(schedule)), m_l1BlockInfo(std::move(l1BlockInfo))
    {
        if (!m_schedule)
        {
            throw std::invalid_argument("OpSchedulerSeam: null fork schedule");
        }
    }

    explicit OpSchedulerSeam(
        bcos::ledger::OpForkSchedule forkSchedule, bcos::evm::opstack::L1BlockInfo l1BlockInfo)
      : OpSchedulerSeam(std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
                            bcos::evm::opstack::OpForkSchedule::fromLedgerSchedule(forkSchedule)),
            std::move(l1BlockInfo))
    {}

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

    [[nodiscard]] bcos::engine::OpForkId forkIdAt(uint64_t timestampSeconds) const
    {
        if (auto const id = detail::tryEngineForkId(m_schedule->forkAt(timestampSeconds)))
        {
            return *id;
        }
        bcos::ledger::throwInvalidOpForkSchedule("OpSchedulerSeam: unsupported schedule fork");
    }

    [[nodiscard]] bcos::engine::EngineApiProfile engineApiFor(uint64_t timestampSeconds) const
    {
        return bcos::engine::engineApiProfileFor(forkIdAt(timestampSeconds));
    }

    [[nodiscard]] const bcos::evm::opstack::OpForkConfig& configAt(uint64_t timestampSeconds) const
    {
        return m_schedule->configAt(timestampSeconds);
    }

    /// Q5 window: Jovian+ activation live at `blockTsSec` but not at `parentTsSec`.
    /// Both arguments are Unix seconds.
    [[nodiscard]] bool isNoUserTxActivationBlock(uint64_t parentTsSec, uint64_t blockTsSec) const
    {
        return bcos::evm::opstack::isNoUserTxActivationBlock(*m_schedule, parentTsSec, blockTsSec);
    }

    /// Jovian semantics or later for a block whose internal (millisecond) timestamp is
    /// @p internalTimestampMs — blobGasUsed is the DA footprint, the operator fee uses the
    /// ×100 formula and extraData is the 17-byte Jovian shape; Isthmus keeps blobGasUsed 0.
    /// Derived from the fork the schedule resolves rather than a single flag: Karst is a
    /// superset of Jovian and leaves the L1-attributes / DA-footprint shape unchanged, and
    /// OpFork is declared in fork order (OpForkSchedule.h), so `>= Jovian` is the predicate.
    /// The CALLER picks which block's timestamp to pass: op-geth keys base fee on the parent
    /// (eip1559.go CalcBaseFee), op-node keys the L1-attributes layout and the payload
    /// attributes on the child (derive/l1_block_info.go, derive/attributes.go).
    [[nodiscard]] bool isJovianActive(int64_t internalTimestampMs) const noexcept
    {
        return static_cast<int>(m_schedule->forkAt(detail::forkTimestampSec(
                   internalTimestampMs))) >= static_cast<int>(bcos::evm::opstack::OpFork::Jovian);
    }

    /// Karst semantics for a block whose internal (millisecond) timestamp is
    /// @p internalTimestampMs: Jovian's fee and receipt rules on an Osaka EVM base. Used by
    /// the engine's getPayload method-version gate (V5 is Karst-only, V4 is pre-Karst).
    [[nodiscard]] bool isKarstActive(int64_t internalTimestampMs) const noexcept
    {
        return static_cast<int>(m_schedule->forkAt(detail::forkTimestampSec(
                   internalTimestampMs))) >= static_cast<int>(bcos::evm::opstack::OpFork::Karst);
    }

    /// `timestampSeconds` is Unix seconds. Callers must convert payload/header internal
    /// milliseconds with `unixSecondsFromInternalMillis`. Never pass raw header.timestamp().
    [[nodiscard]] bcos::engine::EngineForkResolution resolveEngineForkAt(
        uint64_t timestampSeconds) const
    {
        if (timestampSeconds < m_schedule->baselineTimestamp())
        {
            return bcos::engine::OpForkResolutionError::UnsupportedTimestamp;
        }
        auto const forkId = detail::tryEngineForkId(m_schedule->forkAt(timestampSeconds));
        if (!forkId.has_value())
        {
            return bcos::engine::OpForkResolutionError::UnsupportedTimestamp;
        }
        const auto& cfg = m_schedule->configAt(timestampSeconds);
        if (*forkId == bcos::engine::OpForkId::Karst && cfg.rev != EVMC_OSAKA)
        {
            return bcos::engine::OpForkResolutionError::InconsistentExecutionConfig;
        }
        return bcos::engine::EngineForkContext{
            .forkId = *forkId,
            .api = engineApiFor(timestampSeconds),
            .hasDaFootprint = cfg.has_da_footprint,
            .extraDataLayout = bcos::engine::extraDataLayoutFor(*forkId),
        };
    }

    /// Synthesize the L1-attributes deposit envelope from the configured L1 info.
    /// Refuses the unset snapshot sentinel (number/time/hash all zero) and an unset
    /// SystemConfig (zero baseFeeScalar or batcherHash) so a missing CL snapshot cannot
    /// mint a plausible L1-attributes deposit.
    /// `timestampSeconds` is the block Unix seconds (FCU attrs / payload), never configAt(0).
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(uint64_t timestampSeconds) const
    {
        refuseUnsetSynthesisInputs();
        return bcos::evm::opstack::synthesizeL1AttributesDeposit(
            m_l1BlockInfo, configAt(timestampSeconds).has_da_footprint);
    }

    /// Two-timestamp overload keyed on internal milliseconds: the calldata layout is picked
    /// by the CHILD block's fork, but the Jovian ACTIVATION block still emits the previous
    /// fork's Isthmus layout (op-node's isJovianButNotFirstBlock, derive/l1_block_info.go:462)
    /// because the L1Block predeploy is upgraded by that very block — the PARENT's fork is
    /// what distinguishes the activation block from every later Jovian block.
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(
        int64_t l2InternalTimestampMs, int64_t parentInternalTimestampMs) const
    {
        refuseUnsetSynthesisInputs();
        const bool jovianLayout =
            isJovianActive(l2InternalTimestampMs) && isJovianActive(parentInternalTimestampMs);
        return bcos::evm::opstack::synthesizeL1AttributesDeposit(m_l1BlockInfo, jovianLayout);
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
    void refuseUnsetSynthesisInputs() const
    {
        if (bcos::evm::opstack::isUnsetL1BlockInfo(m_l1BlockInfo))
        {
            throw std::invalid_argument(
                "OpSchedulerSeam: refuse to synthesize L1-attributes from an unset "
                "L1BlockInfo (number, time, and blockHash are all zero)");
        }
        if (bcos::evm::opstack::isUnsetSystemConfig(m_l1BlockInfo))
        {
            throw std::invalid_argument(
                "OpSchedulerSeam: refuse to synthesize L1-attributes with an unset "
                "SystemConfig (baseFeeScalar and batcherHash must be non-zero)");
        }
    }


    std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> m_schedule;
    bcos::evm::opstack::L1BlockInfo m_l1BlockInfo;
};

}  // namespace bcos::evm::engine
