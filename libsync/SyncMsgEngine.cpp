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

void SyncMsgEngine::messageHandler(
    P2PException _e, std::shared_ptr<dev::p2p::SessionFace> _session, Message::Ptr _msg)
{
    SYNCLOG(TRACE) << "[Rcv] [Packet] Receive packet from: " << _session->id() << endl;
    if (!checkSession(_session) || !checkMessage(_msg))
    {
        SYNCLOG(WARNING) << "[Rcv] [Packet] Reject packet: [reason]: session or msg illegal"
                         << endl;
        _session->disconnect(LocalIdentity);
        return;
    }

    SyncMsgPacket packet;
    if (!packet.decode(_session, _msg))
    {
        SYNCLOG(WARNING)
            << "[Rcv] [Packet] Reject packet: [reason/nodeId/size/message]: decode failed/"
            << _session->id() << "/" << _msg->buffer()->size() << "/" << toHex(*_msg->buffer())
            << endl;
        _session->disconnect(BadProtocol);
        return;
    }

    bool ok = interpret(packet);
    if (!ok)
        SYNCLOG(WARNING)
            << "[Rcv] [Packet] Reject packet: [reason/packetType]: illegal packet type/"
            << int(packet.packetType) << endl;
}

bool SyncMsgEngine::checkSession(std::shared_ptr<dev::p2p::SessionFace> _session)
{
    /// TODO: denine LocalIdentity after SyncPeer finished
    if (_session->id() == m_nodeId)
        return false;
    return true;
}

bool SyncMsgEngine::checkMessage(Message::Ptr _msg)
{
    bytesConstRef msgBytes = ref(*_msg->buffer());
    if (msgBytes.size() < 2 || msgBytes[0] > 0x7f)
        return false;
    if (RLP(msgBytes.cropped(1)).actualSize() + 1 != msgBytes.size())
        return false;
    return true;
}

bool SyncMsgEngine::isNewerBlock(shared_ptr<Block> block)
{
    if (block->header().number() <= m_blockChain->number())
        return false;

    // if (block->header()->)
    // if //Check sig list
    // return false;

    return true;
}

bool SyncMsgEngine::interpret(SyncMsgPacket const& _packet)
{
    SYNCLOG(TRACE) << "[Rcv] [Packet] interpret packet type: " << int(_packet.packetType) << endl;
    switch (_packet.packetType)
    {
    case StatusPacket:
        onPeerStatus(_packet);
        break;
    case TransactionsPacket:
        onPeerTransactions(_packet);
        break;
    case BlocksPacket:
        onPeerBlocks(_packet);
        break;
    case ReqBlocskPacket:
        onPeerRequestBlocks(_packet);
        break;
    default:
        return false;
    }
    return true;
}

void SyncMsgEngine::onPeerStatus(SyncMsgPacket const& _packet)
{
    shared_ptr<SyncPeerStatus> status = m_syncStatus->peerStatus(_packet.nodeId);
    SyncPeerInfo info{_packet.nodeId, _packet.rlp()[0].toInt<int64_t>(),
        _packet.rlp()[1].toHash<h256>(), _packet.rlp()[2].toHash<h256>()};

    if (status == nullptr)
    {
        SYNCLOG(TRACE) << "[Rcv] [Status] Peer status new " << info.nodeId << endl;
        m_syncStatus->newSyncPeerStatus(info);
    }
    else
    {
        SYNCLOG(TRACE) << "[Rcv] [Status] Peer status update " << info.nodeId << endl;
        status->update(info);
    }
}

void SyncMsgEngine::onPeerTransactions(SyncMsgPacket const& _packet)
{
    RLP const& rlps = _packet.rlp();
    unsigned itemCount = rlps.itemCount();
    size_t successCnt = 0;

    for (unsigned i = 0; i < itemCount; ++i)
    {
        Transaction tx;
        tx.decode(rlps[i]);

        auto importResult = m_txPool->import(tx);
        if (ImportResult::Success == importResult)
            successCnt++;
        else
            SYNCLOG(TRACE) << "[Rcv] [Tx] Transaction import into txPool FAILED from peer "
                              "[reason/txHash/peer]: "
                           << int(importResult) << "/" << _packet.nodeId << "/" << move(tx.sha3())
                           << endl;


        m_txPool->transactionIsKonwnBy(tx.sha3(), _packet.nodeId);
    }
    SYNCLOG(TRACE) << "[Rcv] [Tx] Peer transactions import [import/rcv/txPool]: " << successCnt
                   << "/" << itemCount << "/" << m_txPool->pendingSize() << " from "
                   << _packet.nodeId << endl;
}

void SyncMsgEngine::onPeerBlocks(SyncMsgPacket const& _packet)
{
    RLP const& rlps = _packet.rlp();
    unsigned itemCount = rlps.itemCount();
    size_t successCnt = 0;
    for (unsigned i = 0; i < itemCount; ++i)
    {
        shared_ptr<Block> block = make_shared<Block>(rlps[i].toBytes());
        if (isNewerBlock(block))
        {
            successCnt++;
            m_syncStatus->bq().push(block);
        }
    }
    SYNCLOG(TRACE) << "[Rcv] [Download] Peer block receive [import/rcv/downloadBlockQueue]: "
                   << successCnt << "/" << itemCount << "/" << m_syncStatus->bq().size() << endl;
}

void SyncMsgEngine::sendBlocks(
    NodeID _nodeId, int64_t _fromNumber, std::vector<dev::bytes> const& _blockRLPs)
{
    if (0 == _blockRLPs.size())
    {
        SYNCLOG(TRACE) << "[Rcv] [Send] [Download] Block back to " << _nodeId << " back[null]"
                       << endl;
        return;
    }

    SyncBlocksPacket retPacket;
    retPacket.encode(_blockRLPs);

    auto msg = retPacket.toMessage(m_protocolId);
    m_service->asyncSendMessageByNodeID(_nodeId, msg);
    SYNCLOG(TRACE) << "[Rcv] [Send] [Download] Block back to " << _nodeId << " back[" << _fromNumber
                   << ", " << _fromNumber + _blockRLPs.size() - 1 << "/ " << msg->buffer()->size()
                   << "B]" << endl;
}

void SyncMsgEngine::onPeerRequestBlocks(SyncMsgPacket const& _packet)
{
    RLP const& rlp = _packet.rlp();

    // request
    int64_t from = rlp[0].toInt<int64_t>();
    unsigned size = rlp[1].toInt<unsigned>();

    SYNCLOG(TRACE) << "[Rcv] [Send] [Download] Block request from " << _packet.nodeId << " req["
                   << from << ", " << from + size - 1 << "]" << endl;

    vector<bytes> blockRLPs;
    size_t shardSize = 0;
    int64_t number = from;
    for (; number < from + size; ++number)
    {
        shared_ptr<Block> block = m_blockChain->getBlockByNumber(number);
        if (!block || block->header().number() != number)
        {
            number++;  // to the right number when the last sendBlocks
            break;
        }

        if (block->rlp().size() > c_maxPayload)
        {
            SYNCLOG(FATAL)
                << "[Rcv] [Send] [Download] Block RLP size is larger than max packet size" << endl;
            return;
        }

        // Send blocks if packet size more than c_maxPayload
        if (shardSize != 0 && shardSize + block->rlp().size() > c_maxPayload)
        {
            sendBlocks(_packet.nodeId, number, blockRLPs);
            blockRLPs.clear();
            shardSize = 0;
        }

        blockRLPs.emplace_back(block->rlp());
        shardSize += block->rlp().size();
    }
    sendBlocks(_packet.nodeId, number - 1, blockRLPs);
}
