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
 * @brief : Sync packet decode and encode
 * @author: jimmyshi
 * @date: 2018-10-18
 */

#include "SyncMsgPacket.h"


using namespace std;
using namespace dev;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::eth;

bool SyncMsgPacket::decode(std::shared_ptr<dev::p2p::Session> _session, dev::p2p::Message::Ptr _msg)
{
    if (_msg == nullptr)
        return false;

    bytesConstRef frame = ref(*(_msg->buffer()));
    if (!checkPacket(frame))
        return false;

    packetType = (SyncPacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();
    nodeId = _session->id();
    m_rlp = RLP(frame.cropped(1));

    return true;
}

Message::Ptr SyncMsgPacket::toMessage(uint16_t _protocolId)
{
    Message::Ptr msg = std::make_shared<Message>();

    std::shared_ptr<bytes> b = std::make_shared<bytes>();
    m_rlpStream.swapOut(*b);
    msg->setBuffer(b);
    msg->setProtocolID(_protocolId);
    return msg;
}

bool SyncMsgPacket::checkPacket(bytesConstRef _msg)
{
    if (_msg[0] > 0x7f || _msg.size() < 2)
        return false;
    if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
        return false;
    return true;
}

RLPStream& SyncMsgPacket::prep(RLPStream& _s, unsigned _id, unsigned _args)
{
    return _s.appendRaw(bytes(1, _id)).appendList(_args);
}

void SyncStatusPacket::encode(int64_t _number, h256 const& _genesisHash, h256 const& _latestHash)
{
    m_rlpStream.clear();
    prep(m_rlpStream, StatusPacket, 3) << _number << _genesisHash << _latestHash;
}

void SyncTransactionsPacket::encode(unsigned _txsSize, bytes const& _txRLPs)
{
    m_rlpStream.clear();
    prep(m_rlpStream, TransactionsPacket, _txsSize).appendRaw(_txRLPs, _txsSize);
}

void SyncBlocksPacket::encode(std::vector<dev::bytes> const& _blockRLPs)
{
    m_rlpStream.clear();
    unsigned size = _blockRLPs.size();
    prep(m_rlpStream, BlocksPacket, size);
    for (bytes const& bs : _blockRLPs)
        m_rlpStream.append(bs);
}

void SyncReqBlockPacket::encode(int64_t _from, unsigned _size)
{
    m_rlpStream.clear();
    prep(m_rlpStream, ReqBlocskPacket, 2) << _from << _size;
}