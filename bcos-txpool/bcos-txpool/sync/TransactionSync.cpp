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
 * @file TransactionSync.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "bcos-txpool/sync/TransactionSync.h"
#include "bcos-txpool/sync/utilities/Common.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/Protocol.h>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::ledger;
using namespace bcos::consensus;

void TransactionSync::onRecvSyncMessage(
    Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data, SendResponseCallback _sendResponse)
{
    try
    {
        if (_error != nullptr)
        {
            SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
            return;
        }
        // reject the message from the outside-group
        if (!m_config->existsInGroup(_nodeID))
        {
            return;
        }
        auto txsSyncMsg = m_config->msgFactory()->createTxsSyncMsg(_data);
        // receive txs request, and response the transactions
        if (txsSyncMsg->type() == TxsSyncPacketType::TxsRequestPacket)
        {
            auto self = weak_from_this();
            m_worker->enqueue([self, txsSyncMsg, _sendResponse, _nodeID]() {
                try
                {
                    auto transactionSync = self.lock();
                    if (!transactionSync)
                    {
                        return;
                    }
                    transactionSync->onReceiveTxsRequest(txsSyncMsg, _sendResponse, _nodeID);
                }
                catch (std::exception const& e)
                {
                    SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage: send txs response exception")
                                      << LOG_KV("message", boost::diagnostic_information(e))
                                      << LOG_KV("peer", _nodeID->shortHex());
                }
            });
        }
        if (txsSyncMsg->type() == TxsSyncPacketType::TxsStatusPacket)
        {
            auto self = weak_from_this();
            m_txsRequester->enqueue([self, _nodeID, txsSyncMsg]() {
                try
                {
                    auto transactionSync = self.lock();
                    if (!transactionSync)
                    {
                        return;
                    }
                    transactionSync->onPeerTxsStatus(_nodeID, txsSyncMsg);
                }
                catch (std::exception const& e)
                {
                    SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage:  onPeerTxsStatus exception")
                                      << LOG_KV("message", boost::diagnostic_information(e))
                                      << LOG_KV("peer", _nodeID->shortHex());
                }
            });
        }
    }
    catch (std::exception const& e)
    {
        SYNC_LOG(WARNING) << LOG_DESC("onRecvSyncMessage exception")
                          << LOG_KV("message", boost::diagnostic_information(e))
                          << LOG_KV("peer", _nodeID->shortHex());
    }
}

void TransactionSync::onReceiveTxsRequest(TxsSyncMsgInterface::Ptr _txsRequest,
    SendResponseCallback _sendResponse, bcos::crypto::PublicPtr _peer)
{
    auto const& txsHash = _txsRequest->txsHash();
    HashList missedTxs;
    auto txs = m_config->txpoolStorage()->fetchTxs(missedTxs, txsHash);
    // Note: here assume that all the transaction should be hit in the txpool
    if (!missedTxs.empty())
    {
        SYNC_LOG(DEBUG) << LOG_DESC("onReceiveTxsRequest: transaction missing")
                        << LOG_KV("missedTxsSize", missedTxs.size())
                        << LOG_KV("peer", _peer ? _peer->shortHex() : "unknown")
                        << LOG_KV("nodeId", m_config->nodeID()->shortHex());
    }
    // response the txs
    auto block = m_config->blockFactory()->createBlock();
    for (const auto& constTx : *txs)
    {
        auto tx = std::const_pointer_cast<Transaction>(constTx);
        block->appendTransaction(tx);
    }
    bytes txsData = {};
    block->encode(txsData);
    auto txsResponse = m_config->msgFactory()->createTxsSyncMsg(
        TxsSyncPacketType::TxsResponsePacket, std::move(txsData));
    auto packetData = txsResponse->encode();
    _sendResponse(ref(*packetData));
    SYNC_LOG(INFO) << LOG_DESC("onReceiveTxsRequest: response txs")
                   << LOG_KV("peer", _peer ? _peer->shortHex() : "unknown")
                   << LOG_KV("txsSize", txs->size());
}

