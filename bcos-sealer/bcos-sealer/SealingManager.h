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
#include <bcos-utilities/CallbackCollectionHandler.h>
#include <bcos-utilities/ThreadPool.h>
#include <atomic>

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

    virtual ~SealingManager();
    void stop();

    bool shouldGenerateProposal();

    std::pair<bool, bcos::protocol::Block::Ptr> generateProposal(
        std::function<uint16_t(bcos::protocol::Block::Ptr)>);

    // the consensus module notify the sealer to reset sealing when viewchange
    virtual void resetSealing();
    virtual void resetSealingInfo(
        ssize_t _startSealingNumber, ssize_t _endSealingNumber, size_t _maxTxsPerBlock);

    virtual void resetLatestNumber(int64_t _latestNumber);
    virtual void resetLatestHash(crypto::HashType _latestHash);
    virtual int64_t latestNumber() const;
    virtual crypto::HashType latestHash() const;
    virtual void fetchTransactions();

    template <class T>
    bcos::Handler<> onReady(T callback)
    {
        return m_onReady.add(std::move(callback));
    }
    virtual void notifyResetProposal(bcos::protocol::Block const& _block);

protected:
    virtual void appendTransactions(
        TxsMetaDataQueue& _txsQueue, bcos::protocol::Block const& _fetchedTxs);
    virtual bool reachMinSealTimeCondition();
    virtual void clearPendingTxs();
    virtual void notifyResetTxsFlag(
        bcos::crypto::HashListPtr _txsHash, bool _flag, size_t _retryTime = 0);

    virtual int64_t txsSizeExpectedToFetch();
    virtual size_t pendingTxsSize();

private:
    SealerConfig::Ptr m_config;
    TxsMetaDataQueue m_pendingTxs;
    TxsMetaDataQueue m_pendingSysTxs;
    SharedMutex x_pendingTxs;

    ThreadPool::Ptr m_worker;

    std::atomic<uint64_t> m_lastSealTime = {0};

    // the invalid sealingNumber is -1
    std::atomic<ssize_t> m_sealingNumber = {-1};

    std::atomic<ssize_t> m_startSealingNumber = {0};
    std::atomic<ssize_t> m_endSealingNumber = {0};
    std::atomic<size_t> m_maxTxsPerBlock = {0};

    // for sys block
    std::atomic<int64_t> m_waitUntil = {0};

    bcos::CallbackCollectionHandler<> m_onReady;

    std::atomic_bool m_fetchingTxs = {false};

    std::atomic<ssize_t> m_latestNumber = {0};
    bcos::crypto::HashType m_latestHash;
};
}  // namespace bcos::sealer
