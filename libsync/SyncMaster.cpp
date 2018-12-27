/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : Sync implementation
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#include "SyncMaster.h"
#include <libblockchain/BlockChainInterface.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;
using namespace dev::blockverifier;

void SyncMaster::printSyncInfo()
{
    SYNCLOG(TRACE) << "[Sync Info] --------------------------------------------" << endl;
    SYNCLOG(TRACE) << "            IsSyncing:    " << isSyncing() << endl;
    SYNCLOG(TRACE) << "            Block number: " << m_blockChain->number() << endl;
    SYNCLOG(TRACE) << "            Block hash:   "
                   << m_blockChain->numberHash(m_blockChain->number()) << endl;
    SYNCLOG(TRACE) << "            Genesis hash: " << m_syncStatus->genesisHash << endl;
    auto pendingSize = m_txPool->pendingSize();
    SYNCLOG(TRACE) << "            TxPool size:  " << pendingSize << endl;
    SYNCLOG(TRACE) << "            Peers size:   " << m_syncStatus->peers().size() << endl;
    SYNCLOG(TRACE) << "[Peer Info] --------------------------------------------" << endl;
    SYNCLOG(TRACE) << "    Host: " << m_nodeId.abridged() << endl;

    NodeIDs peers = m_syncStatus->peers();
    for (auto& peer : peers)
        SYNCLOG(TRACE) << "    Peer: " << peer.abridged() << endl;
    SYNCLOG(TRACE) << "            --------------------------------------------" << endl;
}

SyncStatus SyncMaster::status() const
{
    ReadGuard l(x_sync);
    SyncStatus res;
    res.state = m_syncStatus->state;
    res.protocolId = m_protocolId;
    res.currentBlockNumber = m_blockChain->number();
    res.knownHighestNumber = m_syncStatus->knownHighestNumber;
    res.knownLatestHash = m_syncStatus->knownLatestHash;
    return res;
}

string const SyncMaster::syncInfo() const
{
    json_spirit::Object syncInfo;
    syncInfo.push_back(json_spirit::Pair("isSyncing", isSyncing()));
    syncInfo.push_back(json_spirit::Pair("protocolId", m_protocolId));
    syncInfo.push_back(json_spirit::Pair("genesisHash", toHex(m_syncStatus->genesisHash)));
    syncInfo.push_back(json_spirit::Pair("nodeId", toHex(m_nodeId)));

    int64_t currentNumber = m_blockChain->number();
    syncInfo.push_back(json_spirit::Pair("blockNumber", currentNumber));
    syncInfo.push_back(
        json_spirit::Pair("latestHash", toHex(m_blockChain->numberHash(currentNumber))));
    syncInfo.push_back(json_spirit::Pair("knownHighestNumber", m_syncStatus->knownHighestNumber));
    syncInfo.push_back(json_spirit::Pair("knownLatestHash", toHex(m_syncStatus->knownLatestHash)));
    syncInfo.push_back(json_spirit::Pair("txPoolSize", std::to_string(m_txPool->pendingSize())));

    json_spirit::Array peersInfo;
    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        json_spirit::Object info;
        info.push_back(json_spirit::Pair("nodeId", toHex(_p->nodeId)));
        info.push_back(json_spirit::Pair("genesisHash", toHex(_p->genesisHash)));
        info.push_back(json_spirit::Pair("blockNumber", _p->number));
        info.push_back(json_spirit::Pair("latestHash", toHex(_p->latestHash)));
        peersInfo.push_back(info);
        return true;
    });

    syncInfo.push_back(json_spirit::Pair("peers", peersInfo));

    json_spirit::Value value(syncInfo);
    std::string statusStr = json_spirit::write_string(value, true);
    return statusStr;
}

void SyncMaster::start()
{
    if (!fp_isConsensusOk)
    {
        SYNCLOG(ERROR) << "Consensus verify handler is not set" << endl;
        BOOST_THROW_EXCEPTION(SyncVerifyHandlerNotSet());
    }

    startWorking();
}

void SyncMaster::stop()
{
    doneWorking();
    stopWorking();
}

