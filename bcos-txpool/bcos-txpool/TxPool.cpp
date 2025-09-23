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
 * @brief implementation for txpool
 * @file TxPool.cpp
 * @author: yujiechen
 * @date 2021-05-10
 */
#include "TxPool.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include "txpool/validator/LedgerNonceChecker.h"
#include "txpool/validator/TxValidator.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-utilities/ITTAPI.h>
#include <oneapi/tbb/parallel_for.h>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::sync;
using namespace bcos::consensus;
using namespace bcos::tool;

bcos::txpool::TxPool::TxPool(TxPoolConfig::Ptr config, TxPoolStorageInterface::Ptr txpoolStorage,
    bcos::sync::TransactionSyncInterface::Ptr transactionSync, size_t verifierWorkerNum)
  : m_config(std::move(config)),
    m_txpoolStorage(std::move(txpoolStorage)),
    m_transactionSync(std::move(transactionSync)),
    m_transactionFactory(m_config->blockFactory()->transactionFactory()),
    m_ledger(m_config->ledger())
{
    m_verifier = std::make_shared<ThreadPool>("verifier", 2);
    // worker to pre-store-txs
    m_txsPreStore = std::make_shared<ThreadPool>("txsPreStore", 1);
    TXPOOL_LOG(INFO) << LOG_DESC("create TxPool") << LOG_KV("submitterNum", verifierWorkerNum);
}

bcos::txpool::TxPool::~TxPool() noexcept
{
    stop();
}

void TxPool::start()
{
    if (m_running)
    {
        TXPOOL_LOG(INFO) << LOG_DESC("The txpool has already been started!");
        return;
    }

    m_txpoolStorage->start();
    m_running = true;
    TXPOOL_LOG(INFO) << LOG_DESC("Start the txpool.");
}

void TxPool::stop()
{
    if (!m_running)
    {
        TXPOOL_LOG(INFO) << LOG_DESC("The txpool has already been stopped!");
        return;
    }
    if (m_txsPreStore)
    {
        m_txsPreStore->stop();
    }
    if (m_verifier)
    {
        m_verifier->stop();
    }
    if (m_txpoolStorage)
    {
        m_txpoolStorage->stop();
    }

    if (m_transactionSync)
    {
        m_transactionSync->stop();
    }

    m_running = false;
    TXPOOL_LOG(INFO) << LOG_DESC("Stop the txpool.");
}

task::Task<protocol::TransactionSubmitResult::Ptr> TxPool::submitTransaction(
    protocol::Transaction::Ptr transaction)
{
    co_return co_await m_txpoolStorage->submitTransaction(std::move(transaction));
}

task::Task<protocol::TransactionSubmitResult::Ptr> TxPool::submitTransactionWithoutReceipt(
    protocol::Transaction::Ptr transaction)
{
    co_return co_await m_txpoolStorage->submitTransactionWithoutReceipt(std::move(transaction));
}

task::Task<protocol::TransactionSubmitResult::Ptr> TxPool::submitTransactionWithHook(
    protocol::Transaction::Ptr transaction, std::function<void()> onTxSubmitted)
{
    co_return co_await m_txpoolStorage->submitTransactionWithHook(
        std::move(transaction), std::move(onTxSubmitted));
}

task::Task<void> TxPool::broadcastTransaction(const protocol::Transaction& transaction)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().BROADCAST_TX);
    bcos::bytes buffer;
    transaction.encode(buffer);
    co_await broadcastTransactionBuffer(bcos::ref(buffer));
}

task::Task<void> TxPool::broadcastTransactionBuffer(bytesConstRef data)
{
    if (m_treeRouter != nullptr) [[unlikely]]
    {
        co_await broadcastTransactionBufferByTree(data, true);
    }
    else [[likely]]
    {
        // co_await m_transactionSync->config()->frontService()->broadcastMessage(
        //     protocol::NodeType::CONSENSUS_NODE, protocol::SYNC_PUSH_TRANSACTION,
        //     ::ranges::views::single(data));
        m_transactionSync->config()->frontService()->asyncSendBroadcastMessage(
            protocol::NodeType::CONSENSUS_NODE, protocol::SYNC_PUSH_TRANSACTION, data);
    }
}

