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
 * @brief : The implementation of callback from p2p
 * @author: jimmyshi
 * @date: 2018-10-17
 */
#include "SyncMsgEngine.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;

static size_t const c_maxPayload = dev::p2p::P2PMessage::MAX_LENGTH - 2048;

void SyncMsgEngine::messageHandler(
    NetworkException, std::shared_ptr<dev::p2p::P2PSession> _session, P2PMessage::Ptr _msg)
{
    if (!checkSession(_session) || !checkMessage(_msg))
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("Reject packet: [reason]: session/msg/group illegal");
        return;
    }

    SyncMsgPacket::Ptr packet = std::make_shared<SyncMsgPacket>();
    if (!packet->decode(_session, _msg))
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("Reject packet") << LOG_KV("reason", "decode failed")
                                 << LOG_KV("nodeId", _session->nodeID().abridged())
                                 << LOG_KV("size", _msg->buffer()->size())
                                 << LOG_KV("message", toHex(*_msg->buffer()));
        return;
    }

    bool ok = interpret(packet, _msg, _session->nodeID());
    if (!ok)
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("Reject packet")
                                 << LOG_KV("reason", "illegal packet type")
                                 << LOG_KV("packetType", int(packet->packetType));
}

bool SyncMsgEngine::checkSession(std::shared_ptr<dev::p2p::P2PSession> _session)
{
    /// TODO: denine LocalIdentity after SyncPeer finished
    if (_session->nodeID() == m_nodeId)
        return false;

    return true;
}

bool SyncMsgEngine::checkMessage(P2PMessage::Ptr _msg)
{
    bytesConstRef msgBytes = ref(*_msg->buffer());
    if (msgBytes.size() < 2 || msgBytes[0] > 0x7f)
        return false;
    if (RLP(msgBytes.cropped(1)).actualSize() + 1 != msgBytes.size())
        return false;
    return true;
}

bool SyncMsgEngine::checkGroupPacket(SyncMsgPacket const& _packet)
{
    return m_syncStatus->hasPeer(_packet.nodeId);
}

bool SyncMsgEngine::interpret(
    SyncMsgPacket::Ptr _packet, dev::p2p::P2PMessage::Ptr _msg, dev::h512 const& _peer)
{
    try
    {
        SYNC_ENGINE_LOG(TRACE) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                               << LOG_DESC("interpret packet type")
                               << LOG_KV("type", int(_packet->packetType));

        auto self = std::weak_ptr<SyncMsgEngine>(shared_from_this());
        switch (_packet->packetType)
        {
        case StatusPacket:
            onPeerStatus(*_packet);
            break;
        case TransactionsPacket:
            m_txsReceiver->enqueue([self, _packet, _msg]() {
                auto msgEngine = self.lock();
                if (msgEngine)
                {
                    msgEngine->onPeerTransactions(_packet, _msg);
                }
            });
            break;
        case BlocksPacket:
            onPeerBlocks(*_packet);
            break;
        case ReqBlocskPacket:
            onPeerRequestBlocks(*_packet);
            break;
        // receive transaction hash, _msg is only used to ensure the life-time for rlps of _packet
        case TxsStatusPacket:
            m_txsWorker->enqueue([self, _packet, _peer, _msg]() {
                auto msgEngine = self.lock();
                if (msgEngine)
                {
                    msgEngine->onPeerTxsStatus(_packet, _peer, _msg);
                }
            });
            break;
        // receive txs-requests,  _msg is only used to ensure the life-time for rlps of _packet
        case TxsRequestPacekt:
            m_txsSender->enqueue([self, _packet, _peer, _msg]() {
                auto msgEngine = self.lock();
                if (msgEngine)
                {
                    msgEngine->onReceiveTxsRequest(_packet, _peer, _msg);
                }
            });
            break;
        default:
            return false;
        }
    }
    catch (std::exception& e)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("Interpret error for") << LOG_KV("reason", e.what());
        return false;
    }
    return true;
}

