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
 * @brief block sync implementation
 * @file BlockSync.cpp
 * @author: yujiechen
 * @date 2021-05-24
 */
#include "bcos-sync/BlockSync.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-tool/LedgerConfigFetcher.h>
#include <json/json.h>
#include <boost/bind/bind.hpp>
#include <string>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::ledger;
using namespace bcos::tool;

BlockSync::BlockSync(BlockSyncConfig::Ptr _config, unsigned _idleWaitMs)
  : Worker("syncWorker", _idleWaitMs),
    m_config(_config),
    m_syncStatus(std::make_shared<SyncPeerStatus>(_config)),
    m_downloadingQueue(std::make_shared<DownloadingQueue>(_config))
{
    m_downloadBlockProcessor = std::make_shared<bcos::ThreadPool>("Download", 1);
    m_sendBlockProcessor = std::make_shared<bcos::ThreadPool>("SyncSend", 1);
    m_downloadingTimer = std::make_shared<Timer>(m_config->downloadTimeout(), "downloadTimer");

    if (m_config->enableSendBlockStatusByTree())
    {
        m_syncTreeTopology =
            std::make_shared<SyncTreeTopology>(m_config->nodeID(), m_config->syncTreeWidth());
    }

    m_downloadingTimer->registerTimeoutHandler([this] { onDownloadTimeout(); });
    m_downloadingQueue->registerNewBlockHandler(
        [this](auto&& config) { onNewBlock(std::forward<decltype(config)>(config)); });
    m_downloadingQueue->registerApplyFinishedHandler([this](bool _isNotify) {
        if (_isNotify)
        {
            m_signalled.notify_all();
        }
    });
    initSendResponseHandler();
}

void BlockSync::start()
{
    if (m_running)
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("BlockSync has already been started");
        return;
    }
    startWorking();
    m_running = true;
    BLKSYNC_LOG(INFO) << LOG_DESC("Start BlockSync");
}

void BlockSync::init()
{
    auto fetcher = std::make_shared<LedgerConfigFetcher>(m_config->ledger());
    BLKSYNC_LOG(INFO) << LOG_DESC("start fetch the ledger config for block sync module");
    fetcher->fetchBlockNumberAndHash();
    fetcher->fetchCompatibilityVersion();
    fetcher->fetchFeatures();
    fetcher->fetchConsensusNodeList();
    fetcher->fetchObserverNodeList();
    fetcher->fetchCandidateSealerList();
    fetcher->fetchGenesisHash();
    // set the syncConfig
    auto genesisHash = fetcher->genesisHash();
    BLKSYNC_LOG(INFO) << LOG_DESC("fetch the ledger config for block sync module success")
                      << LOG_KV("number", fetcher->ledgerConfig()->blockNumber())
                      << LOG_KV("latestHash", fetcher->ledgerConfig()->hash().abridged())
                      << LOG_KV("genesisHash", genesisHash);
    m_config->setGenesisHash(genesisHash);
    m_config->resetConfig(fetcher->ledgerConfig());
    if (m_config->enableSendBlockStatusByTree())
    {
        updateTreeTopologyNodeInfo();
    }
    BLKSYNC_LOG(INFO) << LOG_DESC("init block sync success");
}

void BlockSync::enableAsMaster(bool _masterNode)
{
    BLKSYNC_LOG(INFO) << LOG_DESC("enableAsMaster:") << _masterNode;
    if (m_masterNode == _masterNode)
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("enableAsMasterNode: The masterNodeState is not changed")
                          << LOG_KV("master", _masterNode);
        return;
    }
    m_config->setMasterNode(_masterNode);
    if (!_masterNode)
    {
        m_masterNode = _masterNode;
        return;
    }
    init();
    // only set m_masterNode to be true when init success
    m_masterNode = _masterNode;
}

void BlockSync::initSendResponseHandler()
{
    // set the sendResponse callback
    std::weak_ptr<bcos::front::FrontServiceInterface> weakFrontService = m_config->frontService();
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
                        BLKSYNC_LOG(TRACE) << LOG_DESC("sendResponse failed") << LOG_KV("uuid", _id)
                                           << LOG_KV("module", std::to_string(_moduleID))
                                           << LOG_KV("dst", _dstNode->shortHex())
                                           << LOG_KV("code", _error->errorCode())
                                           << LOG_KV("msg", _error->errorMessage());
                    }
                });
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(WARNING) << LOG_DESC("sendResponse exception")
                                 << LOG_KV("message", boost::diagnostic_information(e));
        }
    };
}

void BlockSync::stop()
{
    if (!m_running)
    {
        BLKSYNC_LOG(INFO) << LOG_DESC("BlockSync has already been stopped");
        return;
    }
    BLKSYNC_LOG(INFO) << LOG_DESC("Stop BlockSync");
    if (m_downloadBlockProcessor)
    {
        m_downloadBlockProcessor->stop();
    }
    if (m_sendBlockProcessor)
    {
        m_sendBlockProcessor->stop();
    }
    if (m_downloadingTimer)
    {
        m_downloadingTimer->destroy();
    }
    m_running = false;
    finishWorker();
    if (isWorking())
    {
        // stop the worker thread
        stopWorking();
        terminate();
    }
}