task::Task<void> TxPool::broadcastTransactionBufferByTree(
    bytesConstRef _data, bool isStartNode, bcos::crypto::NodeIDPtr fromNode)
{
    if (m_treeRouter != nullptr)
    {
        auto protocolList =
            m_transactionSync->config()->frontService()->groupNodeInfo()->nodeProtocolList();
        // NOTE: the protocolList is a vector which sorted by nodeID, and is NOT convenience
        // for filter whether send new protocol or not. So one-size-fits-all approach, if
        // protocolList have lower V2 version, broadcast SYNC_PUSH_TRANSACTION by default.
        auto index =
            std::find_if(protocolList.begin(), protocolList.end(), [](auto const& protocol) {
                return protocol->version() < protocol::ProtocolVersion::V2;
            });
        if (index != protocolList.end()) [[unlikely]]
        {
            TXPOOL_LOG(TRACE) << LOG_DESC(
                                     "broadcastTransactionBufferByTree but have lower version node")
                              << LOG_KV("index", index->get()->version());
            // have lower V2 version, broadcast SYNC_PUSH_TRANSACTION by default
            co_await m_transactionSync->config()->frontService()->broadcastMessage(
                protocol::NodeType::CONSENSUS_NODE, protocol::SYNC_PUSH_TRANSACTION,
                ::ranges::views::single(_data));
        }
        else [[likely]]
        {
            auto selectedNode =
                m_treeRouter->selectNodes(m_transactionSync->config()->connectedGroupNodeList(),
                    m_treeRouter->consIndex(), isStartNode, fromNode);
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                std::stringstream selectedNodeList;
                std::for_each(selectedNode->begin(), selectedNode->end(),
                    [&](const bcos::crypto::NodeIDPtr& item) {
                        selectedNodeList << (item ? item->shortHex() : "") << ",";
                    });
                TXPOOL_LOG(TRACE) << LOG_DESC("broadcastTransactionBufferByTree")
                                  << LOG_KV("selectSize", selectedNode->size())
                                  << LOG_KV("selectedNode", selectedNodeList.str());
            }
            for (const auto& node : (*selectedNode))
            {
                m_transactionSync->config()->frontService()->asyncSendMessageByNodeID(
                    protocol::TREE_PUSH_TRANSACTION, node, _data, 0, front::CallbackFunc());
            }
        }
    }
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> TxPool::getTransactions(
    RANGES::any_view<bcos::h256, RANGES::category::mask | RANGES::category::sized> hashes)
{
    co_return m_txpoolStorage->getTransactions(std::move(hashes));
}

bool TxPool::checkExistsInGroup(TxSubmitCallback _txSubmitCallback)
{
    auto syncConfig = m_transactionSync->config();
    if (!_txSubmitCallback || syncConfig->existsInGroup())
    {
        return true;
    }
    auto txResult = m_config->txResultFactory()->createTxSubmitResult();
    txResult->setTxHash(HashType());
    txResult->setStatus((uint32_t)TransactionStatus::RequestNotBelongToTheGroup);

    auto errorMsg = "Do not send transactions to nodes that are not in the group";
    _txSubmitCallback(BCOS_ERROR_PTR((int32_t)txResult->status(), errorMsg), txResult);
    TXPOOL_LOG(WARNING) << LOG_DESC(errorMsg);
    return false;
}

void TxPool::asyncSealTxs(uint64_t _txsLimit, TxsHashSetPtr _avoidTxs,
    std::function<void(Error::Ptr, Block::Ptr, Block::Ptr)> _sealCallback)
{
    auto fetchedTxs = m_config->blockFactory()->createBlock();
    auto sysTxs = m_config->blockFactory()->createBlock();
    {
        bcos::WriteGuard guard(x_markTxsMutex);
        m_txpoolStorage->batchFetchTxs(fetchedTxs, sysTxs, _txsLimit, _avoidTxs, true);
    }
    _sealCallback(nullptr, fetchedTxs, sysTxs);
}

