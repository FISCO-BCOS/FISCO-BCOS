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
    SYNCLOG(TRACE) << "            Genesis hash: " << m_blockChain->numberHash(0) << endl;
    SYNCLOG(TRACE) << "            TxPool size:  " << m_txPool->pendingSize() << endl;
    SYNCLOG(TRACE) << "            Peers size:   " << m_syncStatus->peers().size() << endl;
    SYNCLOG(TRACE) << "[Peer Info] --------------------------------------------" << endl;
    SYNCLOG(TRACE) << "    Host: " << m_nodeId << endl;

    NodeIDs peers = m_syncStatus->peers();
    for (auto& peer : peers)
        SYNCLOG(TRACE) << "    Peer: " << peer << endl;
    SYNCLOG(TRACE) << "            --------------------------------------------" << endl;
}

void SyncMaster::start()
{
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
        if (m_newBlocks)
        {
            m_newBlocks = false;
            maintainBlocks();
        }
    }

    // Not Idle do
    if (isSyncing())
    {
        if (m_state == SyncState::Downloading)
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

SyncStatus SyncMaster::status() const
{
    ReadGuard l(x_sync);
    SyncStatus res;
    res.state = m_state;
    res.protocolId = m_protocolId;
    res.startBlockNumber = m_startingBlock;
    res.currentBlockNumber = m_blockChain->number();
    res.highestBlockNumber = m_highestBlock;
    return res;
}

void SyncMaster::noteSealingBlockNumber(int64_t _number)
{
    WriteGuard l(x_currentSealingNumber);
    m_currentSealingNumber = _number;
}

bool SyncMaster::isSyncing() const
{
    return m_state != SyncState::Idle;
}

void SyncMaster::maintainTransactions()
{
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;

    auto ts =
        m_txPool->topTransactionsCondition(c_maxSendTransactions, [&](Transaction const& _tx) {
            bool unsent = !m_txPool->isTransactionKonwnBy(_tx.sha3(), m_nodeId);
            return unsent;
        });

    SYNCLOG(TRACE) << "[Send] [Tx] Transaction " << ts.size() << " of " << m_txPool->pendingSize()
                   << " need to broadcast" << endl;

    for (size_t i = 0; i < ts.size(); ++i)
    {
        auto const& t = ts[i];
        NodeIDs peers;
        unsigned _percent = m_txPool->isTransactionKonwnBySomeone(t.sha3()) ? 25 : 100;

        peers = m_syncStatus->randomSelection(_percent, [&](std::shared_ptr<SyncPeerStatus> _p) {
            bool unsent = !m_txPool->isTransactionKonwnBy(t.sha3(), m_nodeId);
            return unsent && !m_txPool->isTransactionKonwnBy(t.sha3(), _p->nodeId);
        });

        for (auto const& p : peers)
        {
            peerTransactions[p].push_back(i);
            m_txPool->transactionIsKonwnBy(t.sha3(), p);
        }

        if (0 != peers.size())
            m_txPool->transactionIsKonwnBy(t.sha3(), m_nodeId);
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
        m_service->asyncSendMessageByNodeID(_p->nodeId, msg);
        SYNCLOG(TRACE) << "[Send] [Tx] Transaction send [txNum/toNodeId/messageSize]: "
                       << int(txsSize) << "/" << _p->nodeId << "/" << msg->buffer()->size() << "B"
                       << endl;

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

        m_service->asyncSendMessageByNodeID(_p->nodeId, packet.toMessage(m_protocolId));
        SYNCLOG(TRACE) << "[Send] [Status] Status [number/genesisHash/currentHash] :" << int(number)
                       << "/" << m_genesisHash << "/" << currentHash << " to " << _p->nodeId
                       << endl;

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
            SYNCLOG(TRACE) << "[Idle] [Download] No need to download when mining or no need "
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
        SYNCLOG(TRACE) << "[Idle] [Download] No need to download when not timeout "
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
        SYNCLOG(TRACE)
            << "[Idle] [Download] no need to sync with blocks are in queue, currentNumber:"
            << currentNumber << " maxRequestNumber:" << maxRequestNumber << endl;
        return;  // no need to send request block packet
    }

    // Sharding by c_maxRequestBlocks to request blocks
    size_t shardNumber =
        (maxRequestNumber - currentNumber + c_maxRequestBlocks - 1) / c_maxRequestBlocks;
    size_t shard = 0;
    while (shard < shardNumber)
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
            m_service->asyncSendMessageByNodeID(_p->nodeId, packet.toMessage(m_protocolId));
            SYNCLOG(TRACE) << "[Send] [Download] Request blocks [from, to] : [" << from << ", "
                           << to << "] to " << _p->nodeId << endl;

            ++shard;  // shard move

            return shard < shardNumber;
        });

        if (!thisTurnFound)
        {
            int64_t from = currentNumber + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxRequestNumber);

            SYNCLOG(ERROR) << "[Send] [Download] Couldn't find any peers to request blocks ["
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
        if (isNewBlock(topBlock))
        {
            ExecutiveContext::Ptr exeCtx = m_blockVerifier->executeBlock(*topBlock);
            m_blockChain->commitBlock(*topBlock, exeCtx);
            m_txPool->dropBlockTrans(*topBlock);
            SYNCLOG(TRACE) << "[Rcv] [Download] Block commit [number/txs/hash]: "
                           << topBlock->header().number() << "/" << topBlock->transactions().size()
                           << "/" << topBlock->headerHash();
        }
        else
            SYNCLOG(WARNING) << "[Rcv] [Download] Ignore illegal block [thisNumber/currentNumber]: "
                             << topBlock->header().number() << "/" << m_blockChain->number();

        bq.pop();
        topBlock = bq.top();
    }

    // has download finished ?
    currentNumber = m_blockChain->number();
    if (currentNumber >= m_syncStatus->knownHighestNumber)
    {
        h256 const& latestHash =
            m_blockChain->getBlockByNumber(m_syncStatus->knownHighestNumber)->headerHash();
        SYNCLOG(TRACE) << "[Rcv] [Download] Finish. Latest hash: " << latestHash
                       << " Expected hash: " << m_syncStatus->knownLatestHash;
        assert(m_syncStatus->knownLatestHash == latestHash);
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
    SessionInfos sessions = m_service->sessionInfosByProtocolID(m_protocolId);
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

            m_service->asyncSendMessageByNodeID(session.nodeID, packet.toMessage(m_protocolId));
            SYNCLOG(TRACE)
                << "[Send] [Status] Status to new peer [number/genesisHash/currentHash] :"
                << int(currentNumber) << "/" << m_genesisHash << "/" << currentHash << " to "
                << session.nodeID << endl;
        }
    }
}

void SyncMaster::maintainDownloadingQueueBuffer()
{
    if (m_state == SyncState::Downloading)
    {
        m_syncStatus->bq().clearFullQueueIfNotHas(m_blockChain->number() + 1);
        m_syncStatus->bq().flushBufferToQueue();
    }
    else
        m_syncStatus->bq().clear();
}

bool SyncMaster::isNewBlock(BlockPtr _block)
{
    if (_block == nullptr)
        return false;

    int64_t currentNumber = m_blockChain->number();
    if (currentNumber + 1 != _block->header().number())
        return false;
    if (m_blockChain->numberHash(currentNumber) != _block->header().parentHash())
        return false;

    // TODO check block minerlist sig
    return true;
}