void SyncMaster::doWork()
{
    // Debug print
    if (isSyncing())
        printSyncInfo();

    // Always do
    maintainPeersConnection();
    maintainDownloadingQueueBuffer();
    maintainPeersStatus();

    // Idle do
    if (!isSyncing())
    {
        // cout << "SyncMaster " << m_protocolId << " doWork()" << endl;
        if (m_newTransactions)
        {
            m_newTransactions = false;
            maintainTransactions();
        }

        if (m_newBlocks || utcTime() > m_maintainBlocksTimeout)
        {
            m_newBlocks = false;
            maintainBlocks();
            m_maintainBlocksTimeout = utcTime() + c_maintainBlocksTimeout;
        }

        maintainBlockRequest();
    }

    // Not Idle do
    if (isSyncing())
    {
        if (m_syncStatus->state == SyncState::Downloading)
        {
            bool finished = maintainDownloadingQueue();
            if (finished)
                noteDownloadingFinish();
        }
    }
}

void SyncMaster::workLoop()
{
    while (workerState() == WorkerState::Started)
    {
        doWork();
        if (idleWaitMs())
        {
            std::unique_lock<std::mutex> l(x_signalled);
            m_signalled.wait_for(l, std::chrono::milliseconds(idleWaitMs()));
        }
    }
}


void SyncMaster::noteSealingBlockNumber(int64_t _number)
{
    WriteGuard l(x_currentSealingNumber);
    m_currentSealingNumber = _number;
}

bool SyncMaster::isSyncing() const
{
    return m_syncStatus->state != SyncState::Idle;
}

void SyncMaster::maintainTransactions()
{
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;

    auto ts =
        m_txPool->topTransactionsCondition(c_maxSendTransactions, [&](Transaction const& _tx) {
            bool unsent = !m_txPool->isTransactionKnownBy(_tx.sha3(), m_nodeId);
            return unsent;
        });

    auto txSize = ts.size();
    auto pendingSize = m_txPool->pendingSize();
    SYNCLOG(TRACE) << "[Tx] Transaction " << txSize << " of " << pendingSize << " need to broadcast"
                   << endl;

    for (size_t i = 0; i < ts.size(); ++i)
    {
        auto const& t = ts[i];
        NodeIDs peers;
        unsigned _percent = m_txPool->isTransactionKnownBySomeone(t.sha3()) ? 25 : 100;

        peers = m_syncStatus->randomSelection(_percent, [&](std::shared_ptr<SyncPeerStatus> _p) {
            bool unsent = !m_txPool->isTransactionKnownBy(t.sha3(), m_nodeId);
            return unsent && !m_txPool->isTransactionKnownBy(t.sha3(), _p->nodeId);
        });

        for (auto const& p : peers)
        {
            peerTransactions[p].push_back(i);
            m_txPool->transactionIsKnownBy(t.sha3(), p);
        }

        if (0 != peers.size())
            m_txPool->transactionIsKnownBy(t.sha3(), m_nodeId);
    }


    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        bytes txRLPs;
        unsigned txsSize = peerTransactions[_p->nodeId].size();
        if (0 == txsSize)
            return true;  // No need to send

        for (auto const& i : peerTransactions[_p->nodeId])
            txRLPs += ts[i].rlp();

        SyncTransactionsPacket packet;
        packet.encode(txsSize, txRLPs);

        auto msg = packet.toMessage(m_protocolId);
        m_service->asyncSendMessageByNodeID(_p->nodeId, msg, CallbackFuncWithSession(), Options());
        SYNCLOG(DEBUG) << "[Tx] Send transaction to peer [txNum/toNodeId/messageSize]: "
                       << int(txsSize) << "/" << _p->nodeId.abridged() << "/"
                       << msg->buffer()->size() << "B" << endl;

        return true;
    });
}

void SyncMaster::maintainBlocks()
{
    int64_t number = m_blockChain->number();
    h256 const& currentHash = m_blockChain->numberHash(number);

    // Just broadcast status
    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        SyncStatusPacket packet;
        packet.encode(number, m_genesisHash, currentHash);

        m_service->asyncSendMessageByNodeID(
            _p->nodeId, packet.toMessage(m_protocolId), CallbackFuncWithSession(), Options());
        SYNCLOG(DEBUG) << "[Status] Send current status when maintainBlocks "
                          "[number/genesisHash/currentHash/peer] :"
                       << int(number) << "/" << m_genesisHash << "/" << currentHash << "/"
                       << _p->nodeId.abridged() << endl;

        return true;
    });
}

