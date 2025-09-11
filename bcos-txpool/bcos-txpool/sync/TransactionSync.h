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
 * @brief implementation for transaction sync
 * @file TransactionSync.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once

#include "bcos-crypto/interfaces/crypto/Signature.h"
#include "bcos-txpool/sync/TransactionSyncConfig.h"
#include "bcos-txpool/sync/interfaces/TransactionSyncInterface.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Worker.h>

namespace bcos::sync
{
class TransactionSync : public TransactionSyncInterface,
                        public std::enable_shared_from_this<TransactionSync>
{
public:
    using Ptr = std::shared_ptr<TransactionSync>;
    explicit TransactionSync(TransactionSyncConfig::Ptr config, bool checkTransactionSignature)
      : TransactionSyncInterface(std::move(config)),
        m_checkTransactionSignature(checkTransactionSignature)
    {
        m_hashImpl = m_config->blockFactory()->cryptoSuite()->hashImpl();
        m_signatureImpl = m_config->blockFactory()->cryptoSuite()->signatureImpl();
    }
    TransactionSync(const TransactionSync&) = delete;
    TransactionSync(TransactionSync&&) = delete;
    TransactionSync& operator=(const TransactionSync&) = delete;
    TransactionSync& operator=(TransactionSync&&) = delete;

    ~TransactionSync() override { stop(); };

    using SendResponseCallback = std::function<void(bytesConstRef respData)>;
    void onRecvSyncMessage(bcos::Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, SendResponseCallback _sendResponse) override;

    using VerifyResponseCallback = std::function<void(Error::Ptr, bool)>;
    void requestMissedTxs(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::crypto::HashListPtr _missedTxs, bcos::protocol::Block::ConstPtr _verifiedProposal,
        VerifyResponseCallback _onVerifyFinished) override;

    void onEmptyTxs() override;

    void stop() override;

protected:
    virtual void responseTxsStatus(bcos::crypto::NodeIDPtr _fromNode);

    virtual void onPeerTxsStatus(
        bcos::crypto::NodeIDPtr _fromNode, TxsSyncMsgInterface::Ptr _txsStatus);

    virtual void onReceiveTxsRequest(TxsSyncMsgInterface::Ptr _txsRequest,
        SendResponseCallback _sendResponse, bcos::crypto::PublicPtr _peer);

    // functions called by requestMissedTxs
    virtual void verifyFetchedTxs(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, bcos::crypto::HashListPtr _missedTxs,
        bcos::protocol::Block::ConstPtr _verifiedProposal,
        VerifyResponseCallback _onVerifyFinished);
    virtual void requestMissedTxsFromPeer(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::crypto::HashListPtr _missedTxs, bcos::protocol::Block::ConstPtr _verifiedProposal,
        VerifyResponseCallback _onVerifyFinished);

    virtual size_t onGetMissedTxsFromLedger(std::set<bcos::crypto::HashType>& _missedTxs,
        Error::Ptr _error, bcos::protocol::TransactionsPtr _fetchedTxs,
        bcos::protocol::Block::ConstPtr _verifiedProposal,
        VerifyResponseCallback _onVerifyFinished);

    virtual std::tuple<bool, std::shared_ptr<protocol::Transactions>> importDownloadedTxsByBlock(
        bcos::protocol::Block::Ptr _txsBuffer,
        bcos::protocol::Block::ConstPtr _verifiedProposal = nullptr);

    virtual bool importDownloadedTxs(bcos::protocol::TransactionsPtr _txs,
        bcos::protocol::Block::ConstPtr _verifiedProposal = nullptr);

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    bcos::crypto::SignatureCrypto::Ptr m_signatureImpl;

    bool m_checkTransactionSignature;
};
}  // namespace bcos::sync