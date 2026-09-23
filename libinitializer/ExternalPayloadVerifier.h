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
 * @file ExternalPayloadVerifier.h
 * @brief Adapts the shared EthereumBlockVerifier to the engine module's
 *        IExternalPayloadVerifier seam (EL-mode external newPayload)
 */

#pragma once

#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-devp2p/sync/HeaderValidator.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"  // complete type for shared_ptr upcast in the decoder
#include "bcos-task/Task.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "engine/bcos-engine/EngineServiceCommon.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <boost/throw_exception.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace bcos::initializer
{
/// The engine module cannot name scheduler_v1::EthereumBlockVerifier (the dependency
/// direction is one-way), so this adapter — constructed by Initializer, injected into
/// EthEngineService — carries everything the verifier call needs that the seam's
/// ExternalPayloadBlock does not: the ledger, the fork schedule, chain id, merge block,
/// and the raw-envelope decoder. The verifier instance is the one shared with the devp2p
/// sync loop (EthereumSyncInitializer), so both commit lanes serialize on its
/// m_commitMutex.
template <class GlobalStateStorage>
class ExternalPayloadVerifierImpl final
    : public engine::engine_common::IExternalPayloadVerifier<GlobalStateStorage>
{
public:
    using Verifier = scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
        executor_v1::eth::EthereumExecutor>;

    ExternalPayloadVerifierImpl(std::shared_ptr<Verifier> verifier,
        bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        scheduler_v1::EvmcForkTimestamps forkSchedule, uint64_t chainId, uint64_t mergeBlock)
      : m_verifier(std::move(verifier)),
        m_ledger(std::move(ledger)),
        m_forkSchedule(forkSchedule),
        m_chainId(chainId),
        m_mergeBlock(mergeBlock)
    {
        auto hashImpl = blockFactory->cryptoSuite()->hashImpl();
        m_decoder = [hashImpl = std::move(hashImpl)](bcos::bytes const& raw) {
            return bcos::rpc::decodeWeb3RawTransaction(
                bcos::bytesConstRef(raw.data(), raw.size()), *hashImpl);
        };
    }

    task::Task<engine::engine_common::ExternalPayloadResult> verifyAndCommit(
        GlobalStateStorage& storage,
        engine::engine_common::ExternalPayloadBlock const& block) override
    {
        using engine::engine_common::ExternalPayloadOutcome;
        using engine::engine_common::ExternalPayloadResult;
        try
        {
            auto result = co_await m_verifier->verifyAndCommit(storage, *m_ledger, block.ethHeader,
                block.parentHeader, block.rawTransactions, block.rawWithdrawals, m_forkSchedule,
                m_chainId, /*rawUncles=*/{}, m_mergeBlock, m_decoder, m_stateRootCalc);
            if (!result.valid)
            {
                co_return ExternalPayloadResult{
                    .outcome = ExternalPayloadOutcome::Invalid, .error = std::move(result.error)};
            }
            co_return ExternalPayloadResult{
                .outcome = ExternalPayloadOutcome::Valid, .error = {}};
        }
        catch (scheduler_v1::StaleOrOutOfOrderBlock const&)
        {
            // Lost the head+1 race against the other commit lane; the caller answers
            // SYNCING so the CL retries and re-reads the head.
            co_return ExternalPayloadResult{
                .outcome = ExternalPayloadOutcome::StaleOrOutOfOrder, .error = {}};
        }
    }

    task::Task<engine::engine_common::ExternalRollbackResult> rollbackToCommitted(
        GlobalStateStorage& storage, bcos::protocol::BlockNumber targetNumber) override
    {
        using engine::engine_common::ExternalRollbackResult;
        try
        {
            co_await m_verifier->rollbackChain(storage, targetNumber);
            co_return ExternalRollbackResult{.rolledBack = true, .error = {}};
        }
        catch (scheduler_v1::RollbackRefused const& e)
        {
            // Beyond the reorg window or a journal row missing: the caller answers SYNCING
            // so the CL backfills from the network instead of retrying a hopeless rewind.
            co_return ExternalRollbackResult{.rolledBack = false, .error = e.what()};
        }
    }

    task::Task<engine::engine_common::ExternalL1Context> deriveL1Context(
        bcos::protocol::EthBlockHeaderData const& parentHeader,
        int64_t timestampSeconds) override
    {
        using engine::engine_common::ExternalL1Context;
        // The builder only ever produces PoS blocks (difficulty 0), which is also the
        // per-block "TTD passed" signal evmcRevisionForTimestamp keys on.
        auto const revision = scheduler_v1::evmcRevisionForTimestamp(
            m_forkSchedule, timestampSeconds, u256(0));
        ExternalL1Context context;
        // Throws UnsupportedFork for a revision this binary cannot map to a header era
        // — the same loud failure the number-keyed derivation had.
        context.forkVersion = engine::detail::ethBlockVersionFor(revision);
        context.baseFee = devp2p::sync::computeNextBaseFee(parentHeader);
        context.excessBlobGas = devp2p::sync::computeNextExcessBlobGas(parentHeader,
            devp2p::sync::toGasSchedule(protocol::blobScheduleForTimestamp(
                protocol::BlobForkTimes{.cancunTime = m_forkSchedule.cancunTime,
                    .pragueTime = m_forkSchedule.pragueTime,
                    .bpo1Time = m_forkSchedule.bpo1Time,
                    .bpo2Time = m_forkSchedule.bpo2Time},
                static_cast<uint64_t>(timestampSeconds))),
            /*_osakaActive=*/revision >= EVMC_OSAKA);
        co_return context;
    }

    task::Task<engine::engine_common::ExternalBuildResult> buildL1Block(
        typename GlobalStateStorage::ViewType& view,
        engine::engine_common::ExternalBuildBlock const& block) override
    {
        using engine::engine_common::ExternalBuildResult;
        // PoS building: no uncles, so the PoW-reward gate inside executeEthereumBlock
        // never fires (difficulty 0 / mergeBlock reached), exactly like the verifier's
        // PoS path. rawUncles stays empty.
        auto execution = co_await m_verifier->executeEthereumBlock(view, block.ethHeader,
            block.parentHeader, block.rawTransactions, block.rawWithdrawals, m_forkSchedule,
            m_chainId, /*rawUncles=*/{}, m_mergeBlock, m_decoder, m_stateRootCalc);
        if (execution.error.has_value())
        {
            ExternalBuildResult failure;
            failure.error = std::move(*execution.error);
            co_return failure;
        }
        ExternalBuildResult result;
        result.ok = true;
        result.transactions = std::move(execution.transactions);
        result.receipts = std::move(execution.receipts);
        result.computation = std::move(execution.computation);
        result.stateRoot = execution.stateRoot;
        if (execution.mptDelta.has_value())
        {
            result.mptDelta =
                std::make_shared<const ledger::mpt::MPTDeltaLayer>(std::move(*execution.mptDelta));
        }
        result.requestsHash = execution.requestsHash;
        result.executionRequests = std::move(execution.executionRequests);
        co_return result;
    }

private:
    std::shared_ptr<Verifier> m_verifier;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    scheduler_v1::EvmcForkTimestamps m_forkSchedule;
    uint64_t m_chainId;
    uint64_t m_mergeBlock;
    typename Verifier::TransactionDecoder m_decoder;
    // v2 always computes the MPT state root itself; the injected calculator must never run.
    typename Verifier::template StateRootCalculator<typename GlobalStateStorage::ViewType>
        m_stateRootCalc = [](typename GlobalStateStorage::ViewType&,
                              uint32_t) -> task::Task<bcos::crypto::HashType> {
        BOOST_THROW_EXCEPTION(std::runtime_error{
            "legacy state-root fold must not run for executor v2 (Ethereum L1 EL mode)"});
    };
};
}  // namespace bcos::initializer
