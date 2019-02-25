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

static unsigned const c_maxSendTransactions = 1000;

void SyncMaster::printSyncInfo()
{
    auto pendingSize = m_txPool->pendingSize();
    NodeIDs peers = m_syncStatus->peers();
    std::string peer_str;
    for (auto& peer : peers)
    {
        peer_str += peer.abridged() + "/";
    }
    SYNC_LOG(TRACE) << "\n[Sync Info] --------------------------------------------\n"
                    << "            IsSyncing:    " << isSyncing() << "\n"
                    << "            Block number: " << m_blockChain->number() << "\n"
                    << "            Block hash:   "
                    << m_blockChain->numberHash(m_blockChain->number()) << "\n"
                    << "            Genesis hash: " << m_syncStatus->genesisHash.abridged() << "\n"
                    << "            TxPool size:  " << pendingSize << "\n"
                    << "            Peers size:   " << m_syncStatus->peers().size() << "\n"
                    << "[Peer Info] --------------------------------------------\n"
                    << "    Host: " << m_nodeId.abridged() << "\n"
                    << "    Peer: " << peer_str << "\n"
                    << "            --------------------------------------------";
}

SyncStatus SyncMaster::status() const
{
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
    syncInfo.push_back(json_spirit::Pair("genesisHash", toHexPrefixed(m_syncStatus->genesisHash)));
    syncInfo.push_back(json_spirit::Pair("nodeId", toHex(m_nodeId)));

    int64_t currentNumber = m_blockChain->number();
    syncInfo.push_back(json_spirit::Pair("blockNumber", currentNumber));
    syncInfo.push_back(
        json_spirit::Pair("latestHash", toHexPrefixed(m_blockChain->numberHash(currentNumber))));
    // syncInfo.push_back(json_spirit::Pair("knownHighestNumber",
    // m_syncStatus->knownHighestNumber)); syncInfo.push_back(json_spirit::Pair("knownLatestHash",
    // toHex(m_syncStatus->knownLatestHash)));
    syncInfo.push_back(json_spirit::Pair("txPoolSize", std::to_string(m_txPool->pendingSize())));

    json_spirit::Array peersInfo;
    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        json_spirit::Object info;
        info.push_back(json_spirit::Pair("nodeId", toHex(_p->nodeId)));
        info.push_back(json_spirit::Pair("genesisHash", toHexPrefixed(_p->genesisHash)));
        info.push_back(json_spirit::Pair("blockNumber", _p->number));
        info.push_back(json_spirit::Pair("latestHash", toHexPrefixed(_p->latestHash)));
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
        SYNC_LOG(ERROR) << LOG_DESC("Consensus verify handler is not set");
        BOOST_THROW_EXCEPTION(SyncVerifyHandlerNotSet());
    }

    startWorking();
}

void SyncMaster::stop()
{
    doneWorking();
    stopWorking();
    // will not restart worker, so terminate it
    terminate();
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
        if (m_needMaintainTransactions && m_newTransactions)
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
    m_signalled.notify_all();
}

bool SyncMaster::isSyncing() const
{
    return m_syncStatus->state != SyncState::Idle;
}

