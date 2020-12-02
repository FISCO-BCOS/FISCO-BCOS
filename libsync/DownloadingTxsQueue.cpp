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

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::eth;

void DownloadingTxsQueue::push(
    SyncMsgPacket::Ptr _packet, bcos::p2p::P2PMessage::Ptr _msg, NodeID const& _fromPeer)
{
    std::shared_ptr<DownloadTxsShard> txsShard =
        std::make_shared<DownloadTxsShard>(_packet->rlp().data(), _fromPeer);
    int RPCPacketType = 1;
    if (_msg->packetType() == RPCPacketType && m_treeRouter)
    {
        int64_t consIndex = _packet->rlp()[1].toPositiveInt64();
        SYNC_LOG(DEBUG) << LOG_DESC("receive and send transactions by tree")
                        << LOG_KV("consIndex", consIndex)
                        << LOG_KV("fromPeer", _fromPeer.abridged());

        auto selectedNodeList = m_treeRouter->selectNodes(m_syncStatus->peersSet(), consIndex);
        // append all the parent nodes into the knownNode
        auto parentNodeList = m_treeRouter->selectParent(m_syncStatus->peersSet(), consIndex, true);
        for (auto const& _parentNodeId : *parentNodeList)
        {
            txsShard->appendKnownNode(_parentNodeId);
        }
        // forward the received txs
        for (auto const& selectedNode : *selectedNodeList)
        {
            if (selectedNode == _fromPeer || !m_service)
            {
                continue;
            }
            m_service->asyncSendMessageByNodeID(selectedNode, _msg, nullptr);
            txsShard->appendKnownNode(selectedNode);
            SYNC_LOG(DEBUG) << LOG_DESC("forward transaction")
                            << LOG_KV("selectedNode", selectedNode.abridged());
        }
    }
    WriteGuard l(x_buffer);
    m_buffer->emplace_back(txsShard);
}


void DownloadingTxsQueue::pop2TxPool(
    std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool, bcos::eth::CheckTransaction _checkSig)
{
    try
    {
        auto start_time = utcTime();
        auto record_time = utcTime();
        int64_t moveBuffer_time_cost = 0;
        int64_t newBuffer_time_cost = 0;
        auto isBufferFull_time_cost = utcTime() - record_time;
        record_time = utcTime();
        // fetch from buffer(only one thread can callback this function)
        std::shared_ptr<std::vector<std::shared_ptr<DownloadTxsShard>>> localBuffer;
        {
            Guard ml(m_mutex);
            UpgradableGuard l(x_buffer);
            if (m_buffer->size() == 0)
            {
                return;
            }
            localBuffer = m_buffer;
            moveBuffer_time_cost = utcTime() - record_time;
            record_time = utcTime();
            UpgradeGuard ul(l);
            m_buffer = std::make_shared<std::vector<std::shared_ptr<DownloadTxsShard>>>();
            newBuffer_time_cost = utcTime() - record_time;
        }
        // the node is not the group member, return without submit the transaction to the txPool
        if (!m_needImportToTxPool)
        {
            SYNC_LOG(DEBUG) << LOG_DESC("stop pop2TxPool for the node is not belong to the group")
                            << LOG_KV("pendingTxsSize", _txPool->pendingSize())
                            << LOG_KV("shardSize", m_buffer->size());
            return;
        }
        auto maintainBuffer_start_time = utcTime();
        int64_t decode_time_cost = 0;
        int64_t verifySig_time_cost = 0;
        int64_t import_time_cost = 0;
        size_t successCnt = 0;
        for (size_t i = 0; i < localBuffer->size(); ++i)
        {
            record_time = utcTime();
            // decode
            auto txs = std::make_shared<bcos::eth::Transactions>();
            std::shared_ptr<DownloadTxsShard> txsShard = (*localBuffer)[i];
            // TODO drop by Txs Shard

            RLP const& txsBytesRLP = RLP(ref(txsShard->txsBytes))[0];
            bcos::eth::TxsParallelParser::decode(
                txs, txsBytesRLP.toBytesConstRef(), _checkSig, true);
            decode_time_cost += (utcTime() - record_time);
            record_time = utcTime();

            // parallel verify transaction before import
            tbb::parallel_for(tbb::blocked_range<size_t>(0, txs->size()),
                [&](const tbb::blocked_range<size_t>& _r) {
                    for (size_t j = _r.begin(); j != _r.end(); ++j)
                    {
                        try
                        {
                            if (!_txPool->txExists((*txs)[j]->hash()))
                            {
                                (*txs)[j]->sender();
                            }
                        }
                        catch (std::exception const& _e)
                        {
                            SYNC_LOG(WARNING) << LOG_DESC("verify sender for tx failed")
                                              << LOG_KV("reason", boost::diagnostic_information(_e))
                                              << LOG_KV("hash", (*txs)[j]->hash());
                        }
                    }
                });
            verifySig_time_cost += (utcTime() - record_time);

            // import into tx pool
            record_time = utcTime();
            NodeID fromPeer = txsShard->fromPeer;
            for (auto tx : *txs)
            {
                try
                {
                    auto importResult = _txPool->import(tx);
                    if (bcos::eth::ImportResult::Success == importResult)
                    {
                        tx->appendNodeContainsTransaction(fromPeer);
                        tx->appendNodeListContainTransaction(*(txsShard->knownNodes));
                        successCnt++;
                    }
                    else if (bcos::eth::ImportResult::AlreadyKnown == importResult)
                    {
                        SYNC_LOG(TRACE)
                            << LOG_BADGE("Tx")
                            << LOG_DESC("Import peer transaction into txPool DUPLICATED from peer")
                            << LOG_KV("reason", int(importResult))
                            << LOG_KV("peer", fromPeer.abridged())
                            << LOG_KV("txHash", tx->hash().abridged());
                    }
                    else
                    {
                        SYNC_LOG(TRACE)
                            << LOG_BADGE("Tx")
                            << LOG_DESC("Import peer transaction into txPool FAILED from peer")
                            << LOG_KV("reason", int(importResult))
                            << LOG_KV("peer", fromPeer.abridged())
                            << LOG_KV("txHash", tx->hash().abridged());
                    }
                }
                catch (std::exception& e)
                {
                    SYNC_LOG(WARNING)
                        << LOG_BADGE("Tx") << LOG_DESC("Invalid transaction RLP received")
                        << LOG_KV("hash", tx->hash().abridged()) << LOG_KV("reason", e.what())
                        << LOG_KV("rlp", *toHexString(tx->rlp()));
                    continue;
                }
            }
            import_time_cost += (utcTime() - record_time);
        }
        SYNC_LOG(TRACE) << LOG_BADGE("Tx") << LOG_DESC("Import peer transactions")
                        << LOG_KV("import", successCnt)
                        << LOG_KV("moveBufferTimeCost", moveBuffer_time_cost)
                        << LOG_KV("newBufferTimeCost", newBuffer_time_cost)
                        << LOG_KV("isBufferFullTimeCost", isBufferFull_time_cost)
                        << LOG_KV("decodTimeCost", decode_time_cost)
                        << LOG_KV("verifySigTimeCost", verifySig_time_cost)
                        << LOG_KV("importTimeCost", import_time_cost)
                        << LOG_KV("maintainBufferTimeCost", utcTime() - maintainBuffer_start_time)
                        << LOG_KV("totalTimeCostFromStart", utcTime() - start_time);
    }
    catch (std::exception const& _e)
    {
        SYNC_LOG(ERROR) << LOG_DESC("pop2TxPool failed")
                        << LOG_KV("reason", boost::diagnostic_information(_e));
    }
}