void SyncMaster::maintainPeersStatus()
{
    // need download? ->set syncing and knownHighestNumber
    int64_t currentNumber = m_blockChain->number();
    int64_t maxPeerNumber = 0;
    h256 latestHash;
    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        if (_p->number > maxPeerNumber)
        {
            latestHash = _p->latestHash;
            maxPeerNumber = _p->number;
        }
        return true;
    });

    // update my known
    if (maxPeerNumber > currentNumber)
    {
        WriteGuard l(m_syncStatus->x_known);
        m_syncStatus->knownHighestNumber = maxPeerNumber;
        m_syncStatus->knownLatestHash = latestHash;
    }

    // Not to start download when mining or no need
    {
        ReadGuard l(x_currentSealingNumber);
        if (maxPeerNumber <= m_currentSealingNumber)
        {
            // mining : maxPeerNumber - currentNumber == 1
            // no need: maxPeerNumber - currentNumber <= 0
            SYNCLOG(TRACE) << "[Download] No need to download when mining or no need "
                              "[currentNumber/currentSealingNumber/maxPeerNumber]: "
                           << currentNumber << "/" << m_currentSealingNumber << "/" << maxPeerNumber
                           << endl;
            return;  // no need to sync
        }
    }

    // Skip downloading if last if not timeout
    uint64_t currentTime = utcTime();
    if (currentTime - m_lastDownloadingRequestTime < c_downloadingRequestTimeout)
    {
        SYNCLOG(TRACE) << "[Download] No need to download when not timeout "
                          "[currentNumber/maxPeerNumber]: "
                       << currentNumber << "/" << maxPeerNumber << endl;
        return;  // no need to sync
    }
    m_lastDownloadingRequestTime = currentTime;

    // Start download
    noteDownloadingBegin();

    // Choose to use min number in blockqueue or max peer number
    int64_t maxRequestNumber = maxPeerNumber;
    BlockPtr topBlock = m_syncStatus->bq().top();
    if (nullptr != topBlock)
    {
        int64_t minNumberInQueue = topBlock->header().number();
        maxRequestNumber = min(maxPeerNumber, minNumberInQueue - 1);
    }
    if (currentNumber >= maxRequestNumber)
    {
        SYNCLOG(TRACE) << "[Download] No need to sync when blocks are already in queue "
                          "[currentNumber/maxRequestNumber]: "
                       << currentNumber << "/" << maxRequestNumber << endl;
        return;  // no need to send request block packet
    }

    // Sharding by c_maxRequestBlocks to request blocks
    size_t shardNumber =
        (maxRequestNumber - currentNumber + c_maxRequestBlocks - 1) / c_maxRequestBlocks;
    size_t shard = 0;
    while (shard < shardNumber && shard < c_maxRequestShards)
    {
        bool thisTurnFound = false;
        m_syncStatus->foreachPeerRandom([&](std::shared_ptr<SyncPeerStatus> _p) {
            // shard: [from, to]
            int64_t from = currentNumber + 1 + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxRequestNumber);
            if (_p->number < to)
                return true;  // exit, to next peer

            // found a peer
            thisTurnFound = true;
            SyncReqBlockPacket packet;
            unsigned size = to - from + 1;
            packet.encode(from, size);
            m_service->asyncSendMessageByNodeID(
                _p->nodeId, packet.toMessage(m_protocolId), CallbackFuncWithSession(), Options());
            SYNCLOG(DEBUG) << "[Download] [Request] Request blocks [from, to] : [" << from << ", "
                           << to << "] to " << _p->nodeId << endl;

            ++shard;  // shard move

            return shard < shardNumber && shard < c_maxRequestShards;
        });

        if (!thisTurnFound)
        {
            int64_t from = currentNumber + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxRequestNumber);

            SYNCLOG(ERROR) << "[Download] [Request] Couldn't find any peers to request blocks ["
                           << from << ", " << to << "]" << endl;
            break;
        }
    }
}

