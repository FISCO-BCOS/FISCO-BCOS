/**
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
 * @brief faker for the TxPool
 * @file FakeTxPool.h
 * @author: yujiechen
 * @date 2021-05-28
 */
#pragma once
#include "../../protocol/CommonError.h"
#include "../../txpool/TxPoolInterface.h"
#include <bcos-utilities/ThreadPool.h>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;


namespace bcos::test
{
class FakeTxPool : public TxPoolInterface
{
public:
    using Ptr = std::shared_ptr<FakeTxPool>;
    FakeTxPool() { m_worker = std::make_shared<ThreadPool>("txpool", 1); }
    ~FakeTxPool() override {}

    void start() override {}
    void stop() override { m_worker->stop(); }

    // useless for PBFT, maybe needed by RPC
    task::Task<protocol::TransactionSubmitResult::Ptr> submitTransaction(
        protocol::Transaction::Ptr transaction, bool /*unused*/) override
    {
        co_return nullptr;
    }
    void asyncResetTxPool(std::function<void(Error::Ptr)> _callback) override
    {
        _callback(nullptr);
    }
    // useless for PBFT, needed by dispatcher to fetch block transactions
    void asyncFillBlock(HashListPtr, std::function<void(Error::Ptr, ConstTransactionsPtr)>) override
    {}

    // useless for PBFT, maybe useful for the ledger
    void asyncNotifyBlockResult(
        BlockNumber, TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>) override
    {}

    // called by frontService to dispatch message
    // useless for PBFT
    void asyncNotifyTxsSyncMessage(Error::Ptr, std::string const&, NodeIDPtr, bytesConstRef,
        std::function<void(Error::Ptr _error)>) override
    {}

    // notify related interfaces: useless for the PBFT module
    void notifyConsensusNodeList(ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}
    // notify related interfaces: useless for the PBFT module
    void notifyObserverNodeList(ConsensusNodeList const&, std::function<void(Error::Ptr)>) override
    {}

    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}
    std::tuple<std::vector<bcos::protocol::TransactionMetaData::Ptr>,
        std::vector<bcos::protocol::TransactionMetaData::Ptr>>
    sealTxs(uint64_t) override
    {
        return {};
    }

    void asyncMarkTxs(const HashList&, bool, bcos::protocol::BlockNumber,
        bcos::crypto::HashType const&, std::function<void(Error::Ptr)>) override
    {}

    void asyncVerifyBlock(PublicPtr, protocol::Block::ConstPtr,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override
    {
        m_worker->enqueue([this, _onVerifyFinished]() {
            if (m_verifyResult)
            {
                _onVerifyFinished(nullptr, m_verifyResult);
                return;
            }
            _onVerifyFinished(
                BCOS_ERROR_UNIQUE_PTR(CommonError::TransactionsMissing, "TransactionsMissing"),
                m_verifyResult);
        });
    }

    void setVerifyResult(bool _verifyResult) { m_verifyResult = _verifyResult; }
    bool verifyResult() const { return m_verifyResult; }

    void asyncGetPendingTransactionSize(std::function<void(Error::Ptr, uint64_t)>) override {}

private:
    bool m_verifyResult = true;
    std::shared_ptr<ThreadPool> m_worker = nullptr;
};
}  // namespace bcos::test
