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
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : implementation of sync transaction
 * @author: yujiechen
 * @date: 2019-09-16
 */

#include "SyncTransaction.h"
#include "SyncMsgPacket.h"
#include <json/json.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::txpool;

static unsigned const c_maxSendTransactions = 1000;

void SyncTransaction::start()
{
    startWorking();
}

void SyncTransaction::stop()
{
    doneWorking();
    stopWorking();
    // will not restart worker, so terminate it
    terminate();
}

void SyncTransaction::doWork()
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    auto printSyncInfo_time_cost = utcTime() - record_time;
    record_time = utcTime();

    maintainDownloadingTransactions();
    auto maintainDownloadingTransactions_time_cost = utcTime() - record_time;
    record_time = utcTime();

    auto maintainTransactions_time_cost = 0;

    // only maintain transactions for the nodes inner the group
    if (m_needMaintainTransactions && m_newTransactions)
    {
        maintainTransactions();
    }
    maintainTransactions_time_cost = utcTime() - record_time;

    if (m_needForwardRemainTxs)
    {
        forwardRemainingTxs();
    }

    SYNC_LOG(TRACE) << LOG_BADGE("Record") << LOG_DESC("Sync loop time record")
                    << LOG_KV("printSyncInfoTimeCost", printSyncInfo_time_cost)
                    << LOG_KV("maintainDownloadingTransactionsTimeCost",
                           maintainDownloadingTransactions_time_cost)
                    << LOG_KV("maintainTransactionsTimeCost", maintainTransactions_time_cost)
                    << LOG_KV("syncTotalTimeCost", utcTime() - start_time);
}

void SyncTransaction::workLoop()
{
    while (workerState() == WorkerState::Started)
    {
        doWork();
        // no new transactions and the size of transactions need to be broadcasted is zero
        if (idleWaitMs() && !m_newTransactions && m_txQueue->bufferSize() == 0)
        {
            std::unique_lock<std::mutex> l(x_signalled);
            m_signalled.wait_for(l, std::chrono::milliseconds(idleWaitMs()));
        }
    }
}

void SyncTransaction::maintainTransactions()
{
    auto ts = m_txPool->topTransactionsCondition(c_maxSendTransactions, m_nodeId);
    auto txSize = ts.size();
    if (txSize == 0)
    {
        m_newTransactions = false;
        return;
    }
    sendTransactions(ts, false, 0);
}

void SyncTransaction::sendTransactions(
    Transactions const& _ts, bool const& _fastForwardRemainTxs, int64_t const& _startIndex)
{
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;
    auto pendingSize = m_txPool->pendingSize();

    SYNC_LOG(TRACE) << LOG_BADGE("Tx") << LOG_DESC("Transaction need to send ")
                    << LOG_KV("fastForwardRemainTxs", _fastForwardRemainTxs)
                    << LOG_KV("startIndex", _startIndex) << LOG_KV("txs", _ts.size())
                    << LOG_KV("totalTxs", pendingSize);

    NodeIDs selectedPeers;
    // fastforward remaining transactions
    if (_fastForwardRemainTxs)
    {
        selectedPeers = m_fastForwardedNodes;
    }
    else
    {
        // only broadcastTransactions to the consensus nodes
        if (fp_txsReceiversFilter)
        {
            selectedPeers = fp_txsReceiversFilter(m_syncStatus->peersSet());
        }
        else
        {
            selectedPeers = m_syncStatus->peers();
        }
    }

    UpgradableGuard l(m_txPool->xtransactionKnownBy());

    auto endIndex =
        std::min((int64_t)(_startIndex + c_maxSendTransactions - 1), (int64_t)(_ts.size() - 1));
    for (ssize_t i = _startIndex; i <= endIndex; ++i)
    {
        auto const& t = _ts[i];
        NodeIDs peers;

        // TODO: add redundancy when receive transactions from P2P
        if (m_txPool->isTransactionKnownBySomeone(t.sha3()) && !_fastForwardRemainTxs)
        {
            m_txPool->setTransactionIsKnownBy(t.sha3(), m_nodeId);
            continue;
        }

        peers = m_syncStatus->filterPeers(selectedPeers, [&](std::shared_ptr<SyncPeerStatus> _p) {
            bool unsent =
                !m_txPool->isTransactionKnownBy(t.sha3(), m_nodeId) || _fastForwardRemainTxs;
            bool isSealer = _p->isSealer;
            return isSealer && unsent && !m_txPool->isTransactionKnownBy(t.sha3(), _p->nodeId);
        });
        UpgradeGuard ul(l);
        m_txPool->setTransactionIsKnownBy(t.sha3(), m_nodeId);
        if (0 == peers.size())
            continue;
        for (auto const& p : peers)
        {
            peerTransactions[p].push_back(i);
            m_txPool->setTransactionIsKnownBy(t.sha3(), p);
        }
    }

    m_syncStatus->foreachPeerRandom([&](shared_ptr<SyncPeerStatus> _p) {
        std::vector<bytes> txRLPs;
        unsigned txsSize = peerTransactions[_p->nodeId].size();
        if (0 == txsSize)
            return true;  // No need to send

        for (auto const& i : peerTransactions[_p->nodeId])
            txRLPs.emplace_back(_ts[i].rlp(WithSignature));

        SyncTransactionsPacket packet;
        packet.encode(txRLPs);

        auto msg = packet.toMessage(m_protocolId);
        m_service->asyncSendMessageByNodeID(_p->nodeId, msg, CallbackFuncWithSession(), Options());

        // update sended txs information
        if (m_statisticHandler)
        {
            m_statisticHandler->updateSendedTxsInfo(txsSize, msg->length());
        }

        SYNC_LOG(DEBUG) << LOG_BADGE("Tx") << LOG_DESC("Send transaction to peer")
                        << LOG_KV("txNum", int(txsSize))
                        << LOG_KV("fastForwardRemainTxs", _fastForwardRemainTxs)
                        << LOG_KV("startIndex", _startIndex)
                        << LOG_KV("toNodeId", _p->nodeId.abridged())
                        << LOG_KV("messageSize(B)", msg->buffer()->size());
        return true;
    });
}

void SyncTransaction::forwardRemainingTxs()
{
    Guard l(m_fastForwardMutex);
    int64_t currentTxsSize = m_txPool->pendingSize();
    // no need to forward remaining transactions if the txpool is empty
    if (currentTxsSize == 0)
    {
        return;
    }
    auto ts = m_txPool->topTransactions(currentTxsSize);
    int64_t startIndex = 0;
    while (startIndex < currentTxsSize)
    {
        sendTransactions(ts, m_needForwardRemainTxs, startIndex);
        startIndex += c_maxSendTransactions;
    }
    m_needForwardRemainTxs = false;
    m_fastForwardedNodes.clear();
}

void SyncTransaction::maintainDownloadingTransactions()
{
    m_txQueue->pop2TxPool(m_txPool);
}