bool SyncMaster::maintainDownloadingQueue()
{
    int64_t currentNumber = m_blockChain->number();
    if (currentNumber >= m_syncStatus->knownHighestNumber)
        return true;

    DownloadingBlockQueue& bq = m_syncStatus->bq();

    // pop block in sequence and ignore block which number is lower than currentNumber +1
    BlockPtr topBlock = bq.top();
    while (topBlock != nullptr && topBlock->header().number() <= (m_blockChain->number() + 1))
    {
        try
        {
            if (isNewBlock(topBlock))
            {
                auto parentBlock =
                    m_blockChain->getBlockByNumber(topBlock->blockHeader().number() - 1);
                BlockInfo parentBlockInfo{parentBlock->header().hash(),
                    parentBlock->header().number(), parentBlock->header().stateRoot()};
                ExecutiveContext::Ptr exeCtx =
                    m_blockVerifier->executeBlock(*topBlock, parentBlockInfo);
                CommitResult ret = m_blockChain->commitBlock(*topBlock, exeCtx);
                if (ret == CommitResult::OK)
                {
                    m_txPool->dropBlockTrans(*topBlock);
                    SYNCLOG(DEBUG)
                        << "[Download] [BlockSync] Download block commit [number/txs/hash]: "
                        << topBlock->header().number() << "/" << topBlock->transactions().size()
                        << "/" << topBlock->headerHash() << endl;
                }
                else
                {
                    m_txPool->handleBadBlock(*topBlock);
                    SYNCLOG(TRACE)
                        << "[Download] [BlockSync] Block commit failed [number/txs/hash]: "
                        << topBlock->header().number() << "/" << topBlock->transactions().size()
                        << "/" << topBlock->headerHash() << endl;
                }
            }
            else
            {
                SYNCLOG(TRACE) << "[Download] [BlockSync] Block of queue top is not new block "
                                  "[number/txs/hash]: "
                               << topBlock->header().number() << "/"
                               << topBlock->transactions().size() << "/" << topBlock->headerHash()
                               << endl;
            }
        }
        catch (exception& e)
        {
            SYNCLOG(ERROR) << "[Download] [BlockSync] Block of queue top is not a valid block "
                              "[number/txs/hash]: "
                           << topBlock->header().number() << "/" << topBlock->transactions().size()
                           << "/" << topBlock->headerHash() << endl;
        }

        bq.pop();
        topBlock = bq.top();
    }

    // has download finished ?
    currentNumber = m_blockChain->number();
    if (currentNumber >= m_syncStatus->knownHighestNumber)
    {
        h256 const& latestHash =
            m_blockChain->getBlockByNumber(m_syncStatus->knownHighestNumber)->headerHash();
        SYNCLOG(TRACE) << "[Download] [BlockSync] Download finish. Latest hash: " << latestHash
                       << " Expected hash: " << m_syncStatus->knownLatestHash;

        if (m_syncStatus->knownLatestHash != latestHash)
            SYNCLOG(FATAL) << "[Download] State error: This node's version is not compatable with "
                              "others! All data should be cleared of this node before restart."
                           << endl;
        return true;
    }
    return false;
}

void SyncMaster::maintainPeersConnection()
{
    // Delete inactive peers
    NodeIDs nodeIds = m_syncStatus->peers();
    for (NodeID const& id : nodeIds)
    {
        if (!m_service->isConnected(id))
            m_syncStatus->deletePeer(id);
    }

    // Add new peers
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    int64_t currentNumber = m_blockChain->number();
    h256 const& currentHash = m_blockChain->numberHash(currentNumber);
    for (auto const& session : sessions)
    {
        if (!m_syncStatus->hasPeer(session.nodeID))
        {
            // create a peer
            SyncPeerInfo newPeer{session.nodeID, 0, m_genesisHash, m_genesisHash};
            m_syncStatus->newSyncPeerStatus(newPeer);

            // send my status to her
            SyncStatusPacket packet;
            packet.encode(currentNumber, m_genesisHash, currentHash);

            m_service->asyncSendMessageByNodeID(session.nodeID, packet.toMessage(m_protocolId),
                CallbackFuncWithSession(), Options());
            SYNCLOG(DEBUG) << "[Status] Send current status to new peer "
                              "[number/genesisHash/currentHash/peer] :"
                           << int(currentNumber) << "/" << m_genesisHash << "/" << currentHash
                           << "/" << session.nodeID.abridged() << endl;
        }
    }
}