void SyncMsgEngine::onPeerStatus(SyncMsgPacket const& _packet)
{
    shared_ptr<SyncPeerStatus> status = m_syncStatus->peerStatus(_packet.nodeId);

    RLP const& rlps = _packet.rlp();

    if (rlps.itemCount() != 3)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Status")
                                 << LOG_DESC("Receive invalid status packet format")
                                 << LOG_KV("peer", _packet.nodeId.abridged());
        return;
    }

    SyncPeerInfo info{
        _packet.nodeId, rlps[0].toInt<int64_t>(), rlps[1].toHash<h256>(), rlps[2].toHash<h256>()};

    if (info.genesisHash != m_genesisHash)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Status")
                                 << LOG_DESC(
                                        "Receive invalid status packet with different genesis hash")
                                 << LOG_KV("peer", _packet.nodeId.abridged())
                                 << LOG_KV("genesisHash", info.genesisHash);
        return;
    }

    int64_t currentNumber = m_blockChain->number();
    if (status == nullptr)
    {
        if (currentNumber < info.number)
        {
            m_syncStatus->newSyncPeerStatus(info);
        }
        SYNC_ENGINE_LOG(DEBUG) << LOG_BADGE("Status")
                               << LOG_DESC("Receive status from unknown peer")
                               << LOG_KV("shouldAccept",
                                      (currentNumber < info.number ? "true" : "false"))
                               << LOG_KV("peer", info.nodeId.abridged())
                               << LOG_KV("peerBlockNumber", info.number)
                               << LOG_KV("genesisHash", info.genesisHash.abridged())
                               << LOG_KV("latestHash", info.latestHash.abridged());
    }
    else
    {
        SYNC_ENGINE_LOG(DEBUG) << LOG_BADGE("Status") << LOG_DESC("Receive status from peer")
                               << LOG_KV("peerNodeId", info.nodeId.abridged())
                               << LOG_KV("peerBlockNumber", info.number)
                               << LOG_KV("genesisHash", info.genesisHash.abridged())
                               << LOG_KV("latestHash", info.latestHash.abridged());
        status->update(info);
    }
    if (currentNumber < info.number && m_onNotifyWorker)
    {
        m_onNotifyWorker();
    }
}

bool SyncMsgEngine::blockNumberFarBehind() const
{
    int64_t currentNumber = m_blockChain->number();
    return m_syncStatus->knownHighestNumber - currentNumber > 20;
}

void SyncMsgEngine::onPeerTransactions(SyncMsgPacket::Ptr _packet, dev::p2p::P2PMessage::Ptr _msg)
{
    try
    {
        // Note: checkGroupPacket degrade the speed of receiving transactions
        if (!checkGroupPacket(*_packet))
        {
            SYNC_ENGINE_LOG(DEBUG) << LOG_BADGE("Tx") << LOG_DESC("Drop unknown peer transactions")
                                   << LOG_KV("fromNodeId", _packet->nodeId.abridged());
            return;
        }
        m_txQueue->push(_packet, _msg, _packet->nodeId);
        if (m_onNotifySyncTrans)
        {
            m_onNotifySyncTrans();
        }
    }
    catch (std::exception const& e)
    {
        SYNC_ENGINE_LOG(ERROR) << LOG_DESC("onPeerTransactions exceptioned")
                               << LOG_KV("errorInfo", boost::diagnostic_information(e));
    }
}

void SyncMsgEngine::onPeerBlocks(SyncMsgPacket const& _packet)
{
    RLP const& rlps = _packet.rlp();

    SYNC_ENGINE_LOG(DEBUG) << LOG_BADGE("Download") << LOG_BADGE("BlockSync")
                           << LOG_DESC("Receive peer block packet")
                           << LOG_KV("packetSize(B)", rlps.data().size());

    m_syncStatus->bq().push(rlps);
    // notify sync master to solve DownloadingQueue
    if (m_onNotifyWorker)
    {
        m_onNotifyWorker();
    }
}