void TransactionSync::requestMissedTxs(PublicPtr _generatedNodeID, HashListPtr _missedTxs,
    Block::Ptr _verifiedProposal, std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    auto missedTxsSet =
        std::make_shared<std::set<HashType>>(_missedTxs->begin(), _missedTxs->end());
    auto startT = utcTime();
    auto self = weak_from_this();
    m_config->ledger()->asyncGetBatchTxsByHashList(_missedTxs, false,
        [self, startT, _verifiedProposal, missedTxsSet, _generatedNodeID, _onVerifyFinished](
            Error::Ptr _error, TransactionsPtr _fetchedTxs, auto&&) {
            auto txsSync = self.lock();
            if (!txsSync)
            {
                return;
            }
            // hit all the txs
            auto missedTxsSize = txsSync->onGetMissedTxsFromLedger(
                *missedTxsSet, _error, _fetchedTxs, _verifiedProposal, _onVerifyFinished);
            if (missedTxsSize == 0)
            {
                SYNC_LOG(INFO) << LOG_DESC("missedTxsSize == 0");
                return;
            }
            if (!_generatedNodeID ||
                _generatedNodeID->data() == txsSync->m_config->nodeID()->data())
            {
                SYNC_LOG(WARNING)
                    << LOG_DESC("requestMissedTxs failed from the ledger for Transaction missing")
                    << LOG_KV("missedTxs", missedTxsSize);
                _onVerifyFinished(
                    BCOS_ERROR_PTR(CommonError::TransactionsMissing,
                        "requestMissedTxs failed from the ledger for Transaction missing"),
                    false);
                return;
            }
            // fetch missed txs from the given peer
            auto ledgerMissedTxs =
                std::make_shared<HashList>(missedTxsSet->begin(), missedTxsSet->end());
            SYNC_LOG(INFO)
                << LOG_DESC("requestMissedTxs: missing txs from ledger and fetch from the peer")
                << LOG_KV("txsSize", ledgerMissedTxs->size())
                << LOG_KV("peer", _generatedNodeID->shortHex())
                << LOG_KV("readDBTime", utcTime() - startT)
                << LOG_KV("consNum", _verifiedProposal && _verifiedProposal->blockHeader() ?
                                         _verifiedProposal->blockHeader()->number() :
                                         -1)
                << LOG_KV("hash", _verifiedProposal && _verifiedProposal->blockHeader() ?
                                      _verifiedProposal->blockHeader()->hash().abridged() :
                                      "null");
            txsSync->requestMissedTxsFromPeer(
                _generatedNodeID, ledgerMissedTxs, _verifiedProposal, _onVerifyFinished);
        });
}

size_t TransactionSync::onGetMissedTxsFromLedger(std::set<HashType>& _missedTxs, Error::Ptr _error,
    TransactionsPtr _fetchedTxs, Block::Ptr _verifiedProposal,
    VerifyResponseCallback _onVerifyFinished)
{
    if (_error != nullptr)
    {
        SYNC_LOG(INFO) << LOG_DESC("onGetMissedTxsFromLedger: get error response")
                        << LOG_KV("_missedTxs.size()", _missedTxs.size())
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
        return _missedTxs.size();
    }
    // import and verify the transactions
    auto ret = importDownloadedTxs(_fetchedTxs, std::move(_verifiedProposal));
    if (!ret)
    {
        SYNC_LOG(WARNING) << LOG_DESC("onGetMissedTxsFromLedger: verify tx failed");
        return _missedTxs.size();
    }
    // fetch missed transactions from the local ledger
    for (const auto& tx : *_fetchedTxs)
    {
        auto it = _missedTxs.find(tx->hash());
        if (it == _missedTxs.end())
        {
            SYNC_LOG(WARNING) << LOG_DESC(
                                     "onGetMissedTxsFromLedger: Encounter transaction that was "
                                     "not expected to fetch from the ledger")
                              << LOG_KV("tx", tx->hash().abridged());
            continue;
        }
        // update the missedTxs
        _missedTxs.erase(it);
    }
    if (_missedTxs.empty() && _onVerifyFinished)
    {
        SYNC_LOG(DEBUG) << LOG_DESC("onGetMissedTxsFromLedger: hit all transactions");
        _onVerifyFinished(nullptr, true);
    }
    SYNC_LOG(TRACE) << LOG_DESC("onGetMissedTxsFromLedger: missing txs")
                    << LOG_KV("missCount", _missedTxs.size());
    return _missedTxs.size();
}

