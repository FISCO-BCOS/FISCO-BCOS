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
#include "Common.h"
#include "SealerConfig.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/protocol/TransactionMetaData.h"
#include "bcos-utilities/CallbackCollectionHandler.h"
#include "bcos-utilities/ThreadPool.h"
namespace bcos
{
namespace sealer
{
using TxsMetaDataQueue = std::deque<bcos::protocol::TransactionMetaData::Ptr>;
class SealingManager : public std::enable_shared_from_this<SealingManager>
{
public:
    using Ptr = std::shared_ptr<SealingManager>;
    using ConstPtr = std::shared_ptr<SealingManager const>;
    explicit SealingManager(SealerConfig::Ptr _config)
      : m_config(_config),
        m_pendingTxs(std::make_shared<TxsMetaDataQueue>()),
        m_pendingSysTxs(std::make_shared<TxsMetaDataQueue>()),
        m_worker(std::make_shared<ThreadPool>("sealerWorker", 1))
    {}

    virtual ~SealingManager() { stop(); }

    virtual void stop()
    {
        if (m_worker)
        {
            m_worker->stop();
        }
    }

    virtual bool shouldGenerateProposal();
    virtual bool shouldFetchTransaction();

    std::pair<bool, bcos::protocol::Block::Ptr> generateProposal();
    virtual void setUnsealedTxsSize(size_t _unsealedTxsSize)
    {
        m_unsealedTxsSize = _unsealedTxsSize;
        m_config->consensus()->asyncNoteUnSealedTxsSize(_unsealedTxsSize, [](Error::Ptr _error) {
            if (_error)
            {
                SEAL_LOG(WARNING) << LOG_DESC(
                                         "asyncNoteUnSealedTxsSize to the consensus module failed")
                                  << LOG_KV("code", _error->errorCode())
                                  << LOG_KV("msg", _error->errorMessage());
            }
        });
    }

    // the consensus module notify the sealer to reset sealing when viewchange
    virtual void resetSealing();
    virtual void resetSealingInfo(
        ssize_t _startSealingNumber, ssize_t _endSealingNumber, size_t _maxTxsPerBlock)
    {
        if (_startSealingNumber > _endSealingNumber)
        {
            return;
        }
        // non-continuous sealing request
        if (m_sealingNumber > m_endSealingNumber || _startSealingNumber != (m_endSealingNumber + 1))
        {
            clearPendingTxs();
            m_startSealingNumber = _startSealingNumber;
            m_sealingNumber = _startSealingNumber;
            m_lastSealTime = utcSteadyTime();
        }
        m_endSealingNumber = _endSealingNumber;
        m_maxTxsPerBlock = _maxTxsPerBlock;
        SEAL_LOG(INFO) << LOG_DESC("resetSealingInfo") << LOG_KV("start", m_startSealingNumber)
                       << LOG_KV("end", m_endSealingNumber)
                       << LOG_KV("sealingNumber", m_sealingNumber);
    }

    virtual void resetCurrentNumber(int64_t _currentNumber) { m_currentNumber = _currentNumber; }
    virtual int64_t currentNumber() const { return m_currentNumber; }
    virtual void fetchTransactions();

    template <class T>
    bcos::Handler<> onReady(T const& _t)
    {
        return m_onReady.add(_t);
    }
    virtual void notifyResetProposal(bcos::protocol::Block::Ptr _block);

protected:
    virtual void appendTransactions(
        std::shared_ptr<TxsMetaDataQueue> _txsQueue, bcos::protocol::Block::Ptr _fetchedTxs);
    virtual bool reachMinSealTimeCondition();
    virtual void clearPendingTxs();
    virtual void notifyResetTxsFlag(
        bcos::crypto::HashListPtr _txsHash, bool _flag, size_t _retryTime = 0);

    virtual int64_t txsSizeExpectedToFetch();
    virtual size_t pendingTxsSize();

private:
    SealerConfig::Ptr m_config;
    std::shared_ptr<TxsMetaDataQueue> m_pendingTxs;
    std::shared_ptr<TxsMetaDataQueue> m_pendingSysTxs;
    SharedMutex x_pendingTxs;

    ThreadPool::Ptr m_worker;

    std::atomic<uint64_t> m_lastSealTime = {0};

    // the invalid sealingNumber is -1
    std::atomic<ssize_t> m_sealingNumber = {-1};
    std::atomic<size_t> m_unsealedTxsSize = {0};

    std::atomic<ssize_t> m_startSealingNumber = {0};
    std::atomic<ssize_t> m_endSealingNumber = {0};
    std::atomic<size_t> m_maxTxsPerBlock = {0};

    std::atomic<int64_t> m_waitUntil = {0};

    bcos::CallbackCollectionHandler<> m_onReady;

    std::atomic_bool m_fetchingTxs = {false};

    std::atomic<ssize_t> m_currentNumber = {0};
};
}  // namespace sealer
}  // namespace bcos