void SyncMaster::maintainTransactions()
{
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;

    auto ts = m_txPool->topTransactionsCondition(c_maxSendTransactions, m_nodeId);
    auto txSize = ts.size();
    auto pendingSize = m_txPool->pendingSize();

    SYNC_LOG(TRACE) << LOG_BADGE("Tx") << LOG_DESC("Transaction need to send ")
                    << LOG_KV("txs", txSize) << LOG_KV("totalTxs", pendingSize);
    UpgradableGuard l(m_txPool->xtransactionKnownBy());
    for (size_t i = 0; i < ts.size(); ++i)
    {
        auto const& t = ts[i];
        NodeIDs peers;
        unsigned _percent = m_txPool->isTransactionKnownBySomeone(t.sha3()) ? 25 : 100;

        peers = m_syncStatus->randomSelection(_percent, [&](std::shared_ptr<SyncPeerStatus> _p) {
            bool unsent = !m_txPool->isTransactionKnownBy(t.sha3(), m_nodeId);
            bool isSealer = _p->isSealer;
            return isSealer && unsent && !m_txPool->isTransactionKnownBy(t.sha3(), _p->nodeId);
        });
        if (0 == peers.size())
            return;
        UpgradeGuard ul(l);
        m_txPool->setTransactionIsKnownBy(t.sha3(), m_nodeId);
        for (auto const& p : peers)
        {
            peerTransactions[p].push_back(i);
            m_txPool->setTransactionIsKnownBy(t.sha3(), p);
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

        auto msg = packet.toMessage(m_protocolId);
        m_service->asyncSendMessageByNodeID(_p->nodeId, msg, CallbackFuncWithSession(), Options());

        SYNC_LOG(DEBUG) << LOG_BADGE("Tx") << LOG_DESC("Send transaction to peer")
                        << LOG_KV("txNum", int(txsSize))
                        << LOG_KV("toNodeId", _p->nodeId.abridged())
                        << LOG_KV("messageSize(B)", msg->buffer()->size());
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

        SYNC_LOG(DEBUG) << LOG_BADGE("Status")
                        << LOG_DESC("Send current status when maintainBlocks")
                        << LOG_KV("number", int(number))
                        << LOG_KV("genesisHash", m_genesisHash.abridged())
                        << LOG_KV("currentHash", currentHash.abridged())
                        << LOG_KV("peer", _p->nodeId.abridged());

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
            SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_DESC("No need to download")
                            << LOG_KV("currentNumber", currentNumber)
                            << LOG_KV("currentSealingNumber", m_currentSealingNumber)
                            << LOG_KV("maxPeerNumber", maxPeerNumber);
            return;  // no need to sync
        }
    }

    // Skip downloading if last if not timeout

    uint64_t currentTime = utcTime();
    if (currentTime - m_lastDownloadingRequestTime < c_downloadingRequestTimeout)
    {
        SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_DESC("Waiting for peers' blocks")
                        << LOG_KV("currentNumber", currentNumber)
                        << LOG_KV("maxRequestNumber", m_maxRequestNumber)
                        << LOG_KV("maxPeerNumber", maxPeerNumber);
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
        SYNC_LOG(TRACE) << LOG_BADGE("Download")
                        << LOG_DESC("No need to sync when blocks are already in queue")
                        << LOG_KV("currentNumber", currentNumber)
                        << LOG_KV("maxRequestNumber", maxRequestNumber);
        return;  // no need to send request block packet
    }

    // Sharding by c_maxRequestBlocks to request blocks
    size_t shardNumber =
        (maxRequestNumber - currentNumber + c_maxRequestBlocks - 1) / c_maxRequestBlocks;
    size_t shard = 0;

    m_maxRequestNumber = 0;  // each request turn has new m_maxRequestNumber
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

            // update max request number
            m_maxRequestNumber = max(m_maxRequestNumber, to);

            SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                            << LOG_DESC("Request blocks") << LOG_KV("frm", from) << LOG_KV("to", to)
                            << LOG_KV("peer", _p->nodeId.abridged());

            ++shard;  // shard move

            return shard < shardNumber && shard < c_maxRequestShards;
        });

        if (!thisTurnFound)
        {
            int64_t from = currentNumber + shard * c_maxRequestBlocks;
            int64_t to = min(from + c_maxRequestBlocks - 1, maxRequestNumber);

            SYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("Request")
                              << LOG_DESC("Couldn't find any peers to request blocks")
                              << LOG_KV("from", from) << LOG_KV("to", to);
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
                    SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                    << LOG_DESC("Download block commit")
                                    << LOG_KV("number", topBlock->header().number())
                                    << LOG_KV("txs", topBlock->transactions().size())
                                    << LOG_KV("hash", topBlock->headerHash().abridged());
                }
                else
                {
                    SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                    << LOG_DESC("Block commit failed")
                                    << LOG_KV("number", topBlock->header().number())
                                    << LOG_KV("txs", topBlock->transactions().size())
                                    << LOG_KV("hash", topBlock->headerHash().abridged());
                }
            }
            else
            {
                SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                                << LOG_DESC("Block of queue top is not new block")
                                << LOG_KV("number", topBlock->header().number())
                                << LOG_KV("txs", topBlock->transactions().size())
                                << LOG_KV("hash", topBlock->headerHash().abridged());
            }
        }
        catch (exception& e)
        {
            SYNC_LOG(ERROR) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                            << LOG_DESC("Block of queue top is not a valid block")
                            << LOG_KV("number", topBlock->header().number())
                            << LOG_KV("txs", topBlock->transactions().size())
                            << LOG_KV("hash", topBlock->headerHash().abridged());
        }

        bq.pop();
        topBlock = bq.top();
    }


    currentNumber = m_blockChain->number();
    // has this request turn finished ?
    if (currentNumber >= m_maxRequestNumber)
        m_lastDownloadingRequestTime = 0;  // reset it to trigger request immediately

    // has download finished ?
    if (currentNumber >= m_syncStatus->knownHighestNumber)
    {
        h256 const& latestHash =
            m_blockChain->getBlockByNumber(m_syncStatus->knownHighestNumber)->headerHash();
        SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                        << LOG_DESC("Download finish")
                        << LOG_KV("latestHash", latestHash.abridged())
                        << LOG_KV("expectedHash", m_syncStatus->knownLatestHash.abridged());

        if (m_syncStatus->knownLatestHash != latestHash)
            SYNC_LOG(FATAL)
                << LOG_BADGE("Download")
                << LOG_DESC(
                       "State error: This node's version is not compatable with others! All data "
                       "should be cleared of this node before restart");

        return true;
    }
    return false;
}