void TransactionSync::requestMissedTxsFromPeer(PublicPtr _generatedNodeID, HashListPtr _missedTxs,
    Block::Ptr _verifiedProposal, std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    BlockHeader::Ptr proposalHeader = nullptr;
    if (_verifiedProposal)
    {
        proposalHeader = _verifiedProposal->blockHeader();
    }
    if (_missedTxs->empty() && _onVerifyFinished)
    {
        _onVerifyFinished(nullptr, true);
        return;
    }


    auto protocolID = _verifiedProposal ? ModuleID::ConsTxsSync : ModuleID::TxsSync;

    auto txsRequest =
        m_config->msgFactory()->createTxsSyncMsg(TxsSyncPacketType::TxsRequestPacket, *_missedTxs);
    auto encodedData = txsRequest->encode();
    auto startT = utcTime();
    auto self = weak_from_this();
    m_config->frontService()->asyncSendMessageByNodeID(protocolID, std::move(_generatedNodeID),
        ref(*encodedData), m_config->networkTimeout(),
        [self, startT, _missedTxs, _verifiedProposal, proposalHeader, _onVerifyFinished](
            auto&& _error, auto&& _nodeID, bytesConstRef _data, const std::string&, auto&&) {
            try
            {
                auto transactionSync = self.lock();
                if (!transactionSync)
                {
                    return;
                }
                auto networkT = utcTime() - startT;
                auto recordT = utcTime();
                transactionSync->verifyFetchedTxs(_error, _nodeID, _data, _missedTxs,
                    _verifiedProposal,
                    [networkT, recordT, proposalHeader, _onVerifyFinished](
                        Error::Ptr _error, bool _result) {
                        if (!_onVerifyFinished)
                        {
                            return;
                        }
                        _onVerifyFinished(std::move(_error), _result);
                        if (!(proposalHeader))
                        {
                            return;
                        }
                        SYNC_LOG(DEBUG)
                            << LOG_DESC("requestMissedTxs: response verify result")
                            << LOG_KV("propIndex", proposalHeader->number())
                            << LOG_KV("propHash", proposalHeader->hash().abridged())
                            << LOG_KV("_result", _result) << LOG_KV("networkT", networkT)
                            << LOG_KV("verifyAndSubmitT", (utcTime() - recordT));
                    });
            }
            catch (std::exception const& e)
            {
                SYNC_LOG(WARNING)
                    << LOG_DESC(
                           "requestMissedTxs: verifyFetchedTxs when recv txs response exception")
                    << LOG_KV("message", boost::diagnostic_information(e))
                    << LOG_KV("_peer", _nodeID->shortHex());
            }
        });
}