void SyncMsgEngine::onPeerRequestBlocks(SyncMsgPacket const& _packet)
{
    if (!checkGroupPacket(_packet))
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                 << LOG_DESC("Drop unknown peer blocks request")
                                 << LOG_KV("fromNodeId", _packet.nodeId.abridged());
        return;
    }

    RLP const& rlp = _packet.rlp();

    if (rlp.itemCount() != 2)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Download") << LOG_BADGE("Request")
                                 << LOG_DESC("Receive invalid request blocks packet format")
                                 << LOG_KV("peer", _packet.nodeId.abridged());
        return;
    }

    // request
    int64_t from = rlp[0].toInt<int64_t>();
    unsigned size = rlp[1].toInt<unsigned>();

    SYNC_ENGINE_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("Request")
                          << LOG_DESC("Receive block request")
                          << LOG_KV("peer", _packet.nodeId.abridged()) << LOG_KV("from", from)
                          << LOG_KV("to", from + size - 1);

    auto peerStatus = m_syncStatus->peerStatus(_packet.nodeId);
    if (peerStatus != nullptr && peerStatus)
    {
        peerStatus->reqQueue.push(from, (int64_t)size);
        // notify sync master to handle block requests
        if (m_onNotifyWorker)
        {
            m_onNotifyWorker();
        }
    }
}

void DownloadBlocksContainer::batchAndSend(BlockPtr _block)
{
    // TODO: thread safe
    std::shared_ptr<bytes> blockRLP = _block->rlpP();

    batchAndSend(blockRLP);
}

void DownloadBlocksContainer::batchAndSend(std::shared_ptr<dev::bytes> _blockRLP)
{
    // TODO: thread safe
    bytes& blockRLP = *_blockRLP;

    if (blockRLP.size() > c_maxPayload)
    {
        sendBigBlock(blockRLP);
        return;
    }

    // Clear and send batch if full
    if (m_currentBatchSize + blockRLP.size() > c_maxPayload)
        clearBatchAndSend();

    // emplace back block in batch
    m_blockRLPsBatch.emplace_back(blockRLP);
    m_currentBatchSize += blockRLP.size();
}

void DownloadBlocksContainer::clearBatchAndSend()
{
    // TODO: thread safe
    if (0 == m_blockRLPsBatch.size())
        return;

    SyncBlocksPacket retPacket;
    retPacket.encode(m_blockRLPsBatch);

    auto msg = retPacket.toMessage(m_protocolId);
    m_service->asyncSendMessageByNodeID(m_nodeId, msg, CallbackFuncWithSession(), Options());
    SYNC_ENGINE_LOG(INFO) << LOG_BADGE("Download") << LOG_BADGE("Request") << LOG_BADGE("BlockSync")
                          << LOG_DESC("Send block packet") << LOG_KV("peer", m_nodeId.abridged())
                          << LOG_KV("blocks", m_blockRLPsBatch.size())
                          << LOG_KV("bytes(V)", msg->buffer()->size());

    m_blockRLPsBatch.clear();
    m_currentBatchSize = 0;
}

void DownloadBlocksContainer::sendBigBlock(bytes const& _blockRLP)
{
    SyncBlocksPacket retPacket;
    retPacket.singleEncode(_blockRLP);

    auto msg = retPacket.toMessage(m_protocolId);
    m_service->asyncSendMessageByNodeID(m_nodeId, msg, CallbackFuncWithSession(), Options());
    SYNC_ENGINE_LOG(INFO) << LOG_BADGE("Rcv") << LOG_BADGE("Send") << LOG_BADGE("Download")
                          << LOG_DESC("Block back") << LOG_KV("peer", m_nodeId.abridged())
                          << LOG_KV("blocks", 1) << LOG_KV("bytes(B)", msg->buffer()->size());
}

