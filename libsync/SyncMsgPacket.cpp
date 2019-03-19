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
#include <libp2p/P2PMessage.h>
#include <libp2p/P2PSession.h>
#include <libp2p/Service.h>

using namespace std;
using namespace dev;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::eth;

std::shared_ptr<dev::compress::CompressInterface> SyncMsgPacket::g_compressHandler = nullptr;
bool SyncMsgPacket::decode(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _msg)
{
    if (_msg == nullptr)
        return false;

    bytesConstRef frame = ref(*(_msg->buffer()));
    /// check packet
    if (!g_compressHandler && !checkPacket(frame))
    {
        return false;
    }
    /// get packet type
    packetType = (SyncPacketType)(RLP(frame.cropped(0, 1)).toInt<unsigned>() - c_syncPacketIDBase);
    nodeId = _session->nodeID();
    if (g_compressHandler && (packetType == BlocksPacket || packetType == TransactionsPacket))
    {
        if (!m_compressPtr)
        {
            m_compressPtr = std::make_shared<bytes>();
        }
        size_t uncompress_len = g_compressHandler->uncompress(frame.cropped(1), *m_compressPtr);
        if (!uncompress_len)
        {
            LOG(ERROR) << LOG_BADGE("SyncBlocksPacket")
                       << LOG_DESC("Decode failed for uncompress failed")
                       << LOG_KV("packetType", packetType)
                       << LOG_KV("orgDataSize", frame.cropped(1).size());
            return false;
        }
        if (!checkPacketWithoutType(ref(*m_compressPtr)))
        {
            LOG(ERROR) << LOG_BADGE("SyncBlocksPacket")
                       << LOG_DESC("Decode failed for packet invalid")
                       << LOG_KV("packetType", packetType);
            return false;
        }
        m_rlp = RLP(ref(*m_compressPtr));
        LOG(DEBUG) << LOG_BADGE("SyncBlocksPacket") << LOG_DESC("Decode Success")
                   << LOG_KV("rlpSize", m_rlp.size());
    }
    else
    {
        m_rlp = RLP(frame.cropped(1));
    }
    return true;
}

P2PMessage::Ptr SyncMsgPacket::toMessage(PROTOCOL_ID _protocolId)
{
    P2PMessage::Ptr msg = std::make_shared<P2PMessage>();
    std::shared_ptr<bytes> b = std::make_shared<bytes>();
    /// compress the data except the packetType for blocks and transactions
    if (g_compressHandler && (packetType == BlocksPacket || packetType == TransactionsPacket))
    {
        *b = ref(m_rlpStream.out()).cropped(0, 1).toBytes();
        g_compressHandler->compress(ref(m_rlpStream.out()).cropped(1), *b, b->size());
        msg->setBuffer(b);
    }
    else
    {
        m_rlpStream.swapOut(*b);
        msg->setBuffer(b);
    }

    msg->setProtocolID(_protocolId);
    return msg;
}

bool SyncMsgPacket::checkPacket(bytesConstRef _msg)
{
    if (_msg.size() < 2 || _msg[0] > 0x7f)
        return false;
    if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
        return false;
    return true;
}

bool SyncMsgPacket::checkPacketWithoutType(bytesConstRef _msg)
{
    if (_msg.size() < 1)
    {
        return false;
    }
    if (RLP(_msg).actualSize() != _msg.size())
    {
        return false;
    }
    return true;
}

RLPStream& SyncMsgPacket::prep(RLPStream& _s, unsigned _id, unsigned _args)
{
    packetType = (SyncPacketType)_id;
    return _s.appendRaw(bytes(1, _id + c_syncPacketIDBase)).appendList(_args);
}

void SyncStatusPacket::encode(int64_t _number, h256 const& _genesisHash, h256 const& _latestHash)
{
    m_rlpStream.clear();
    prep(m_rlpStream, StatusPacket, 3) << _number << _genesisHash << _latestHash;
}

void SyncTransactionsPacket::encode(std::vector<bytes> const& _txRLPs)
{
    m_rlpStream.clear();
    bytes txsBytes = dev::eth::TxsParallelParser::encode(_txRLPs);
    prep(m_rlpStream, TransactionsPacket, 1).append(ref(txsBytes));
}

void SyncBlocksPacket::encode(std::vector<dev::bytes> const& _blockRLPs)
{
    m_rlpStream.clear();
    unsigned size = _blockRLPs.size();
    prep(m_rlpStream, BlocksPacket, size);
    for (bytes const& bs : _blockRLPs)
    {
        m_rlpStream.append(bs);
    }
}

void SyncBlocksPacket::singleEncode(dev::bytes const& _blockRLP)
{
    m_rlpStream.clear();
    prep(m_rlpStream, BlocksPacket, 1);
    m_rlpStream.append(_blockRLP);
}

void SyncReqBlockPacket::encode(int64_t _from, unsigned _size)
{
    m_rlpStream.clear();
    prep(m_rlpStream, ReqBlocskPacket, 2) << _from << _size;
}