void TransactionSync::verifyFetchedTxs(Error::Ptr _error, NodeIDPtr _nodeID, bytesConstRef _data,
    HashListPtr _missedTxs, Block::Ptr _verifiedProposal, VerifyResponseCallback _onVerifyFinished)
{
    auto startT = utcTime();
    auto recordT = utcTime();
    if (_error != nullptr)
    {
        SYNC_LOG(INFO) << LOG_DESC("asyncVerifyBlock: fetch missed txs failed")
                       << LOG_KV("peer", _nodeID ? _nodeID->shortHex() : "unknown")
                       << LOG_KV("missedTxsSize", _missedTxs->size())
                       << LOG_KV("code", _error->errorCode())
                       << LOG_KV("msg", _error->errorMessage())
                       << LOG_KV(
                              "propHash", (_verifiedProposal && _verifiedProposal->blockHeader()) ?
                                              _verifiedProposal->blockHeader()->hash().abridged() :
                                              "unknown")
                       << LOG_KV(
                              "propIndex", (_verifiedProposal && _verifiedProposal->blockHeader()) ?
                                               _verifiedProposal->blockHeader()->number() :
                                               -1);
        _onVerifyFinished(_error, false);
        return;
    }
    auto txsResponse = m_config->msgFactory()->createTxsSyncMsg(_data);
    auto error = nullptr;
    if (txsResponse->type() != TxsSyncPacketType::TxsResponsePacket)
    {
        SYNC_LOG(WARNING) << LOG_DESC("requestMissedTxs: receive invalid txsResponse")
                          << LOG_KV("peer", _nodeID->shortHex())
                          << LOG_KV("expectedType", TxsSyncPacketType::TxsResponsePacket)
                          << LOG_KV("recvType", txsResponse->type());
        _onVerifyFinished(
            BCOS_ERROR_PTR(CommonError::FetchTransactionsFailed, "FetchTransactionsFailed"), false);
        return;
    }
    // verify missedTxs
    auto transactions = m_config->blockFactory()->createBlock(txsResponse->txsData(), true, false);
    auto decodeT = utcTime() - startT;
    startT = utcTime();
    BlockHeader::Ptr proposalHeader = nullptr;
    if (_verifiedProposal)
    {
        proposalHeader = _verifiedProposal->blockHeader();
    }
    if (_missedTxs->size() != transactions->transactionsSize())
    {
        SYNC_LOG(INFO) << LOG_DESC("verifyFetchedTxs failed")
                       << LOG_KV("expectedTxs", _missedTxs->size())
                       << LOG_KV("fetchedTxs", transactions->transactionsSize())
                       << LOG_KV("peer", _nodeID->shortHex())
                       << LOG_KV("hash",
                              (proposalHeader) ? proposalHeader->hash().abridged() : "unknown")
                       << LOG_KV("consNum", (proposalHeader) ? proposalHeader->number() : -1);
        // response to verify result
        _onVerifyFinished(
            BCOS_ERROR_PTR(CommonError::TransactionsMissing, "TransactionsMissing"), false);
        // try to import the transactions even when verify failed
        importDownloadedTxs(transactions);
        return;
    }
    if (!importDownloadedTxs(transactions, _verifiedProposal))
    {
        _onVerifyFinished(BCOS_ERROR_PTR(CommonError::TxsSignatureVerifyFailed,
                              "invalid transaction for invalid signature or nonce or blockLimit"),
            false);
        return;
    }
    // check the transaction hash
    for (size_t i = 0; i < _missedTxs->size(); i++)
    {
        if ((*_missedTxs)[i] != transactions->transaction(i)->hash())
        {
            _onVerifyFinished(
                BCOS_ERROR_PTR(CommonError::InconsistentTransactions, "InconsistentTransactions"),
                false);
            return;
        }
    }
    _onVerifyFinished(error, true);
    SYNC_LOG(DEBUG) << METRIC << LOG_DESC("requestMissedTxs and verify success")
                    << LOG_KV(
                           "hash", (proposalHeader) ? proposalHeader->hash().abridged() : "unknown")
                    << LOG_KV("consNum", (proposalHeader) ? proposalHeader->number() : -1)
                    << LOG_KV("decodeT", decodeT) << LOG_KV("importT", (utcTime() - startT))
                    << LOG_KV("timecost", (utcTime() - recordT));
}

bool TransactionSync::importDownloadedTxs(Block::Ptr _txsBuffer, Block::Ptr _verifiedProposal)
{
    auto txs = std::make_shared<Transactions>();
    for (size_t i = 0; i < _txsBuffer->transactionsSize(); i++)
    {
        txs->emplace_back(std::const_pointer_cast<Transaction>(_txsBuffer->transaction(i)));
    }
    return importDownloadedTxs(std::move(txs), std::move(_verifiedProposal));
}

bool TransactionSync::importDownloadedTxs(TransactionsPtr _txs, Block::Ptr _verifiedProposal)
{
    if (_txs->empty())
    {
        return true;
    }
    auto txsSize = _txs->size();
    // Note: only need verify the signature for the transactions
    bool enforceImport = false;
    BlockHeader::Ptr proposalHeader = nullptr;
    if (_verifiedProposal && _verifiedProposal->blockHeader())
    {
        proposalHeader = _verifiedProposal->blockHeader();
        enforceImport = true;
    }
    auto recordT = utcTime();
    auto startT = utcTime();
    // verify the transactions
    std::atomic_bool verifySuccess = {true};
    tbb::parallel_for(tbb::blocked_range<size_t>(0, txsSize),
        [&_txs, &_verifiedProposal, &proposalHeader, this, &verifySuccess](
            const tbb::blocked_range<size_t>& _range) {
            for (size_t i = _range.begin(); i < _range.end(); i++)
            {
                auto tx = (*_txs)[i];
                if (!tx)
                {
                    continue;
                }
                if (_verifiedProposal && proposalHeader)
                {
                    tx->setBatchId(proposalHeader->number());
                    tx->setBatchHash(proposalHeader->hash());
                }
                if (m_config->txpoolStorage()->exist(tx->hash()))
                {
                    continue;
                }
                try
                {
                    tx->verify(*m_hashImpl, *m_signatureImpl);
                }
                catch (std::exception const& e)
                {
                    tx->setInvalid(true);
                    SYNC_LOG(WARNING) << LOG_DESC("verify sender for tx failed")
                                      << LOG_KV("reason", boost::diagnostic_information(e))
                                      << LOG_KV("hash", tx->hash().abridged());
                    verifySuccess = false;
                }
            }
        });
    if (enforceImport && !verifySuccess)
    {
        return false;
    }
    auto verifyT = utcTime() - startT;
    startT = utcTime();
    // import the transactions into txpool
    auto txpoolStorage = m_config->txpoolStorage();
    if (enforceImport)
    {
        if (!txpoolStorage->batchVerifyAndSubmitTransaction(proposalHeader, _txs))
        {
            return false;
        }
    }
    else
    {
        txpoolStorage->batchImportTxs(_txs);
    }
    SYNC_LOG(DEBUG) << LOG_DESC("importDownloadedTxs success")
                    << LOG_KV("hash", proposalHeader ? proposalHeader->hash().abridged() : "none")
                    << LOG_KV("number", proposalHeader ? proposalHeader->number() : -1)
                    << LOG_KV("totalTxs", txsSize) << LOG_KV("verifyT", verifyT)
                    << LOG_KV("submitT", (utcTime() - startT))
                    << LOG_KV("timecost", (utcTime() - recordT));
    return true;
}

