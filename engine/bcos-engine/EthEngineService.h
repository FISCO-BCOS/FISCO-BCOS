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
 * @file EthEngineService.h
 * @brief Ethereum Engine API service beside the legacy EngineServiceImpl
 */

#pragma once

#include "EngineServiceCommon.h"
#include "EngineTracker.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{

namespace detail
{
template <class Guard, class ArtifactsMap>
void commitRetainedPayload(Guard& guard, ArtifactsMap& artifacts, PayloadID const& payloadId,
    h256 const& blockHash, BuiltPayloadPtr entry)
{
    PayloadCache cacheRollback = guard.snapshotPayloadCache();
    ArtifactsMap artifactsRollback = artifacts;
    try
    {
        guard.putAndRetainPayload(payloadId, blockHash, std::move(entry));
        artifacts.clear();
    }
    catch (...)
    {
        guard.restorePayloadCache(std::move(cacheRollback));
        artifacts = std::move(artifactsRollback);
        throw;
    }
}

/// Terminal-SYNCING visibility: the CL keeps retrying an answer it cannot move
/// past without EL sync; keep the fail-closed answer but make the gap visible
/// (rate-limited log naming it). One CAS gate (10 s) PER call site (template
/// parameter, one static per instantiation) so a noisy routine site cannot
/// suppress another site's window. The desc names the specific gap at each
/// call site.
enum SyncingWarnSite : std::size_t
{
    c_forkchoiceHeadUnknown,
    c_newPayloadParentUnknown,
    c_newPayloadNotBuiltHere,
    c_newPayloadCacheMiss,
};

template <SyncingWarnSite kSiteV>
inline void warnSyncingRateLimited(const char* desc, h256 const& blockHash)
{
    static std::atomic<std::chrono::steady_clock::time_point> lastWarn{
        std::chrono::steady_clock::time_point{}};
    auto const now = std::chrono::steady_clock::now();
    auto prev = lastWarn.load(std::memory_order_relaxed);
    if (now - prev >= std::chrono::seconds(10) &&
        lastWarn.compare_exchange_strong(
            prev, now, std::memory_order_relaxed, std::memory_order_relaxed))
    {
        BCOS_LOG(WARNING) << LOG_BADGE("EthEngineService") << LOG_DESC(desc)
                          << LOG_KV("blockHash", blockHash.hex());
    }
}

/// Unlimited variant for the mid-persist alarm: a fully built block lost
/// mid-commit must never be dropped by another site's 10 s window.
inline void warnSyncingUnlimited(const char* desc, h256 const& blockHash)
{
    BCOS_LOG(WARNING) << LOG_BADGE("EthEngineService") << LOG_DESC(desc)
                      << LOG_KV("blockHash", blockHash.hex());
}
}  // namespace detail

template <class ViewType>
struct EthPayloadArtifacts
{
    std::shared_ptr<ViewType> view;
    bcos::protocol::BlockHeader::Ptr header;
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
};

template <class MemPoolType, class GlobalStateStorageType, class ExecutorType, class SchedulerType>
    requires executor_v1::TransactionExecutor<ExecutorType,
                 typename GlobalStateStorageType::ViewType> &&
             scheduler_v1::TransactionScheduler<SchedulerType,
                 typename GlobalStateStorageType::ViewType, ExecutorType,
                 std::vector<protocol::Transaction::Ptr>>
class EthEngineService
{
public:
    using ViewType = typename GlobalStateStorageType::ViewType;

    EthEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage,
        ExecutorType& executor, SchedulerType& scheduler,
        bcos::protocol::BlockFactory::Ptr blockFactory,
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr,
        int64_t blockTxCountLimit = c_defaultBlockTxCountLimit,
        std::uint32_t maxEngineVersion = static_cast<std::uint32_t>(ApiVersion::V3))
      : m_memPool(memPool),
        m_globalStateStorage(globalStateStorage),
        m_executor(executor),
        m_scheduler(scheduler),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_blockTxCountLimit(blockTxCountLimit),
        m_maxEngineVersion(maxEngineVersion)
    {
        if (!m_blockFactory)
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"blockFactory must not be null"});
        }
    }
    ~EthEngineService() = default;
    EthEngineService(const EthEngineService&) = delete;
    EthEngineService(EthEngineService&&) = delete;
    EthEngineService& operator=(const EthEngineService&) = delete;
    EthEngineService& operator=(EthEngineService&&) = delete;

    /// Version-window contract (the four surfaces deliberately differ, matching
    /// op-geth's by-design ceiling split; do not "fix" them to agree):
    /// - exchangeCapabilities: the FULL supported list regardless of
    ///   m_maxEngineVersion — capability advertisement is static, method windows
    ///   are what gate actual dispatch.
    /// - updateForkchoice (FCU): instance-gated by m_maxEngineVersion (V1–V3 for
    ///   the default-constructed service).
    /// - newPayload: V1–V4 (Isthmus V4 empty-lists shape).
    /// - getPayload: V1–V5 via the tracker's window (the V2 build answers V1–V2,
    ///   the V3 build answers V1–V5).
    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        co_return engine_common::supportedCapabilities();
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes* payloadAttributes, std::uint32_t version);

    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        co_return m_tracker.getPayload(payloadId, version);
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version);

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        return m_tracker.safeBlockNumber();
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        return m_tracker.finalizedBlockNumber();
    }

private:
    struct BuildPayloadResult
    {
        ExecutionPayload executionPayload;
        bcos::protocol::BlockHeader::Ptr header;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
    };

    bool isForkchoiceVersionSupported(std::uint32_t version) const
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= m_maxEngineVersion;
    }

    static bool isNewPayloadVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(ApiVersion::V4);
    }

    task::Task<BuildPayloadResult> buildPayload(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version, bcos::protocol::BlockNumber nextBlockNumber,
        std::vector<protocol::Transaction::Ptr> sealedTxs, ViewType& view,
        std::vector<bcos::bytes> const& decodedForcedTxs) const;

    task::Task<h256> calculateStateRoot(ViewType& view, uint32_t blockVersion) const;

    EngineTracker m_tracker;
    std::unordered_map<PayloadID, EthPayloadArtifacts<ViewType>> m_artifacts;
    MemPoolType& m_memPool;
    GlobalStateStorageType& m_globalStateStorage;
    ExecutorType& m_executor;
    SchedulerType& m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    int64_t m_blockTxCountLimit;
    std::uint32_t m_maxEngineVersion;
};

}  // namespace bcos::engine

#include "EthEngineService.inl"