void SyncMaster::maintainDownloadingQueueBuffer()
{
    if (m_syncStatus->state == SyncState::Downloading)
    {
        m_syncStatus->bq().clearFullQueueIfNotHas(m_blockChain->number() + 1);
        m_syncStatus->bq().flushBufferToQueue();
    }
    else
        m_syncStatus->bq().clear();
}

void SyncMaster::maintainBlockRequest()
{
    uint64_t timeout = utcTime() + c_respondDownloadRequestTimeout;
    m_syncStatus->foreachPeerRandom([&](std::shared_ptr<SyncPeerStatus> _p) {
        DownloadRequestQueue& reqQueue = _p->reqQueue;
        if (reqQueue.empty())
            return true;  // no need to respeond

        // Just select one peer per maintain
        reqQueue.disablePush();  // drop push at this time
        DownloadBlocksContainer blockContainer(m_service, m_protocolId, _p->nodeId);

        while (!reqQueue.empty() && utcTime() <= timeout)
        {
            DownloadRequest req = reqQueue.topAndPop();
            int64_t number = req.fromNumber;
            int64_t numberLimit = req.fromNumber + req.size;

            // Send block at sequence
            for (; number < numberLimit && utcTime() <= timeout; number++)
            {
                shared_ptr<Block> block = m_blockChain->getBlockByNumber(number);
                if (!block)
                {
                    SYNCLOG(TRACE)
                        << "[Download] [Request] Get block for node failed "
                           "[reason/number/nodeId]: "
                        << "block is null/" << number << "/" << _p->nodeId.abridged() << endl;
                    break;
                }
                else if (block->header().number() != number)
                {
                    SYNCLOG(TRACE)
                        << "[Download] [Request] Get block for node failed "
                           "[reason/number/nodeId]: "
                        << number << "number incorrect /" << _p->nodeId.abridged() << endl;
                    break;
                }

                blockContainer.batchAndSend(block);
            }

            if (req.fromNumber < number)
                SYNCLOG(DEBUG) << "[Download] [Request] [BlockSync] Send blocks [" << req.fromNumber
                               << ", " << number - 1 << "] to peer " << _p->nodeId.abridged()
                               << endl;

            if (number < numberLimit)  // This respond not reach the end due to timeout
            {
                // write back the rest request range
                reqQueue.enablePush();
                SYNCLOG(DEBUG) << "[Download] [Request] Push unsent requests back to reqQueue ["
                               << number << ", " << numberLimit - 1 << "] of "
                               << _p->nodeId.abridged() << endl;
                reqQueue.push(number, numberLimit - number);
                return false;
            }
        }

        reqQueue.enablePush();
        return false;
    });
}

bool SyncMaster::isNewBlock(BlockPtr _block)
{
    if (_block == nullptr)
        return false;

    int64_t currentNumber = m_blockChain->number();
    if (currentNumber + 1 != _block->header().number())
    {
        SYNCLOG(WARNING) << "[Download] [BlockSync] Ignore illegal block "
                            "[reason/thisNumber/currentNumber]: number illegal/"
                         << _block->header().number() << "/" << currentNumber << endl;
        return false;
    }

    if (m_blockChain->numberHash(currentNumber) != _block->header().parentHash())
    {
        SYNCLOG(WARNING)
            << "[Download] [BlockSync] Ignore illegal block "
               "[reason/thisNumber/currentNumber/thisParentHash/currentHash]: parent hash illegal/"
            << _block->header().number() << "/" << currentNumber << "/"
            << _block->header().parentHash() << "/" << m_blockChain->numberHash(currentNumber)
            << endl;
        return false;
    }

    // check block minerlist sig
    if (fp_isConsensusOk && !(fp_isConsensusOk)(*_block))
    {
        SYNCLOG(WARNING) << "[Download] [BlockSync] Ignore illegal block "
                            "[reason/thisNumber/currentNumber/thisParentHash/currentHash]: "
                            "consensus check failed/"
                         << _block->header().number() << "/" << currentNumber << "/"
                         << _block->header().parentHash() << "/"
                         << m_blockChain->numberHash(currentNumber) << endl;
        return false;
    }

    return true;
}