void TransactionSync::onPeerTxsStatus(NodeIDPtr _fromNode, TxsSyncMsgInterface::Ptr _txsStatus)
{
    // Note: after txpool broadcast every tx before submit, this method only used for onEmptyTx
    // status response
    if (_txsStatus->txsHash().empty())
    {
        responseTxsStatus(_fromNode);
        return;
    }
    auto requestTxs = m_config->txpoolStorage()->filterUnknownTxs(_txsStatus->txsHash(), _fromNode);
    if (requestTxs->empty())
    {
        return;
    }
    requestMissedTxsFromPeer(_fromNode, requestTxs, nullptr, nullptr);
    SYNC_LOG(DEBUG) << LOG_DESC("onPeerTxsStatus") << LOG_KV("reqSize", requestTxs->size())
                    << LOG_KV("peerTxsSize", _txsStatus->txsHash().size())
                    << LOG_KV("peer", _fromNode->shortHex());
}

void TransactionSync::responseTxsStatus(NodeIDPtr _fromNode)
{
    if (m_config->txpoolStorage()->size() == 0)
    {
        return;
    }
    // TODO: get tx directly, not get txHash and request tx indirectly
    auto txsHash =
        m_config->txpoolStorage()->getTxsHash(m_config->getMaxResponseTxsToNodesWithEmptyTxs());
    auto txsStatus =
        m_config->msgFactory()->createTxsSyncMsg(TxsSyncPacketType::TxsStatusPacket, *txsHash);
    auto packetData = txsStatus->encode();
    m_config->frontService()->asyncSendMessageByNodeID(
        ModuleID::TxsSync, _fromNode, ref(*packetData), 0, nullptr);
    SYNC_LOG(DEBUG) << LOG_DESC("onPeerTxsStatus: receive empty txsStatus and responseTxsStatus")
                    << LOG_KV("to", _fromNode->shortHex()) << LOG_KV("txsSize", txsHash->size())
                    << LOG_KV("packetSize", packetData->size());
}

void TransactionSync::onEmptyTxs()
{
    if (m_config->txpoolStorage()->size() > 0)
    {
        return;
    }
    SYNC_LOG(DEBUG) << LOG_DESC("onEmptyTxs: broadcast txs status to all consensus node list");
    auto txsStatus =
        m_config->msgFactory()->createTxsSyncMsg(TxsSyncPacketType::TxsStatusPacket, HashList());
    auto packetData = txsStatus->encode();
    m_config->frontService()->asyncSendBroadcastMessage(
        bcos::protocol::NodeType::CONSENSUS_NODE | bcos::protocol::NodeType::OBSERVER_NODE,
        ModuleID::TxsSync, ref(*packetData));
}

void TransactionSync::stop()
{
    SYNC_LOG(INFO) << LOG_DESC("stop TransactionSync");
    if (m_worker)
    {
        m_worker->stop();
    }
    if (m_txsRequester)
    {
        m_txsRequester->stop();
    }
}