void TxPool::asyncNotifyBlockResult(BlockNumber _blockNumber, TransactionSubmitResultsPtr txsResult,
    std::function<void(Error::Ptr)> _onNotifyFinished)
{
    m_txpoolStorage->batchRemove(_blockNumber, *txsResult);

    if (_onNotifyFinished)
    {
        _onNotifyFinished(nullptr);
    }
}

void TxPool::asyncVerifyBlock(PublicPtr _generatedNodeID, bytesConstRef const& _block,
    std::function<void(Error::Ptr, bool)> _onVerifyFinished)
{
    auto block = m_config->blockFactory()->createBlock(_block);
    auto blockHeader = block->blockHeader();
    TXPOOL_LOG(INFO) << LOG_DESC("begin asyncVerifyBlock")
                     << LOG_KV("consNum", blockHeader ? blockHeader->number() : -1)
                     << LOG_KV("hash", blockHeader ? blockHeader->hash().abridged() : "null");
    // Note: here must have thread pool for lock in the callback
    // use single thread here to decrease thread competition
    auto self = weak_from_this();
    m_verifier->enqueue([self, _generatedNodeID, blockHeader, block, _onVerifyFinished]() {
        try
        {
            auto startT = utcTime();
            auto txpool = self.lock();
            if (!txpool)
            {
                if (_onVerifyFinished)
                {
                    _onVerifyFinished(
                        BCOS_ERROR_PTR(-1, "asyncVerifyBlock failed for lock txpool failed"),
                        false);
                }
                return;
            }
            auto txpoolStorage = txpool->m_txpoolStorage;
            auto missedTxs = txpoolStorage->batchVerifyProposal(block);
            if (!missedTxs)
            {
                _onVerifyFinished(BCOS_ERROR_PTR(CommonError::VerifyProposalFailed,
                                      "asyncVerifyBlock failed for duplicate transaction"),
                    false);
                return;
            }
            auto onVerifyFinishedWrapper =
                [txpool, txpoolStorage, _onVerifyFinished, block, blockHeader, missedTxs, startT](
                    const Error::Ptr& _error, bool _ret) {
                    auto verifyRet = _ret;
                    auto verifyError = _error;
                    if (!missedTxs->empty())
                    {
                        // try to fetch the missed txs from the local  txpool again
                        if (_error && _error->errorCode() == CommonError::TransactionsMissing)
                        {
                            verifyRet = txpoolStorage->batchVerifyProposal(missedTxs);
                        }
                        if (verifyRet)
                        {
                            verifyError = nullptr;
                        }
                    }
                    TXPOOL_LOG(INFO)
                        << METRIC << LOG_DESC("asyncVerifyBlock finished")
                        << LOG_KV("consNum", blockHeader ? blockHeader->number() : -1)
                        << LOG_KV("hash", blockHeader ? blockHeader->hash().abridged() : "null")
                        << LOG_KV("code", verifyError ? verifyError->errorCode() : 0)
                        << LOG_KV("msg", verifyError ? verifyError->errorMessage() : "success")
                        << LOG_KV("result", verifyRet) << LOG_KV("timecost", (utcTime() - startT));
                    if (!_onVerifyFinished)
                    {
                        return;
                    }
                    _onVerifyFinished(verifyError, verifyRet);
                    // batchPreStore the proposal txs when verifySuccess in the case of not enable
                    // txsPreStore
                    // Note: here storeVerifiedBlock will block m_verifier and decrease the
                    // proposal-verify-perf, so we async the storeVerifiedBlock here using
                    // m_txsPreStore
                    if (!verifyError && verifyRet && block && block->blockHeader())
                    {
                        txpool->m_txsPreStore->enqueue(
                            [txpool, block]() { txpool->storeVerifiedBlock(block); });
                    }
                };

            if (missedTxs->empty())
            {
                TXPOOL_LOG(DEBUG) << LOG_DESC("asyncVerifyBlock: hit all transactions in txpool")
                                  << LOG_KV("consNum", blockHeader ? blockHeader->number() : -1)
                                  << LOG_KV("nodeId",
                                         txpool->m_transactionSync->config()->nodeID()->shortHex());
                onVerifyFinishedWrapper(nullptr, true);
                return;
            }
            TXPOOL_LOG(DEBUG) << LOG_DESC("asyncVerifyBlock")
                              << LOG_KV("consNum", blockHeader ? blockHeader->number() : -1)
                              << LOG_KV("totalTxs", block->transactionsHashSize())
                              << LOG_KV("missedTxs", missedTxs->size());
            txpool->m_transactionSync->requestMissedTxs(
                _generatedNodeID, missedTxs, block, onVerifyFinishedWrapper);
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("asyncVerifyBlock exception")
                                << LOG_KV("fromNodeId", _generatedNodeID->shortHex())
                                << LOG_KV("consNum", blockHeader ? blockHeader->number() : -1)
                                << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void TxPool::asyncNotifyTxsSyncMessage(Error::Ptr _error, std::string const& _uuid,
    NodeIDPtr _nodeID, bytesConstRef _data, std::function<void(Error::Ptr)> _onRecv)
{
    auto self = weak_from_this();
    m_transactionSync->onRecvSyncMessage(
        _error, _nodeID, _data, [self, _uuid, _nodeID](bytesConstRef _respData) {
            try
            {
                auto txpool = self.lock();
                if (!txpool)
                {
                    return;
                }
                txpool->m_sendResponseHandler(
                    _uuid, bcos::protocol::ModuleID::TxsSync, _nodeID, _respData);
            }
            catch (std::exception const& e)
            {
                TXPOOL_LOG(TRACE) << LOG_DESC("asyncNotifyTxsSyncMessage: sendResponse failed")
                                  << LOG_KV("msg", boost::diagnostic_information(e))
                                  << LOG_KV("uuid", _uuid) << LOG_KV("dst", _nodeID->shortHex());
            }
        });
    if (!_onRecv)
    {
        return;
    }
    _onRecv(nullptr);
}

void TxPool::notifyConsensusNodeList(
    ConsensusNodeList const& _consensusNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setConsensusNodeList(_consensusNodeList);
    if (m_treeRouter)
    {
        auto nodeIds = crypto::NodeIDs();
        nodeIds.reserve(_consensusNodeList.size());
        for (const auto& node : _consensusNodeList)
        {
            nodeIds.push_back(node.nodeID);
        }
        m_treeRouter->updateConsensusNodeInfo(nodeIds);
    }
    if (!_onRecvResponse)
    {
        return;
    }
    _onRecvResponse(nullptr);
}

void TxPool::notifyObserverNodeList(
    ConsensusNodeList const& _observerNodeList, std::function<void(Error::Ptr)> _onRecvResponse)
{
    m_transactionSync->config()->setObserverList(_observerNodeList);
    if (!_onRecvResponse)
    {
        return;
    }
    _onRecvResponse(nullptr);
}

void TxPool::getTxsFromLocalLedger(HashListPtr _txsHash, HashListPtr _missedTxs,
    std::function<void(Error::Ptr, ConstTransactionsPtr)> _onBlockFilled)
{
    // fetch from the local ledger
    m_transactionSync->requestMissedTxs(nullptr, std::move(_missedTxs), nullptr,
        [this, _txsHash = std::move(_txsHash), _onBlockFilled = std::move(_onBlockFilled)](
            const Error::Ptr& _error, bool _verifyResult) {
            if (_error || !_verifyResult)
            {
                TXPOOL_LOG(WARNING) << LOG_DESC("getTxsFromLocalLedger failed")
                                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                                    << LOG_KV("msg", _error ? _error->errorMessage() : "fetchSucc")
                                    << LOG_KV("verifyResult", _verifyResult);
                _onBlockFilled(
                    BCOS_ERROR_PTR(CommonError::TransactionsMissing, "TransactionsMissing"),
                    nullptr);
                return;
            }
            TXPOOL_LOG(INFO) << LOG_DESC(
                "asyncFillBlock miss and try to get the transaction from the ledger success");
            fillBlock(_txsHash, _onBlockFilled, false);
        });
}

// Note: the transaction must be all hit in local txpool
void TxPool::asyncFillBlock(
    HashListPtr _txsHash, std::function<void(Error::Ptr, ConstTransactionsPtr)> _onBlockFilled)
{
    fillBlock(std::move(_txsHash), std::move(_onBlockFilled), true);
}

void TxPool::fillBlock(HashListPtr _txsHash,
    std::function<void(Error::Ptr, ConstTransactionsPtr)> _onBlockFilled, bool _fetchFromLedger)
{
    ittapi::Report report(
        ittapi::ITT_DOMAINS::instance().TXPOOL, ittapi::ITT_DOMAINS::instance().FILL_BLOCK);

    HashListPtr missedTxs = std::make_shared<HashList>();
    auto txs = m_txpoolStorage->fetchTxs(*missedTxs, *_txsHash);
    if (!missedTxs->empty())
    {
        TXPOOL_LOG(WARNING) << LOG_DESC("asyncFillBlock failed for missing some transactions")
                            << LOG_KV("missedTxsSize", missedTxs->size());
        if (_fetchFromLedger)
        {
            TXPOOL_LOG(INFO) << LOG_DESC("getTxsFromLocalLedger")
                             << LOG_KV("txsSize", _txsHash->size())
                             << LOG_KV("missedSize", missedTxs->size());
            getTxsFromLocalLedger(_txsHash, missedTxs, _onBlockFilled);
        }
        else
        {
            _onBlockFilled(
                BCOS_ERROR_PTR(CommonError::TransactionsMissing, "TransactionsMissing"), nullptr);
        }
        return;
    }
    report.release();

    _onBlockFilled(nullptr, txs);
}


void TxPool::asyncMarkTxs(const HashList& _txsHash, bool _sealedFlag,
    bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
    std::function<void(Error::Ptr)> _onRecvResponse)
{
    bool allMarked = false;
    {
        bcos::ReadGuard guard(x_markTxsMutex);
        allMarked = m_txpoolStorage->batchMarkTxs(_txsHash, _batchId, _batchHash, _sealedFlag);
    }

    if (!_onRecvResponse)
    {
        return;
    }
    if (allMarked)
    {
        _onRecvResponse(nullptr);
    }
    else
    {
        _onRecvResponse(BCOS_ERROR_PTR(
            CommonError::TransactionsMissing, "TransactionsMissing during asyncMarkTxs"));
    }
}

void TxPool::asyncResetTxPool(std::function<void(Error::Ptr)> _onRecvResponse)
{
    // mark all the transactions as unsealed
    m_txpoolStorage->batchMarkAllTxs(false);
    if (!_onRecvResponse)
    {
        return;
    }
    TXPOOL_LOG(INFO) << LOG_DESC("asyncResetTxPool") << LOG_KV("txsSize", m_txpoolStorage->size());
    _onRecvResponse(nullptr);
}

void TxPool::init()
{
    initSendResponseHandler();
    TXPOOL_LOG(INFO) << LOG_DESC("fetch LedgerConfig information");
    auto ledgerConfig = task::syncWait(ledger::getLedgerConfig(*m_config->ledger()));
    TXPOOL_LOG(INFO) << LOG_DESC("fetch LedgerConfig success");

    auto blockLimit = m_config->blockLimit();
    auto startNumber =
        (ledgerConfig->blockNumber() > blockLimit ? (ledgerConfig->blockNumber() - blockLimit + 1) :
                                                    0);

    std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>> nonceList;
    if (startNumber >= 0)
    {
        auto toNumber = ledgerConfig->blockNumber();
        auto fetchedSize = std::min(blockLimit, (toNumber - startNumber + 1));
        TXPOOL_LOG(INFO) << LOG_DESC("fetch history nonces information")
                         << LOG_KV("startNumber", startNumber)
                         << LOG_KV("fetchedSize", fetchedSize);
        nonceList =
            task::syncWait(ledger::getNonceList(*m_config->ledger(), startNumber, fetchedSize));
    }
    TXPOOL_LOG(INFO) << LOG_DESC("fetch history nonces success");

    // create LedgerNonceChecker and set it into the validator
    TXPOOL_LOG(INFO) << LOG_DESC("init txs validator")
                     << LOG_KV("checkBlockLimit", m_checkBlockLimit);
    auto ledgerNonceChecker = std::make_shared<LedgerNonceChecker>(
        nonceList, ledgerConfig->blockNumber(), blockLimit, m_checkBlockLimit);

    auto validator = std::dynamic_pointer_cast<TxValidator>(m_config->txValidator());
    validator->setLedgerNonceChecker(ledgerNonceChecker);
    TXPOOL_LOG(INFO) << LOG_DESC("init txs validator success");

    // init syncConfig
    TXPOOL_LOG(INFO) << LOG_DESC("init sync config");
    auto txsSyncConfig = m_transactionSync->config();
    txsSyncConfig->setConsensusNodeList(ledgerConfig->consensusNodeList());
    txsSyncConfig->setObserverList(ledgerConfig->observerNodeList());
    m_transactionSync->config()->setMaxResponseTxsToNodesWithEmptyTxs(
        ledgerConfig->blockTxCountLimit());
    TXPOOL_LOG(INFO) << LOG_DESC("init sync config success");
}

void TxPool::initSendResponseHandler()
{
    // set the sendResponse callback
    std::weak_ptr<bcos::front::FrontServiceInterface> weakFrontService =
        m_transactionSync->config()->frontService();
    m_sendResponseHandler = [weakFrontService](std::string const& _id, int _moduleID,
                                NodeIDPtr _dstNode, bytesConstRef _data) {
        try
        {
            auto frontService = weakFrontService.lock();
            if (!frontService)
            {
                return;
            }
            frontService->asyncSendResponse(
                _id, _moduleID, _dstNode, _data, [_id, _moduleID, _dstNode](Error::Ptr _error) {
                    if (_error)
                    {
                        TXPOOL_LOG(TRACE) << LOG_DESC("sendResponse failed") << LOG_KV("uuid", _id)
                                          << LOG_KV("module", std::to_string(_moduleID))
                                          << LOG_KV("dst", _dstNode->shortHex())
                                          << LOG_KV("code", _error->errorCode())
                                          << LOG_KV("msg", _error->errorMessage());
                    }
                });
        }
        catch (std::exception const& e)
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("sendResponse exception")
                                << LOG_KV("message", boost::diagnostic_information(e))
                                << LOG_KV("uuid", _id) << LOG_KV("moduleID", _moduleID)
                                << LOG_KV("peer", _dstNode->shortHex());
        }
    };
}


void TxPool::storeVerifiedBlock(bcos::protocol::Block::Ptr _block)
{
    auto blockHeader = _block->blockHeader();

    // return if block has been committed
    TXPOOL_LOG(INFO) << LOG_DESC("storeVerifiedBlock fetch block number from LedgerConfig");
    auto blockNumber = task::syncWait(ledger::getCurrentBlockNumber(*m_config->ledger()));
    auto committedBlockNumber = blockNumber;
    if (blockHeader->number() <= committedBlockNumber)
    {
        TXPOOL_LOG(INFO) << LOG_DESC("storeVerifiedBlock, block already committed")
                         << LOG_KV("consNum", blockHeader->number())
                         << LOG_KV("hash", blockHeader->hash().abridged())
                         << LOG_KV("committedNum", committedBlockNumber);
        return;
    }

    TXPOOL_LOG(INFO) << LOG_DESC("storeVerifiedBlock") << LOG_KV("consNum", blockHeader->number())
                     << LOG_KV("hash", blockHeader->hash().abridged())
                     << LOG_KV("txsSize", _block->transactionsHashSize());
    auto txsHashList = std::make_shared<HashList>();
    for (size_t i = 0; i < _block->transactionsHashSize(); i++)
    {
        txsHashList->emplace_back(_block->transactionHash(i));
    }

    auto self = weak_from_this();
    auto startT = utcTime();
    asyncFillBlock(txsHashList,
        [self, startT, blockHeader, _block](Error::Ptr _error, ConstTransactionsPtr _txs) {
            if (_error)
            {
                TXPOOL_LOG(WARNING)
                    << LOG_DESC("storeVerifiedBlock, fillBlock failed")
                    << LOG_KV("consNum", blockHeader->number())
                    << LOG_KV("hash", blockHeader->hash().abridged())
                    << LOG_KV("msg", _error->errorMessage()) << LOG_KV("code", _error->errorCode());
                return;
            }
            auto txpool = self.lock();
            if (!txpool)
            {
                return;
            }
            txpool->m_config->ledger()->asyncPreStoreBlockTxs(
                std::move(_txs), _block, [startT, blockHeader](Error::UniquePtr&& _error) {
                    if (_error)
                    {
                        TXPOOL_LOG(WARNING)
                            << LOG_DESC("storeVerifiedBlock: asyncPreStoreBlockTxs failed")
                            << LOG_KV("consNum", blockHeader->number())
                            << LOG_KV("hash", blockHeader->hash().abridged())
                            << LOG_KV("msg", _error->errorMessage())
                            << LOG_KV("code", _error->errorCode());
                        return;
                    }
                    TXPOOL_LOG(INFO) << LOG_DESC("storeVerifiedBlock success")
                                     << LOG_KV("consNum", blockHeader->number())
                                     << LOG_KV("hash", blockHeader->hash().abridged())
                                     << LOG_KV("timecost", (utcTime() - startT));
                });
        });
}
void bcos::txpool::TxPool::notifyConnectedNodes(
    bcos::crypto::NodeIDSet const& _connectedNodes, std::function<void(Error::Ptr)> _onResponse)
{
    m_transactionSync->config()->notifyConnectedNodes(_connectedNodes, _onResponse);
    if (m_txpoolStorage->size() > 0)
    {
        return;
    }
    // try to broadcast empty txsStatus and request txs from the connected nodes when the txpool
    // is empty
    m_transactionSync->onEmptyTxs();
}
void bcos::txpool::TxPool::registerTxsCleanUpSwitch(std::function<bool()> _txsCleanUpSwitch)
{
    m_txpoolStorage->registerTxsCleanUpSwitch(_txsCleanUpSwitch);
}
void bcos::txpool::TxPool::tryToSyncTxsFromPeers()
{
    m_transactionSync->onEmptyTxs();
}

task::Task<std::optional<u256>> bcos::txpool::TxPool::getWeb3PendingNonce(std::string_view address)
{
    co_return co_await m_config->txValidator()->web3NonceChecker()->getPendingNonce(address);
}

void bcos::txpool::TxPool::asyncGetPendingTransactionSize(
    std::function<void(Error::Ptr, uint64_t)> _onGetTxsSize)
{
    if (!_onGetTxsSize)
    {
        return;
    }
    auto pendingTxsSize = m_txpoolStorage->size();
    _onGetTxsSize(nullptr, pendingTxsSize);
}

void bcos::txpool::TxPool::setTransactionSync(
    bcos::sync::TransactionSyncInterface::Ptr _transactionSync)
{
    m_transactionSync = std::move(_transactionSync);
}
bcos::txpool::TxPoolStorageInterface::Ptr bcos::txpool::TxPool::txpoolStorage()
{
    return m_txpoolStorage;
}
bcos::txpool::TxPoolConfig::Ptr bcos::txpool::TxPool::txpoolConfig()
{
    return m_config;
}
void bcos::txpool::TxPool::setTxPoolStorage(TxPoolStorageInterface::Ptr _txpoolStorage)
{
    m_txpoolStorage = _txpoolStorage;
    m_transactionSync->config()->setTxPoolStorage(_txpoolStorage);
}
void bcos::txpool::TxPool::clearAllTxs()
{
    m_txpoolStorage->clear();
}
bcos::sync::TransactionSyncInterface::Ptr& bcos::txpool::TxPool::transactionSync()
{
    return m_transactionSync;
}