void BlockSync::printSyncInfo()
{
    if (c_fileLogLevel != TRACE) [[likely]]
    {
        return;
    }
    auto peers = m_syncStatus->peers();
    std::stringstream peer_str;
    for (auto const& peer : *peers)
    {
        peer_str << peer->shortHex() << "/";
    }
    BLKSYNC_LOG(TRACE) << m_config->printBlockSyncState()
                       << "\n[Sync Info] --------------------------------------------\n"
                       << "            IsSyncing:    " << isSyncing() << "\n"
                       << "            Block number: " << m_config->blockNumber() << "\n"
                       << "            Block hash:   " << m_config->hash().abridged() << "\n"
                       << "            Genesis hash: " << m_config->genesisHash().abridged() << "\n"
                       << "            Peers size:   " << peers->size() << "\n"
                       << "[Peer Info] --------------------------------------------\n"
                       << "    Host: " << m_config->nodeID()->shortHex() << "\n"
                       << "    Peer: " << peer_str.str() << "\n"
                       << "            --------------------------------------------";
}

void BlockSync::executeWorker()
{
    if (!m_masterNode)
    {
        return;
    }
    if (isSyncing())
    {
        printSyncInfo();
    }
    // maintain the connections between observers/sealers
    maintainPeersConnection();
    m_downloadBlockProcessor->enqueue([this]() {
        try
        {
            // flush downloaded buffer into downloading queue
            maintainDownloadingBuffer();
            maintainDownloadingQueue();

            // send block-download-request to peers if this node is behind others
            tryToRequestBlocks();

            if (m_config->syncArchivedBlockBody())
            {
                auto archivedBlockNumber = m_config->archiveBlockNumber();
                if (archivedBlockNumber == 0)
                {
                    return;
                }
                syncArchivedBlockBody(archivedBlockNumber);
                verifyAndCommitArchivedBlock(archivedBlockNumber);
            }
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(ERROR) << LOG_DESC(
                                      "maintainDownloadingQueue or maintainPeersStatus exception")
                               << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
    // send block to other nodes
    m_sendBlockProcessor->enqueue([this]() {
        try
        {
            maintainBlockRequest();
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(ERROR) << LOG_DESC("maintainBlockRequest exception")
                               << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void BlockSync::workerProcessLoop()
{
    while (workerState() == WorkerState::Started)
    {
        try
        {
            executeWorker();
            if (idleWaitMs() != 0U)
            {
                boost::unique_lock<boost::mutex> lock(x_signalled);
                m_signalled.wait_for(lock, boost::chrono::milliseconds(idleWaitMs()));
            }
        }
        catch (std::exception const& e)
        {
            BLKSYNC_LOG(ERROR) << LOG_DESC("BlockSync executeWorker exception")
                               << LOG_KV("message", boost::diagnostic_information(e));
        }
    }
}

bool BlockSync::shouldSyncing()
{
    if (m_config->blockNumber() >= m_config->knownHighestNumber())
    {
        return false;
    }
    // the node is reaching consensus the block
    if (m_config->committedProposalNumber() >= m_config->knownHighestNumber())
    {
        return false;
    }
    if (m_config->executedBlock() >= m_config->knownHighestNumber())
    {
        return false;
    }
    return true;
}

bool BlockSync::isSyncing() const
{
    return (m_state == SyncState::Downloading);
}

std::optional<std::tuple<bcos::protocol::BlockNumber, bcos::protocol::BlockNumber>>
BlockSync::getSyncStatus() const
{
    if (!isSyncing())
    {
        return std::nullopt;
    }
    return std::make_tuple(m_config->blockNumber(), m_config->knownHighestNumber());
}

void BlockSync::maintainDownloadingBuffer()
{
    if (m_downloadingQueue->size() == 0)
    {
        return;
    }
    if (!shouldSyncing())
    {
        m_downloadingQueue->clear();
        return;
    }
    m_downloadingQueue->clearFullQueueIfNotHas(m_config->nextBlock());
    m_downloadingQueue->flushBufferToQueue();
}


void BlockSync::asyncNotifyBlockSyncMessage(Error::Ptr _error, std::string const& _uuid,
    NodeIDPtr _nodeID, bytesConstRef _data, std::function<void(Error::Ptr _error)> _onRecv)
{
    if (!m_masterNode)
    {
        return;
    }
    auto self = weak_from_this();
    asyncNotifyBlockSyncMessage(
        _error, _nodeID, _data,
        [_uuid, _nodeID, self](bytesConstRef _respData) {
            try
            {
                auto sync = self.lock();
                if (!sync)
                {
                    return;
                }
                sync->m_sendResponseHandler(_uuid, ModuleID::BlockSync, _nodeID, _respData);
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING) << LOG_DESC("asyncNotifyBlockSyncMessage sendResponse failed")
                                     << LOG_KV("message", boost::diagnostic_information(e))
                                     << LOG_KV("id", _uuid) << LOG_KV("dst", _nodeID->shortHex());
            }
        },
        _onRecv);
}

void BlockSync::asyncNotifyBlockSyncMessage(Error::Ptr _error, NodeIDPtr _nodeID,
    bytesConstRef _data, std::function<void(bytesConstRef)>,
    std::function<void(Error::Ptr)> _onRecv)
{
    if (_onRecv)
    {
        _onRecv(nullptr);
    }
    if (_error != nullptr)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("asyncNotifyBlockSyncMessage error")
                             << LOG_KV("code", _error->errorCode())
                             << LOG_KV("msg", _error->errorMessage());
        return;
    }
    try
    {
        auto syncMsg = m_config->msgFactory()->createBlockSyncMsg(_data);
        switch (syncMsg->packetType())
        {
        case BlockSyncPacketType::BlockStatusPacket:
        {
            onPeerStatus(_nodeID, syncMsg);
            break;
        }
        case BlockSyncPacketType::BlockRequestPacket:
        {
            onPeerBlocksRequest(_nodeID, syncMsg);
            break;
        }
        case BlockSyncPacketType::BlockResponsePacket:
        {
            onPeerBlocks(_nodeID, syncMsg);
            break;
        }
        default:
        {
            BLKSYNC_LOG(WARNING) << LOG_DESC(
                                        "asyncNotifyBlockSyncMessage: unknown block sync message")
                                 << LOG_KV("type", syncMsg->packetType())
                                 << LOG_KV("peer", _nodeID->shortHex());
            break;
        }
        }
    }
    catch (std::exception const& e)
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("asyncNotifyBlockSyncMessage exception")
                             << LOG_KV("message", boost::diagnostic_information(e))
                             << LOG_KV("peer", _nodeID->shortHex());
    }
}

void BlockSync::asyncNotifyNewBlock(
    LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv)
{
    if (_onRecv)
    {
        _onRecv(nullptr);
    }
    BLKSYNC_LOG(DEBUG) << LOG_DESC("asyncNotifyNewBlock: receive new block info")
                       << LOG_KV("number", _ledgerConfig->blockNumber())
                       << LOG_KV("hash", _ledgerConfig->hash().abridged())
                       << LOG_KV("consNodeSize", _ledgerConfig->consensusNodeList().size())
                       << LOG_KV("observerNodeSize", _ledgerConfig->observerNodeList().size());
    if (_ledgerConfig->blockNumber() > m_config->blockNumber())
    {
        onNewBlock(_ledgerConfig);
        // try to commitBlock to ledger when receive new block notification
        m_downloadingQueue->tryToCommitBlockToLedger();
        if (m_config->enableSendBlockStatusByTree())
        {
            // update nodelist in tree topology
            updateTreeTopologyNodeInfo();
        }
    }
}

void BlockSync::onNewBlock(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
    m_config->resetConfig(std::move(_ledgerConfig));
    broadcastSyncStatus();
    m_downloadingQueue->clearExpiredQueueCache();
}

void BlockSync::onPeerStatus(NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg)
{
    // receive peer not exist in the group
    // Note: only should reject syncStatus from the node whose blockNumber falling behind of this
    // node
    if (!m_allowFreeNode && !m_config->existsInGroup(_nodeID) &&
        _syncMsg->number() <= m_config->blockNumber())
    {
        return;
    }
    auto statusMsg = m_config->msgFactory()->createBlockSyncStatusMsg(_syncMsg);
    m_syncStatus->updatePeerStatus(_nodeID, statusMsg);

    if (_syncMsg->version() > static_cast<int32_t>(BlockSyncMsgVersion::v0))
    {
        m_config->nodeTimeMaintenance()->tryToUpdatePeerTimeInfo(_nodeID, statusMsg->time());
    }
}

void BlockSync::onPeerBlocks(NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg)
{
    auto number = _syncMsg->number();
    auto archivedNumber = m_config->archiveBlockNumber();
    auto blockMsg = m_config->msgFactory()->createBlocksMsg(std::move(_syncMsg));
    if (number < archivedNumber)
    {
        bcos::protocol::BlockNumber topNumber = 0;
        size_t downloadedBlockCount = 0;
        {
            ReadGuard lock(x_archivedBlockQueue);
            downloadedBlockCount = m_archivedBlockQueue.size();
            if (!m_archivedBlockQueue.empty())
            {
                topNumber = m_archivedBlockQueue.top()->blockHeader()->number();
                if (topNumber == number)
                {
                    return;
                }
            }
        }
        if (downloadedBlockCount >= m_config->maxDownloadingBlockQueueSize() &&
            number != archivedNumber - 1)
        {
            BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                 << LOG_DESC("archivedBlockQueue is full")
                                 << LOG_KV("queueSize", downloadedBlockCount)
                                 << LOG_KV("receivedBlockNumber", number)
                                 << LOG_KV("topArchivedQueue", topNumber)
                                 << LOG_KV("archivedBlockNumber", archivedNumber);
            return;
        }
        auto block = m_config->blockFactory()->createBlock(blockMsg->blockData(0), true, true);
        BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << BLOCK_NUMBER(number)
                           << LOG_DESC("Receive peer block packet(archived)")
                           << LOG_KV("topArchivedQueue", topNumber)
                           << LOG_KV("archivedBlockNumber", archivedNumber)
                           << LOG_KV("peer", _nodeID->shortHex());
        {
            WriteGuard lock(x_archivedBlockQueue);
            m_archivedBlockQueue.push(block);
        }
        return;
    }
    BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download") << BLOCK_NUMBER(number) << LOG_BADGE("BlockSync")
                       << LOG_DESC("Receive peer block packet")
                       << LOG_KV("peer", _nodeID->shortHex());
    m_downloadingQueue->push(blockMsg);
    m_signalled.notify_all();
}

void BlockSync::onPeerBlocksRequest(NodeIDPtr _nodeID, BlockSyncMsgInterface::Ptr _syncMsg)
{
    auto blockRequest = m_config->msgFactory()->createBlockRequest(std::move(_syncMsg));
    BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("onPeerBlocksRequest")
                      << LOG_DESC("Receive block request") << LOG_KV("peer", _nodeID->shortHex())
                      << LOG_KV("from", blockRequest->number())
                      << LOG_KV("size", blockRequest->size())
                      << LOG_KV("flag", blockRequest->blockDataFlag())
                      << LOG_KV("interval", blockRequest->blockInterval());
    auto peerStatus = m_syncStatus->peerStatus(_nodeID);
    if (!peerStatus && (m_config->existsInGroup(_nodeID) || m_allowFreeNode))
    {
        BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("onPeerBlocksRequest")
                          << LOG_DESC(
                                 "Receive block request from the node belongs to the group but "
                                 "with no peer status, create status now")
                          << LOG_KV("peer", _nodeID ? _nodeID->shortHex() : "unknown")
                          << LOG_KV("curNum", m_config->blockNumber())
                          << LOG_KV("from", blockRequest->number())
                          << LOG_KV("size", blockRequest->size());
        // the node belongs to the group, insert the status into the peer
        peerStatus = m_syncStatus->insertEmptyPeer(_nodeID);
    }
    if (peerStatus)
    {
        peerStatus->downloadRequests()->push(blockRequest->number(), blockRequest->size(),
            blockRequest->blockInterval(), blockRequest->blockDataFlag());
        m_signalled.notify_all();
        return;
    }
    BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("onPeerBlocksRequest")
                         << LOG_DESC("Receive block request from the unknown peer, drop directly")
                         << LOG_KV("peer", _nodeID ? _nodeID->shortHex() : "unknown")
                         << LOG_KV("from", blockRequest->number())
                         << LOG_KV("size", blockRequest->size());
}

void BlockSync::onDownloadTimeout()
{
    // stop the timer and reset the state to idle
    m_downloadingTimer->stop();
    m_state = SyncState::Idle;
}

void BlockSync::downloadFinish()
{
    m_downloadingTimer->stop();
    m_state = SyncState::Idle;
}

void BlockSync::tryToRequestBlocks()
{
    // wait the downloaded block commit to the ledger, and enable the next batch requests
    if (m_config->blockNumber() < m_config->executedBlock() &&
        m_downloadingQueue->commitQueueSize() > 0)
    {
        return;
    }
    if (m_maxRequestNumber <= m_config->blockNumber() ||
        m_maxRequestNumber <= m_config->executedBlock())
    {
        downloadFinish();
    }
    if (!shouldSyncing() || isSyncing())
    {
        return;
    }
    auto requestToNumber = m_config->knownHighestNumber();
    m_config->consensus()->notifyHighestSyncingNumber(requestToNumber);
    auto topBlock = m_downloadingQueue->top(true);
    // The block in BlockQueue is not nextBlock(the BlockQueue missing some block)
    if (topBlock)
    {
        auto topBlockHeader = topBlock->blockHeader();
        if (topBlockHeader && topBlockHeader->number() >= m_config->nextBlock())
        {
            requestToNumber =
                std::min(m_config->knownHighestNumber(), (topBlockHeader->number() - 1));
        }
    }
    auto currentNumber = m_config->blockNumber();
    // no need to request blocks
    // if blockNumber in ledger >= request number
    // or request number <= executed block
    // or request number <= applying block
    if (currentNumber >= requestToNumber || requestToNumber <= m_config->executedBlock() ||
        requestToNumber <= m_config->applyingBlock())
    {
        return;
    }
    requestBlocks(
        currentNumber, requestToNumber, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
}

void BlockSync::requestBlocks(BlockNumber _from, BlockNumber _to, int32_t blockDataFlag)
{
    BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("requestBlocks")
                      << LOG_KV("from", _from) << LOG_KV("to", _to);
    m_state = SyncState::Downloading;
    m_downloadingTimer->start();

    auto blockSizePerShard = m_config->maxRequestBlocks();
    auto shardNumber = (_to - _from + blockSizePerShard - 1) / blockSizePerShard;
    size_t shard = 0;
    auto interval = m_syncStatus->peersSize() - 1;
    interval = (interval == 0) ? 1 : interval;
    // at most request `maxShardPerPeer` shards every time
    for (size_t loop = 0; loop < m_config->maxShardPerPeer() && shard < shardNumber; loop++)
    {
        bool findPeer = false;
        // shard: [from, to]
        m_syncStatus->foreachPeerRandom(
            [this, &_from, &shard, &interval, &blockSizePerShard, &_to, &findPeer, &shardNumber,
                blockDataFlag](PeerStatus::Ptr _p) {
                if (_p->number() < m_config->knownHighestNumber())
                {
                    // Only send request to nodes which are not syncing(has max number)
                    return true;
                }
                // BlockNumber from = _from + 1 + shard * blockSizePerShard;
                // BlockNumber to = std::min((BlockNumber)(from + blockSizePerShard - 1), _to);

                /// example: _from=0, interval=3, blockSizePerShard=4, _to=unlimited, then loop
                /// twice peer0: [1, 4, 7, 10], [13, 16, 19, 22] peer1: [2, 5, 8, 11], [14, 17, 20,
                /// 23] peer2: [3, 6, 9, 12], [15, 18, 21, 24]
                BlockNumber from = _from + 1 + (shard % interval) +
                                   (shard / interval) * (blockSizePerShard * interval);
                BlockNumber to =
                    std::min((BlockNumber)(from + (blockSizePerShard - 1) * interval), _to);
                BlockNumber size = (to - from) / interval + 1;
                if (_p->number() < to || _p->archivedBlockNumber() >= from)
                {
                    return true;  // to next peer
                }
                // found a peer
                findPeer = true;
                auto blockRequest = m_config->msgFactory()->createBlockRequest();
                blockRequest->setBlockDataFlag(blockDataFlag);
                if (size <= 1) [[unlikely]]
                {
                    blockRequest->setNumber(from);
                    blockRequest->setSize(to - from + 1);
                }
                else [[likely]]
                {
                    blockRequest->setNumber(from);
                    blockRequest->setSize(size);
                    blockRequest->setBlockInterval(interval);
                }
                auto encodedData = blockRequest->encode();
                m_config->frontService()->asyncSendMessageByNodeID(
                    ModuleID::BlockSync, _p->nodeId(), ref(*encodedData), 0, nullptr);

                m_maxRequestNumber = std::max(m_maxRequestNumber.load(), to);

                BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                  << LOG_DESC("Request blocks") << LOG_KV("from", from)
                                  << LOG_KV("to", to)
                                  << LOG_KV("interval", blockRequest->blockInterval())
                                  << LOG_KV("curNum", m_config->blockNumber())
                                  << LOG_KV("peerArchived", _p->archivedBlockNumber())
                                  << LOG_KV("peer", _p->nodeId()->shortHex())
                                  << LOG_KV("maxRequestNumber", m_maxRequestNumber)
                                  << LOG_KV("node", m_config->nodeID()->shortHex());

                ++shard;  // shard move
                return shard < shardNumber;
            });
        if (!findPeer)
        {
            BlockNumber from = _from + shard * blockSizePerShard;
            BlockNumber to = std::min((BlockNumber)(from + blockSizePerShard - 1), _to);
            BLKSYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                 << LOG_DESC("Couldn't find any peers to request blocks")
                                 << LOG_KV("from", from) << LOG_KV("to", to);
            break;
        }
    }
}

void BlockSync::maintainDownloadingQueue()
{
    if (!shouldSyncing())
    {
        m_downloadingQueue->clear();
        downloadFinish();
        return;
    }
    m_downloadingQueue->tryToCommitBlockToLedger();
    auto executedBlock = m_config->executedBlock();
    // remove the expired block
    auto topBlock = m_downloadingQueue->top();
    while (topBlock && topBlock->blockHeader()->number() <= executedBlock)
    {
        m_downloadingQueue->pop();
        topBlock = m_downloadingQueue->top();
    }
    topBlock = m_downloadingQueue->top();
    if (!topBlock)
    {
        return;
    }

    // limit the executed blockNumber
    if (executedBlock >= (m_config->blockNumber() + m_waterMark))
    {
        BLKSYNC_LOG(WARNING)
            << LOG_DESC("too many executed blocks have not been committed, stop execute new block")
            << LOG_KV("curNumber", m_config->blockNumber())
            << LOG_KV("executedBlock", executedBlock);
        return;
    }

    // hold header to decrease make_shared overhead
    auto topHeader = topBlock->blockHeaderConst();
    auto expectedBlock = executedBlock + 1;
    auto topNumber = topHeader->number();
    if (topNumber > (expectedBlock))
    {
        if (expectedBlock <= m_config->applyingBlock())
        {
            // expectedBlock is applying, no need to print log
            return;
        }

        if (m_downloadingQueue->commitQueueSize() > 0)
        {
            // still have block not committed, no need to print log
            return;
        }
        BLKSYNC_LOG(INFO) << LOG_DESC("Discontinuous block") << LOG_KV("topNumber", topNumber)
                          << LOG_KV("curNumber", m_config->blockNumber())
                          << LOG_KV("expectedBlock", expectedBlock)
                          << LOG_KV("commitQueueSize", m_downloadingQueue->commitQueueSize())
                          << LOG_KV("isSyncing", isSyncing())
                          << LOG_KV("knownHighestNumber", m_config->knownHighestNumber())
                          << LOG_KV("node", m_config->nodeID()->shortHex());
        return;
    }
    // execute the expected block
    if (topHeader->number() == (executedBlock + 1))
    {
        auto block = m_downloadingQueue->top();
        // Note: the block maybe cleared here
        if (!block)
        {
            return;
        }
        m_downloadingQueue->pop();
        auto blockHeader = block->blockHeader();
        auto header = block->blockHeader();
        auto signature = header->signatureList();
        BLKSYNC_LOG(INFO) << LOG_BADGE("Download") << BLOCK_NUMBER(blockHeader->number())
                          << LOG_DESC("BlockSync: applyBlock")
                          << LOG_KV("hash", blockHeader->hash().abridged())
                          << LOG_KV("node", m_config->nodeID()->shortHex())
                          << LOG_KV("signatureSize", signature.size())
                          << LOG_KV("txsSize", block->transactionsSize());
        m_downloadingQueue->applyBlock(block);
    }
}

void BlockSync::maintainBlockRequest()
{
    m_syncStatus->foreachPeerRandom([&](PeerStatus::Ptr _p) {
        auto reqQueue = _p->downloadRequests();
        // no need to respond
        if (reqQueue->empty())
        {
            return true;
        }
        while (!reqQueue->empty())
        {
            auto archivedBlockNumber = m_config->archiveBlockNumber();

            auto fetchSet = reqQueue->mergeAndPop();
            for (const auto& req : fetchSet)
            {
                if (std::cmp_less(req.number, archivedBlockNumber))
                {
                    continue;
                }
                fetchAndSendBlock(_p->nodeId(), req.number, req.dataFlag);
            }
            BLKSYNC_LOG(DEBUG) << LOG_BADGE("Download Request: response blocks")
                               << LOG_KV("size", fetchSet.size())
                               << LOG_KV("archivedNumber", archivedBlockNumber)
                               << LOG_KV("peer", _p->nodeId()->shortHex());
#if 0
            auto blocksReq = reqQueue->topAndPop();
            BlockNumber startNumber = std::max(blocksReq->fromNumber(), archivedBlockNumber);
            if (blocksReq->interval() > 0)
            {
                auto fromNumber = blocksReq->fromNumber();
                auto blockCount = blocksReq->size();
                auto interval = blocksReq->interval();
                BLKSYNC_LOG(DEBUG)
                    << LOG_BADGE("Download Request: response blocks") << LOG_KV("from", fromNumber)
                    << LOG_KV("size", blockCount) << LOG_KV("interval", interval);
                for (size_t i = 0; i < blockCount; i++)
                {
                    auto fetchNumber = fromNumber + i * interval;
                    if (std::cmp_less(fetchNumber, startNumber))
                    {
                        continue;
                    }
                    fetchAndSendBlock(_p->nodeId(), fetchNumber);
                }
            }
            else
            {
                BlockNumber numberLimit = blocksReq->fromNumber() + blocksReq->size();
                // read archived block number to check the request range
                // the number less than archived block number is not exist
                BLKSYNC_LOG(DEBUG)
                    << LOG_BADGE("Download Request: response blocks")
                    << LOG_KV("from", blocksReq->fromNumber()) << LOG_KV("size", blocksReq->size())
                    << LOG_KV("to", numberLimit - 1)
                    << LOG_KV("archivedNumber", archivedBlockNumber)
                    << LOG_KV("peer", _p->nodeId()->shortHex());
                for (BlockNumber number = startNumber; number < numberLimit; number++)
                {
                    fetchAndSendBlock(_p->nodeId(), number);
                }
            }
#endif
        }
        m_signalled.notify_all();
        return true;
    });
}

void BlockSync::fetchAndSendBlock(
    PublicPtr const& _peer, BlockNumber _number, int32_t _blockDataFlag = HEADER | TRANSACTIONS)
{
    // only fetch blockHeader and transactions
    auto self = weak_from_this();
    m_config->ledger()->asyncGetBlockDataByNumber(_number, _blockDataFlag,
        [self, _peer = std::move(_peer), _number, _blockDataFlag](
            auto&& _error, Block::Ptr _block) {
            if (_error != nullptr)
            {
                BLKSYNC_LOG(WARNING)
                    << LOG_DESC("fetchAndSendBlock failed for asyncGetBlockDataByNumber failed")
                    << LOG_KV("number", _number) << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage());
                return;
            }
            try
            {
                auto sync = self.lock();
                if (!sync)
                {
                    return;
                }
                auto blockHeader = _block->blockHeader();
                auto signature = blockHeader->signatureList();
                auto config = sync->m_config;
                auto blocksReq = config->msgFactory()->createBlocksMsg();
                bytes blockData;
                _block->encode(blockData);
                blocksReq->appendBlockData(std::move(blockData));
                blocksReq->setNumber(_number);
                config->frontService()->asyncSendMessageByNodeID(
                    ModuleID::BlockSync, _peer, ref(*(blocksReq->encode())), 0, nullptr);
                BLKSYNC_LOG(DEBUG)
                    << BLOCK_NUMBER(_number) << LOG_DESC("fetchAndSendBlock: response block")
                    << LOG_KV("toPeer", _peer->shortHex())
                    << LOG_KV("hash", blockHeader->hash().abridged())
                    << LOG_KV("signatureSize", signature.size())
                    << LOG_KV("blockFlag", _blockDataFlag)
                    << LOG_KV("transactionsSize", _block->transactionsSize());
            }
            catch (std::exception const& e)
            {
                BLKSYNC_LOG(WARNING)
                    << LOG_DESC("fetchAndSendBlock exception") << LOG_KV("number", _number)
                    << LOG_KV("message", boost::diagnostic_information(e));
            }
        });
}

void BlockSync::maintainPeersConnection()
{
    if (!m_allowFreeNode && !m_config->existsInGroup())
    {
        return;
    }
    // Delete uncorrelated peers
    NodeIDs peersToDelete;
    m_syncStatus->foreachPeer([&](PeerStatus::Ptr _p) {
        if (_p->nodeId() == m_config->nodeID())
        {
            return true;
        }
        if (!m_config->connected(_p->nodeId()))
        {
            peersToDelete.emplace_back(_p->nodeId());
            return true;
        }
        if (!m_allowFreeNode && !m_config->existsInGroup(_p->nodeId()) &&
            m_config->blockNumber() >= _p->number())
        {
            // Only delete outsider whose number is smaller than myself
            peersToDelete.emplace_back(_p->nodeId());
        }
        return true;
    });
    // delete the invalid peer
    for (const auto& node : peersToDelete)
    {
        m_syncStatus->deletePeer(node);
    }
    if (m_config->enableSendBlockStatusByTree())
    {
        // send sync status by tree
        sendSyncStatusByTree();
    }
    else
    {
        // broad sync status
        broadcastSyncStatus();
    }
}

void BlockSync::sendSyncStatusByTree()
{
    auto statusMsg = m_config->msgFactory()->createBlockSyncStatusMsg(m_config->blockNumber(),
        m_config->hash(), m_config->genesisHash(), static_cast<int32_t>(BlockSyncMsgVersion::v2),
        m_config->archiveBlockNumber());
    m_syncStatus->updatePeerStatus(m_config->nodeID(), statusMsg);
    auto encodedData = statusMsg->encode();
    BLKSYNC_LOG(TRACE) << LOG_BADGE("BlockSync") << LOG_DESC("broadcastSyncStatusByTree")
                       << LOG_KV("number", statusMsg->number())
                       << LOG_KV("genesisHash", statusMsg->genesisHash().abridged())
                       << LOG_KV("currentHash", statusMsg->hash().abridged());
    // Note: only send status to the observers/sealers, but the OUTSIDE_GROUP node maybe
    // observer/sealer before sync to the highest here can't use asyncSendBroadcastMessage=
    // Note: connectedNodeSet() cannot be used directly here, because connectedNodeSet()
    // contains light nodes, but the nodes in groupNodeList() are not necessarily connected to this
    // node, so take the intersection of the two.
    auto const& groupNodeList =
        m_syncTreeTopology->selectNodesForBlockSync(m_config->connectedGroupNodeList());
    for (auto const& nodeID : *groupNodeList)
    {
        m_config->frontService()->asyncSendMessageByNodeID(
            ModuleID::BlockSync, nodeID, ref(*encodedData), 0, nullptr);
    }
}

void BlockSync::broadcastSyncStatus()
{
    auto statusMsg = m_config->msgFactory()->createBlockSyncStatusMsg(m_config->blockNumber(),
        m_config->hash(), m_config->genesisHash(), static_cast<int32_t>(BlockSyncMsgVersion::v2),
        m_config->archiveBlockNumber());
    m_syncStatus->updatePeerStatus(m_config->nodeID(), statusMsg);
    auto encodedData = statusMsg->encode();
    BLKSYNC_LOG(TRACE) << LOG_BADGE("BlockSync") << LOG_DESC("broadcastSyncStatus")
                       << LOG_KV("number", statusMsg->number())
                       << LOG_KV("genesisHash", statusMsg->genesisHash().abridged())
                       << LOG_KV("currentHash", statusMsg->hash().abridged());
    // Note: only send status to the observers/sealers, but the OUTSIDE_GROUP node maybe
    // observer/sealer before sync to the highest here can't use asyncSendBroadcastMessage

    // Broadcast to all nodes if turn on allow_free_nodes_sync
    if (m_allowFreeNode)
    {
        m_config->frontService()->asyncSendBroadcastMessage(
            LIGHT_NODE | CONSENSUS_NODE | OBSERVER_NODE | FREE_NODE, ModuleID::BlockSync,
            bcos::ref(*encodedData));
    }
    else
    {
        auto const& groupNodeList = m_config->groupNodeList();
        for (auto const& nodeID : groupNodeList)
        {
            m_config->frontService()->asyncSendMessageByNodeID(
                ModuleID::BlockSync, nodeID, ref(*encodedData), 0, nullptr);
        }
    }
}

void BlockSync::updateTreeTopologyNodeInfo()
{
    bcos::crypto::NodeIDs consensusNodeIDs;
    bcos::crypto::NodeIDs allNodeIDs;

    // extract NodeIDs
    RANGES::for_each(m_config->consensusNodeList(), [&consensusNodeIDs, &allNodeIDs](auto& node) {
        consensusNodeIDs.emplace_back(node->nodeID());
        allNodeIDs.emplace_back(node->nodeID());
    });
    RANGES::for_each(m_config->observerNodeList(),
        [&allNodeIDs](auto& node) { allNodeIDs.emplace_back(node->nodeID()); });

    RANGES::sort(consensusNodeIDs.begin(), consensusNodeIDs.end(),
        [](auto& a, auto& b) { return a->data() < b->data(); });
    RANGES::sort(allNodeIDs.begin(), allNodeIDs.end(),
        [](auto& a, auto& b) { return a->data() < b->data(); });

    m_syncTreeTopology->updateAllNodeInfo(consensusNodeIDs, allNodeIDs);
}

bool BlockSync::faultyNode(bcos::crypto::NodeIDPtr _nodeID)
{
    // if the node is down, it has no peer information
    auto nodeStatus = m_syncStatus->peerStatus(_nodeID);
    if (nodeStatus == nullptr)
    {
        return true;
    }
    return (nodeStatus->number() + c_FaultyNodeBlockDelta) < m_config->blockNumber();
}

void BlockSync::asyncGetSyncInfo(std::function<void(Error::Ptr, std::string)> _onGetSyncInfo)
{
    Json::Value syncInfo;
    syncInfo["isSyncing"] = isSyncing();
    syncInfo["genesisHash"] = *toHexString(m_config->genesisHash());
    syncInfo["nodeID"] = *toHexString(m_config->nodeID()->data());

    int64_t currentNumber = m_config->blockNumber();
    syncInfo["blockNumber"] = currentNumber;
    syncInfo["archivedBlockNumber"] = m_config->archiveBlockNumber();
    syncInfo["latestHash"] = *toHexString(m_config->hash());
    syncInfo["knownHighestNumber"] = m_config->knownHighestNumber();
    syncInfo["knownLatestHash"] = *toHexString(m_config->knownLatestHash());

    Json::Value peersInfo(Json::arrayValue);
    m_syncStatus->foreachPeer([&](PeerStatus::Ptr _p) {
        // not print the status of the node-self
        if (_p->nodeId() == m_config->nodeID())
        {
            return true;
        }
        Json::Value info;
        info["nodeID"] = *toHexString(_p->nodeId()->data());
        info["genesisHash"] = *toHexString(_p->genesisHash());
        info["blockNumber"] = Json::UInt64(_p->number());
        info["latestHash"] = *toHexString(_p->hash());
        info["archivedBlockNumber"] = Json::UInt64(_p->archivedBlockNumber());
        peersInfo.append(info);
        return true;
    });

    syncInfo["peers"] = peersInfo;
    Json::FastWriter fastWriter;
    std::string statusStr = fastWriter.write(syncInfo);
    _onGetSyncInfo(nullptr, statusStr);
}

std::vector<PeerStatus::Ptr> BlockSync::getPeerStatus()
{
    std::vector<PeerStatus::Ptr> statuses{};
    statuses.reserve(m_syncStatus->peersSize());
    m_syncStatus->foreachPeer([&statuses](auto&& status) {
        statuses.emplace_back(status);
        return true;
    });
    return statuses;
}

void BlockSync::syncArchivedBlockBody(bcos::protocol::BlockNumber archivedBlockNumber)
{
    size_t millseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - m_lastArchivedRequestTime)
                             .count();
    if (millseconds < m_config->downloadTimeout())
    {
        return;
    }
    // use 1/4 of the maxDownloadingBlockQueueSize to limit the memory usage
    auto maxCount = (BlockNumber)(m_config->maxDownloadingBlockQueueSize() / 4);
    BlockNumber topBlockNumber =
        std::max(archivedBlockNumber - (BlockNumber)maxCount, (BlockNumber)0);
    {
        ReadGuard lock(x_archivedBlockQueue);
        if (!m_archivedBlockQueue.empty())
        {
            topBlockNumber = m_archivedBlockQueue.top()->blockHeader()->number();
            if (topBlockNumber >= archivedBlockNumber - 1)
            {
                return;
            }
        }
    }
    if (topBlockNumber == archivedBlockNumber - 1)
    {
        return;
    }
    auto from = topBlockNumber;
    if (archivedBlockNumber - from > maxCount)
    {
        from = archivedBlockNumber - (BlockNumber)maxCount;
    }
    BLKSYNC_LOG(INFO) << LOG_DESC("BlockSync: syncArchivedBlockBody")
                      << LOG_KV("archivedBlockNumber", archivedBlockNumber)
                      << LOG_KV("topBlockNumber", topBlockNumber) << LOG_KV("from", from)
                      << LOG_KV("to", archivedBlockNumber - 1);
    m_lastArchivedRequestTime = std::chrono::system_clock::now();
    requestBlocks(from, archivedBlockNumber - 1, bcos::ledger::FULL_BLOCK);
}

void BlockSync::verifyAndCommitArchivedBlock(bcos::protocol::BlockNumber archivedBlockNumber)
{
    BlockNumber topBlockNumber = 0;
    {
        ReadGuard lock(x_archivedBlockQueue);
        if (!m_archivedBlockQueue.empty())
        {
            topBlockNumber = m_archivedBlockQueue.top()->blockHeader()->number();
        }
        else
        {
            return;
        }
    }
    if (topBlockNumber != archivedBlockNumber - 1)
    {
        return;
    }
    // verify the tx root and receipt root
    bcos::protocol::Block::Ptr block = nullptr;
    {
        WriteGuard lock(x_archivedBlockQueue);
        block = m_archivedBlockQueue.top();
        // m_archivedBlockQueue.pop();
    }

    std::promise<protocol::BlockHeader::Ptr> blockHeaderFuture;
    m_config->ledger()->asyncGetBlockDataByNumber(topBlockNumber, bcos::ledger::HEADER,
        [&blockHeaderFuture](const Error::Ptr& error, const Block::Ptr& block) {
            if (error)
            {
                blockHeaderFuture.set_value(nullptr);
            }
            else
            {
                blockHeaderFuture.set_value(block->blockHeader());
            }
        });
    auto localBlockHeader = blockHeaderFuture.get_future().get();
    if (!localBlockHeader)
    {
        BLKSYNC_LOG(ERROR) << LOG_DESC("BlockSync get local block header failed")
                           << LOG_KV("number", topBlockNumber)
                           << LOG_KV("reason", "get local block header failed");
        return;
    }

    auto transactionRoot =
        block->calculateTransactionRoot(*m_config->blockFactory()->cryptoSuite()->hashImpl());
    auto receiptRoot =
        block->calculateReceiptRoot(*m_config->blockFactory()->cryptoSuite()->hashImpl());
    if (transactionRoot != localBlockHeader->txsRoot() ||
        receiptRoot != localBlockHeader->receiptsRoot())
    {
        BLKSYNC_LOG(ERROR) << LOG_DESC("BlockSync verify archived block failed")
                           << LOG_KV("number", topBlockNumber)
                           << LOG_KV("transactionRoot", *toHexString(transactionRoot))
                           << LOG_KV(
                                  "localTransactionRoot", *toHexString(localBlockHeader->txsRoot()))
                           << LOG_KV("receiptRoot", *toHexString(receiptRoot))
                           << LOG_KV("localReceiptRoot",
                                  *toHexString(localBlockHeader->receiptsRoot()))
                           << LOG_KV("reason", "transactionRoot or receiptRoot not match");
        WriteGuard lock(x_archivedBlockQueue);
        m_archivedBlockQueue.pop();
        return;
    }
    auto err = m_config->ledger()->storeTransactionsAndReceipts(nullptr, block);
    if (err)
    {
        BLKSYNC_LOG(ERROR) << LOG_DESC("BlockSync commit archived block failed")
                           << LOG_KV("number", topBlockNumber)
                           << LOG_KV("reason", "storeTransactionsAndReceipts failed");
        return;
    }
    BLKSYNC_LOG(INFO) << LOG_DESC("BlockSync commit archived block success")
                      << LOG_KV("number", topBlockNumber);
    // update the archived number
    m_config->ledger()->setCurrentStateByKey(bcos::ledger::SYS_KEY_ARCHIVED_NUMBER,
        bcos::storage::Entry(std::to_string(topBlockNumber)));
    {
        WriteGuard lock(x_archivedBlockQueue);
        for (auto topNumber = m_archivedBlockQueue.top()->blockHeader()->number();
             topNumber >= topBlockNumber;)
        {
            m_archivedBlockQueue.pop();
            if (!m_archivedBlockQueue.empty())
            {
                topNumber = m_archivedBlockQueue.top()->blockHeader()->number();
            }
            else
            {
                break;
            }
        }
    }
}
