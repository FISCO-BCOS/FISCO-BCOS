/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file SealingManager.h
 * @author: yujiechen
 * @date: 2021-05-14
 */
#pragma once
#include "SealerConfig.h"
#include "bcos-framework/protocol/TransactionMetaData.h"
#include <bcos-utilities/ThreadPool.h>
#include <atomic>
#include <functional>

namespace bcos::sealer
{
using TxsMetaDataQueue = std::deque<bcos::protocol::TransactionMetaData::Ptr>;
class SealingManager : public std::enable_shared_from_this<SealingManager>
{
public:
    using Ptr = std::shared_ptr<SealingManager>;
    using ConstPtr = std::shared_ptr<SealingManager const>;

    explicit SealingManager(SealerConfig::Ptr _config);
    SealingManager(const SealingManager&) = delete;
    SealingManager(SealingManager&&) = delete;
    SealingManager& operator=(const SealingManager&) = delete;
    SealingManager& operator=(SealingManager&&) = delete;

    virtual ~SealingManager() noexcept = default;
    virtual bool shouldGenerateProposal();

    std::pair<bool, bcos::protocol::Block::Ptr> generateProposal(
        std::function<uint16_t(bcos::protocol::Block::Ptr)>);

    // Test-only helpers used by FIB-117 regression tests to deterministically
    // race a queue mutator against the unlocked predicate check in
    // generateProposal(). They take the same x_pendingTxs write lock as the
    // production paths, so they are safe to call concurrently with the rest
    // of the public API.
    void testOnlySeedPendingTxs(const std::vector<bcos::protocol::TransactionMetaData::Ptr>& _txs);
    void testOnlyDrainPendingTxs();
    // FIB-153 regression helpers: drive / observe m_waitUntil from tests so
    // we can verify the post-hook update path without having to inject a full
    // sys-tx queue.
    void setWaitUntilForTest(int64_t _waitUntil) { m_waitUntil.store(_waitUntil); }
    int64_t getWaitUntilForTest() const { return m_waitUntil.load(); }

    // the consensus module notify the sealer to reset sealing when viewchange
    virtual void resetSealing();
    virtual void resetSealingInfo(
        ssize_t _startSealingNumber, ssize_t _endSealingNumber, size_t _maxTxsPerBlock);

    virtual void resetLatestNumber(int64_t _latestNumber);
    virtual void resetLatestHash(crypto::HashType _latestHash);
    virtual void resetLatestTimestamp(int64_t _latestTimestamp)
    {
        m_latestTimestamp = _latestTimestamp;
    }
    virtual int64_t latestNumber() const;
    virtual crypto::HashType latestHash() const;
    virtual int64_t latestTimestamp() const { return m_latestTimestamp; }

    enum class FetchResult : int8_t
    {
        SUCCESS,
        NOT_READY,
        NO_TRANSACTION,
    };
    virtual FetchResult fetchTransactions();

    void setOnReadyCallback(std::function<void()> callback) { m_onReady = std::move(callback); }
    virtual void notifyResetProposal(
        const std::vector<protocol::TransactionMetaData::Ptr>& metaDatas);
    virtual void notifyResetTxsFlag(
        const bcos::crypto::HashList& _txsHash, bool _flag, size_t _retryTime = 0);

protected:
    virtual void appendTransactions(TxsMetaDataQueue& _txsQueue,
        const std::vector<protocol::TransactionMetaData::Ptr>& _fetchedTxs);
    virtual void clearPendingTxs();


    virtual int64_t txsSizeExpectedToFetch();
    virtual size_t pendingTxsSize();

    // Single source of truth for the "should we seal the next proposal?"
    // decision. Caller MUST hold x_pendingTxs (read or write). All inputs
    // are atomics or guarded by that lock — no internal locking. Both
    // shouldGenerateProposal() (read lock) and generateProposal()
    // (write lock) call it, so FIB-117's race window is structurally
    // eliminated: generateProposal evaluates this under the same write lock
    // that serialises every queue mutator. Virtual so regression tests
    // (FIB-153) can force entry past it.
    virtual bool meetsProposalPreconditions() const;

private:
    SealerConfig::Ptr m_config;
    TxsMetaDataQueue m_pendingTxs;
    TxsMetaDataQueue m_pendingSysTxs;
    SharedMutex x_pendingTxs;

    std::atomic<uint64_t> m_lastSealTime = {0};
    // the invalid sealingNumber is -1
    std::atomic<ssize_t> m_sealingNumber = {-1};

    std::atomic<ssize_t> m_startSealingNumber = {0};
    std::atomic<ssize_t> m_endSealingNumber = {0};
    std::atomic<size_t> m_maxTxsPerBlock = {0};

    // for sys block
    std::atomic<int64_t> m_waitUntil = {0};

    std::function<void()> m_onReady;
    std::mutex m_fetchingTxsMutex;

    std::atomic<ssize_t> m_latestNumber = {0};
    bcos::crypto::HashType m_latestHash;
    int64_t m_latestTimestamp = 0;
    static constexpr size_t c_maxSysTxsPerBlock = 10;
};
}  // namespace bcos::sealer