void SyncMaster::maintainPeersConnection()
{
    // Get active peers
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    set<NodeID> activePeers;
    for (auto const& session : sessions)
    {
        activePeers.insert(session.nodeID);
    }

    // Get sealers and observer
    NodeIDs sealers = m_blockChain->sealerList();
    NodeIDs sealerOrObserver = sealers + m_blockChain->observerList();

    // member set is [(sealer || observer) && activePeer && not myself]
    set<NodeID> memberSet;
    bool hasMyself = false;
    for (auto const& member : sealerOrObserver)
    {
        /// find active peers
        if (activePeers.find(member) != activePeers.end() && member != m_nodeId)
        {
            memberSet.insert(member);
        }
        hasMyself |= (member == m_nodeId);
    }

    // Delete uncorrelated peers(only if the node the sealer or observer, check the identities of
    // other peers)
    if (hasMyself)
    {
        NodeIDs nodeIds = m_syncStatus->peers();
        for (NodeID const& id : nodeIds)
        {
            if (memberSet.find(id) == memberSet.end())
            {
                m_syncStatus->deletePeer(id);
            }
        }
    }


    // Add new peers
    int64_t currentNumber = m_blockChain->number();
    h256 const& currentHash = m_blockChain->numberHash(currentNumber);
    for (auto const& member : memberSet)
    {
        if (member != m_nodeId && !m_syncStatus->hasPeer(member))
        {
            // create a peer
            SyncPeerInfo newPeer{member, 0, m_genesisHash, m_genesisHash};
            m_syncStatus->newSyncPeerStatus(newPeer);

            // send my status to her
            SyncStatusPacket packet;
            packet.encode(currentNumber, m_genesisHash, currentHash);

            m_service->asyncSendMessageByNodeID(
                member, packet.toMessage(m_protocolId), CallbackFuncWithSession(), Options());
            SYNC_LOG(DEBUG) << LOG_BADGE("Status") << LOG_DESC("Send current status to new peer")
                            << LOG_KV("number", int(currentNumber))
                            << LOG_KV("genesisHash", m_genesisHash.abridged())
                            << LOG_KV("currentHash", currentHash.abridged())
                            << LOG_KV("peer", member.abridged());
        }
    }

    // Update sync sealer status
    set<NodeID> sealerSet;
    for (auto sealer : sealers)
        sealerSet.insert(sealer);

    m_syncStatus->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        _p->isSealer = (sealerSet.find(_p->nodeId) != sealerSet.end());
        return true;
    });

    // If myself is not in group, ignore receive packet checking from all peers
    m_msgEngine->needCheckPacketInGroup = hasMyself;

    // If myself is not in group, no need to maintain transactions(send transactions to peers)
    m_needMaintainTransactions = hasMyself;
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
                    SYNC_LOG(TRACE) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                    << LOG_DESC("Get block for node failed")
                                    << LOG_KV("reason", "block is null") << LOG_KV("number", number)
                                    << LOG_KV("nodeId", _p->nodeId.abridged());
                    break;
                }
                else if (block->header().number() != number)
                {
                    SYNC_LOG(TRACE)
                        << LOG_BADGE("Download") << LOG_BADGE("Request")
                        << LOG_DESC("Get block for node failed")
                        << LOG_KV("reason", "number incorrect") << LOG_KV("number", number)
                        << LOG_KV("nodeId", _p->nodeId.abridged());
                    break;
                }

                blockContainer.batchAndSend(block);
            }

            if (req.fromNumber < number)
                SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                << LOG_BADGE("BlockSync") << LOG_DESC("Send blocks")
                                << LOG_KV("from", req.fromNumber) << LOG_KV("to", number - 1)
                                << LOG_KV("peer", _p->nodeId.abridged());

            if (number < numberLimit)  // This respond not reach the end due to timeout
            {
                // write back the rest request range
                reqQueue.enablePush();
                SYNC_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                << LOG_DESC("Push unsent requests back to reqQueue")
                                << LOG_KV("from", number) << LOG_KV("to", numberLimit - 1)
                                << LOG_KV("peer", _p->nodeId.abridged());
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
        SYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                          << LOG_DESC("Ignore illegal block") << LOG_KV("reason", "number illegal")
                          << LOG_KV("thisNumber", _block->header().number())
                          << LOG_KV("currentNumber", currentNumber);
        return false;
    }

    if (m_blockChain->numberHash(currentNumber) != _block->header().parentHash())
    {
        SYNC_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                          << LOG_DESC("Ignore illegal block")
                          << LOG_KV("reason", "parent hash illegal")
                          << LOG_KV("thisNumber", _block->header().number())
                          << LOG_KV("currentNumber", currentNumber)
                          << LOG_KV("thisParentHash", _block->header().parentHash().abridged())
                          << LOG_KV(
                                 "currentHash", m_blockChain->numberHash(currentNumber).abridged());
        return false;
    }

    // check block sealerlist sig
    if (fp_isConsensusOk && !(fp_isConsensusOk)(*_block))
    {
        SYNC_LOG(ERROR) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                        << LOG_DESC("Ignore illegal block")
                        << LOG_KV("reason", "consensus check failed")
                        << LOG_KV("thisNumber", _block->header().number())
                        << LOG_KV("currentNumber", currentNumber)
                        << LOG_KV("thisParentHash", _block->header().parentHash().abridged())
                        << LOG_KV(
                               "currentHash", m_blockChain->numberHash(currentNumber).abridged());
        return false;
    }

    return true;
}
