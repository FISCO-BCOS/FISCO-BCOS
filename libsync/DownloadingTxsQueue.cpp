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
 * @brief : Downloading transactions queue
 * @author: jimmyshi
 * @date: 2019-02-19
 */

#include "DownloadingTxsQueue.h"
#include <tbb/parallel_for.h>

using namespace dev;
using namespace dev::sync;
using namespace dev::eth;

void DownloadingTxsQueue::push(
    SyncMsgPacket const& _packet, dev::p2p::P2PMessage::Ptr _msg, NodeID const& _fromPeer)
{
    DownloadTxsShard txsShard(_packet.rlp().data(), _fromPeer);
    int RPCPacketType = 1;
    if (_msg->packetType() == RPCPacketType && m_syncTransTreeRouter)
    {
        auto selectedNodeList = m_syncTransTreeRouter->selectNodes(m_syncStatus->peersSet());
        // forward the received txs
        for (auto const& selectedNode : *selectedNodeList)
        {
            if (selectedNode == _fromPeer)
            {
                continue;
            }
            m_service->asyncSendMessageByNodeID(selectedNode, _msg, nullptr);
            if (m_statisticHandler)
            {
                m_statisticHandler->updateSendedTxsInfo(_msg->length());
            }
            txsShard.appendForwardNodes(selectedNode);
            SYNC_LOG(DEBUG) << LOG_DESC("forward transaction")
                            << LOG_KV("selectedNode", selectedNode.abridged());
        }
    }
    WriteGuard l(x_buffer);
    m_buffer->emplace_back(txsShard);
    if (m_statisticHandler)
    {
        m_statisticHandler->updateDownloadedTxsBytes(_msg->length());
    }
}


void DownloadingTxsQueue::pop2TxPool(
    std::shared_ptr<dev::txpool::TxPoolInterface> _txPool, dev::eth::CheckTransaction _checkSig)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    // fetch from buffer
    std::shared_ptr<std::vector<DownloadTxsShard>> localBuffer = m_buffer;
    auto moveBuffer_time_cost = utcTime() - record_time;
    record_time = utcTime();
    {
        WriteGuard l(x_buffer);
        m_buffer = std::make_shared<std::vector<DownloadTxsShard>>();
    }
    auto newBuffer_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (_txPool->isFull())
        return;
    auto isBufferFull_time_cost = utcTime() - record_time;
    record_time = utcTime();

    for (size_t i = 0; i < localBuffer->size(); ++i)
    {
        auto maintainBuffer_start_time = utcTime();
        // decode
        auto txs = std::make_shared<dev::eth::Transactions>();
        DownloadTxsShard const& txsShard = (*localBuffer)[i];
        // TODO drop by Txs Shard

        NodeID fromPeer = txsShard.fromPeer;
        auto constructRLP_time_cost = utcTime() - record_time;
        record_time = utcTime();

        if (g_BCOSConfig.version() >= RC2_VERSION)
        {
            RLP const& txsBytesRLP = RLP(ref(txsShard.txsBytes))[0];
            // std::cout << "decode sync txs " << toHex(txsShard.txsBytes) << std::endl;
            dev::eth::TxsParallelParser::decode(
                txs, txsBytesRLP.toBytesConstRef(), _checkSig, true);
        }
        else
        {
            RLP const& txsBytesRLP = RLP(ref(txsShard.txsBytes));
            unsigned txNum = txsBytesRLP.itemCount();
            txs->resize(txNum);
            for (unsigned j = 0; j < txNum; j++)
            {
                (*txs)[j]->decode(txsBytesRLP[j]);
            }
        }

        auto decode_time_cost = utcTime() - record_time;
        record_time = utcTime();

        // parallel verify transaction before import
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, txs->size()), [&](const tbb::blocked_range<size_t>& _r) {
                for (size_t j = _r.begin(); j != _r.end(); ++j)
                {
                    if (!_txPool->txExists((*txs)[j]->sha3()))
                        (*txs)[j]->sender();
                }
            });

        auto verifySig_time_cost = utcTime() - record_time;
        record_time = utcTime();

        // import into tx pool
        size_t successCnt = 0;
        std::vector<dev::h256> knownTxHash;
        for (auto tx : *txs)
        {
            try
            {
                auto importResult = _txPool->import(tx);
                if (dev::eth::ImportResult::Success == importResult)
                    successCnt++;
                else if (dev::eth::ImportResult::AlreadyKnown == importResult)
                {
                    SYNC_LOG(TRACE)
                        << LOG_BADGE("Tx")
                        << LOG_DESC("Import peer transaction into txPool DUPLICATED from peer")
                        << LOG_KV("reason", int(importResult))
                        << LOG_KV("txHash", fromPeer.abridged())
                        << LOG_KV("peer", tx->sha3().abridged());
                }
                else
                {
                    SYNC_LOG(TRACE)
                        << LOG_BADGE("Tx")
                        << LOG_DESC("Import peer transaction into txPool FAILED from peer")
                        << LOG_KV("reason", int(importResult))
                        << LOG_KV("txHash", fromPeer.abridged())
                        << LOG_KV("peer", tx->sha3().abridged());
                }
                knownTxHash.push_back(tx->sha3());
            }
            catch (std::exception& e)
            {
                SYNC_LOG(WARNING) << LOG_BADGE("Tx") << LOG_DESC("Invalid transaction RLP recieved")
                                  << LOG_KV("reason", e.what()) << LOG_KV("rlp", toHex(tx->rlp()));
                continue;
            }
        }
        auto import_time_cost = utcTime() - record_time;
        record_time = utcTime();

        if (knownTxHash.size() > 0)
        {
            _txPool->setTransactionsAreKnownBy(knownTxHash, fromPeer);
        }
        for (auto const& forwardedNode : *(txsShard.forwardNodes))
        {
            _txPool->setTransactionsAreKnownBy(knownTxHash, forwardedNode);
        }

        auto setTxKnownBy_time_cost = utcTime() - record_time;
        record_time = utcTime();

        auto pengdingSize = _txPool->pendingSize();
        auto getPendingSize_time_cost = utcTime() - record_time;
        record_time = utcTime();

        SYNC_LOG(DEBUG) << LOG_BADGE("Tx") << LOG_DESC("Import peer transactions")
                        << LOG_KV("import", successCnt) << LOG_KV("rcv", txs->size())
                        << LOG_KV("txPool", pengdingSize) << LOG_KV("peer", fromPeer.abridged())
                        << LOG_KV("moveBufferTimeCost", moveBuffer_time_cost)
                        << LOG_KV("newBufferTimeCost", newBuffer_time_cost)
                        << LOG_KV("isBufferFullTimeCost", isBufferFull_time_cost)
                        << LOG_KV("constructRLPTimeCost", constructRLP_time_cost)
                        << LOG_KV("decodTimeCost", decode_time_cost)
                        << LOG_KV("verifySigTimeCost", verifySig_time_cost)
                        << LOG_KV("importTimeCost", import_time_cost)
                        << LOG_KV("setTxKnownByTimeCost", setTxKnownBy_time_cost)
                        << LOG_KV("getPendingSizeTimeCost", getPendingSize_time_cost)
                        << LOG_KV("maintainBufferTimeCost", utcTime() - maintainBuffer_start_time)
                        << LOG_KV("totalTimeCostFromStart", utcTime() - start_time);
        if (m_statisticHandler)
        {
            m_statisticHandler->updateDownloadedTxsCount(txs->size());
        }
    }
}