// the last param (_msg) is necessary to ensure the life-time of _packet->rlp()
void SyncMsgEngine::onPeerTxsStatus(
    std::shared_ptr<SyncMsgPacket> _packet, dev::h512 const& _peer, dev::p2p::P2PMessage::Ptr)
{
    try
    {
        RLP const& rlps = _packet->rlp();
        // pop all downloaded txs into the txPool
        while (m_txQueue->bufferSize() > 0)
        {
            m_txQueue->pop2TxPool(m_txPool);
        }
        auto blockNumber = m_blockChain->number();
        std::set<dev::h256> txsHash = rlps[1].toSet<dev::h256>();
        // request transaction to the peer
        auto requestTxs = m_txPool->filterUnknownTxs(txsHash);
        if (requestTxs->size() == 0)
        {
            return;
        }
        std::shared_ptr<SyncTxsReqPacket> txsReqPacket = std::make_shared<SyncTxsReqPacket>();
        txsReqPacket->encode(requestTxs);
        auto p2pMsg = txsReqPacket->toMessage(m_protocolId);
        // send request to the peer
        m_service->asyncSendMessageByNodeID(_peer, p2pMsg, nullptr);
        SYNC_ENGINE_LOG(DEBUG) << LOG_DESC("onPeerTxsStatus")
                               << LOG_KV("reqSize", requestTxs->size())
                               << LOG_KV("blockNumber", blockNumber)
                               << LOG_KV("peerTxsSize", txsHash.size())
                               << LOG_KV("peer", _peer.abridged());
    }
    catch (std::exception const& _e)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("invalid txs status")
                                 << LOG_KV("peer", _peer.abridged())
                                 << LOG_KV("reason", boost::diagnostic_information(_e));
    }
}

// the last param (_msg) is necessary to ensure the life-time of _txsReqPacket->rlp()
void SyncMsgEngine::onReceiveTxsRequest(
    std::shared_ptr<SyncMsgPacket> _txsReqPacket, dev::h512 const& _peer, dev::p2p::P2PMessage::Ptr)
{
    try
    {
        RLP const& rlps = _txsReqPacket->rlp();
        std::vector<dev::h256> reqTxs = rlps[0].toVector<dev::h256>();
        auto txs = m_txPool->obtainTransactions(reqTxs);
        if (0 == txs->size())
        {
            return;
        }
        std::shared_ptr<std::vector<bytes>> txRLPs = std::make_shared<std::vector<bytes>>();
        for (auto tx : *txs)
        {
            txRLPs->emplace_back(tx->rlp(WithSignature));
        }
        std::shared_ptr<SyncTransactionsPacket> txsPacket =
            std::make_shared<SyncTransactionsPacket>();
        txsPacket->encode(*txRLPs);
        auto p2pMsg = txsPacket->toMessage(m_protocolId);
        m_service->asyncSendMessageByNodeID(_peer, p2pMsg, CallbackFuncWithSession(), Options());
        if (m_statisticHandler)
        {
            m_statisticHandler->updateSendedTxsInfo(txRLPs->size(), p2pMsg->length());
        }
        SYNC_ENGINE_LOG(DEBUG) << LOG_BADGE("Rcv") << LOG_BADGE("onReceiveTxsRequest")
                               << LOG_KV("sendedTxsSize", txRLPs->size())
                               << LOG_KV("messageSize", p2pMsg->length())
                               << LOG_KV("peer", _peer.abridged());
    }
    catch (std::exception const& _e)
    {
        SYNC_ENGINE_LOG(WARNING) << LOG_BADGE("Rcv") << LOG_BADGE("Packet")
                                 << LOG_DESC("invalid txs request packet")
                                 << LOG_KV("peer", _peer.abridged())
                                 << LOG_KV("reason", boost::diagnostic_information(_e));
    }
}

void SyncMsgEngine::stop()
{
    if (m_service)
    {
        m_service->removeHandlerByProtocolID(m_protocolId);
    }
    if (m_txsWorker)
    {
        m_txsWorker->stop();
    }
    if (m_txsSender)
    {
        m_txsSender->stop();
    }
    if (m_txsReceiver)
    {
        m_txsReceiver->stop();
    }
    SYNC_ENGINE_LOG(INFO) << LOG_DESC("SyncMsgEngine stopped");
}
