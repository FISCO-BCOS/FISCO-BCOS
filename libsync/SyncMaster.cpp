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

void SyncMaster::start()
{
    startWorking();
}

void SyncMaster::stop()
{
    stopWorking();
}

void SyncMaster::doWork()
{
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

    // Always do
    maintainPeersConnection();
    maintainPeersStatus();
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

bool SyncMaster::isSyncing() const
{
    return m_state != SyncState::Idle;
}

void SyncMaster::maintainTransactions()
{
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;

    auto ts = m_txPool->topTransactionsCondition(c_maxSendTransactions,
        [&](Transaction const& _tx) { return !m_txPool->isTransactionKonwnBySomeone(_tx.sha3()); });

    LOG(DEBUG) << ts.size() << " transactions need to broadcast";
    {
        for (size_t i = 0; i < ts.size(); ++i)
        {
            auto const& t = ts[i];
            NodeIDs peers;
            unsigned _percent = m_txPool->isTransactionKonwnBySomeone(t.sha3()) ? 25 : 100;

            peers =
                m_syncStatus->randomSelection(_percent, [&](std::shared_ptr<SyncPeerStatus> _p) {
                    return !m_txPool->isTransactionKonwnBy(t.sha3(), _p->nodeId);
                });

            for (auto const& p : peers)
            {
                peerTransactions[p].push_back(i);
                m_txPool->transactionIsKonwnBy(t.sha3(), p);
            }
        }
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

        m_service->asyncSendMessageByNodeID(_p->nodeId, packet.toMessage(m_protocolId));
        LOG(TRACE) << "Send" << int(txsSize) << "transactions to " << _p->nodeId;

        return true;
    });

    LOG(DEBUG) << " maintain transaction finish";
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
        LOG(TRACE) << "Send status (" << int(number) << "," << m_genesisHash << "," << currentHash
                   << ") to " << _p->nodeId;

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

    if (currentNumber >= maxPeerNumber)
        return;  // no need to sync
    else
    {
        WriteGuard l(m_syncStatus->x_known);
        m_syncStatus->knownHighestNumber = maxPeerNumber;
        m_syncStatus->knownLatestHash = latestHash;
        noteDownloadingBegin();
    }

    // Sharding by c_maxRequestBlocks to request blocks
    size_t shardNumber = (maxPeerNumber + 1 - currentNumber) / c_maxRequestBlocks;
    size_t shard = 0;
    while (shard < shardNumber)
    {
        bool thisTurnFound = false;
        m_syncStatus->foreachPeerRandom([&](std::shared_ptr<SyncPeerStatus> _p) {
            // shard: [from, to]
            int64_t from = currentNumber + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxPeerNumber);

            if (_p->number < to)
                return true;  // exit, to next peer

            // found a peer
            thisTurnFound = true;
            SyncReqBlockPacket packet;
            unsigned size = to - from + 1;
            packet.encode(from, size);
            m_service->asyncSendMessageByNodeID(_p->nodeId, packet.toMessage(m_protocolId));
            LOG(TRACE) << "Request blocks [" << from << ", " << to << "] to " << _p->nodeId;

            ++shard;  // shard move

            return shard < shardNumber;
        });

        if (!thisTurnFound)
        {
            int64_t from = currentNumber + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxPeerNumber);

            LOG(ERROR) << "Couldn't find any peers to request blocks [" << from << ", " << to
                       << "]";
            break;
        }
    }
}


bool SyncMaster::maintainDownloadingQueue()
{
    int64_t currentNumber = m_blockChain->number();
    if (currentNumber < m_syncStatus->knownHighestNumber)
    {
        BlockPtrVec blocks = m_syncStatus->bq().popSequent(currentNumber + 1, c_maxCommitBlocks);
        for (auto block : blocks)
        {
            // TODO: verify transaction signature before blockVerifier
            ExecutiveContext::Ptr exeCtx = m_blockVerifier->executeBlock(*block);
            m_blockChain->commitBlock(*block, exeCtx);
        }
    }

    currentNumber = m_blockChain->number();
    if (currentNumber >= m_syncStatus->knownHighestNumber)
    {
        assert(m_syncStatus->knownLatestHash ==
               m_blockChain->getBlockByNumber(m_syncStatus->knownHighestNumber)->headerHash());
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
    for (auto const& session : sessions)
    {
        if (!m_syncStatus->hasPeer(session.nodeID))
        {
            SyncPeerInfo newPeer{session.nodeID, 0, m_genesisHash, m_genesisHash};
            m_syncStatus->newSyncPeerStatus(newPeer);
        }
    }
}