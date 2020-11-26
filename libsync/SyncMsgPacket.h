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

#pragma once
#include "Common.h"
#include <libnetwork/Common.h>
#include <libp2p/P2PMessageFactory.h>
#include <libp2p/Service.h>
#include <libutilities/RLP.h>

namespace bcos
{
namespace sync
{
class SyncMsgPacket
{
public:
    using Ptr = std::shared_ptr<SyncMsgPacket>;
    SyncMsgPacket()
    {
        /// TODO:
        /// 1. implement these packet with SyncMsgPacketFactory
        /// 2. modify sync and PBFT related packet from reference to pointer
        if (g_BCOSConfig.version() >= bcos::RC2_VERSION)
        {
            m_p2pFactory = std::make_shared<bcos::p2p::P2PMessageFactoryRC2>();
        }
        else if (g_BCOSConfig.version() <= bcos::RC1_VERSION)
        {
            m_p2pFactory = std::make_shared<bcos::p2p::P2PMessageFactory>();
        }
    }
    virtual ~SyncMsgPacket() {}

    /// Extract data by decoding the message
    bool decode(std::shared_ptr<bcos::p2p::P2PSession> _session,
        std::shared_ptr<bcos::p2p::P2PMessage> _msg);

    /// encode is implement in derived class
    /// basic encode function
    RLPStream& prep(RLPStream& _s, unsigned _id, unsigned _args);

    /// Generate p2p message after encode
    std::shared_ptr<bcos::p2p::P2PMessage> toMessage(PROTOCOL_ID _protocolId);

    RLP const& rlp() const { return m_rlp; }

public:
    SyncPacketType packetType;
    NodeID nodeId;

protected:
    RLP m_rlp;              /// The result of decode
    RLPStream m_rlpStream;  // The result of encode
    std::shared_ptr<bcos::p2p::P2PMessageFactory> m_p2pFactory;

private:
    bool checkPacket(bytesConstRef _msg);
};

class SyncStatusPacket : public SyncMsgPacket
{
public:
    using Ptr = std::shared_ptr<SyncStatusPacket>;
    SyncStatusPacket() { packetType = StatusPacket; }
    SyncStatusPacket(NodeID const& _nodeId, int64_t const& _number, h256 const& _genesisHash,
        h256 const& _latestHash)
      : nodeId(_nodeId), number(_number), genesisHash(_genesisHash), latestHash(_latestHash)
    {
        packetType = StatusPacket;
    }
    ~SyncStatusPacket() override {}
    virtual void encode();
    virtual void decodePacket(RLP const& _rlp, bcos::h512 const& _peer);

public:
    NodeID nodeId;
    int64_t number;
    h256 genesisHash;
    h256 latestHash;
    int64_t alignedTime;

protected:
    unsigned m_itemCount = 3;
};

// extend SyncStatusPacket with aligned time
class SyncStatusPacketWithAlignedTime : public SyncStatusPacket
{
public:
    using Ptr = std::shared_ptr<SyncStatusPacketWithAlignedTime>;
    SyncStatusPacketWithAlignedTime() : SyncStatusPacket() { m_itemCount = 4; }
    SyncStatusPacketWithAlignedTime(NodeID const& _nodeId, int64_t const& _number,
        h256 const& _genesisHash, h256 const& _latestHash)
      : SyncStatusPacket(_nodeId, _number, _genesisHash, _latestHash)
    {
        m_itemCount = 4;
    }

    ~SyncStatusPacketWithAlignedTime() override {}

    void encode() override;
    void decodePacket(RLP const& _rlp, bcos::h512 const& _peer) override;
};

class SyncTransactionsPacket : public SyncMsgPacket
{
public:
    SyncTransactionsPacket() { packetType = TransactionsPacket; }
    void encode(std::vector<bytes> const& _txRLPs, bool const& _enableTreeRouter = false,
        uint64_t const& _consIndex = 0);
    void encode(std::vector<bytes> const& _txRLPs, unsigned const& _fieldSize);
    bcos::p2p::P2PMessage::Ptr toMessage(PROTOCOL_ID _protocolId, bool const& _fromRPC = false);
};

class SyncBlocksPacket : public SyncMsgPacket
{
public:
    SyncBlocksPacket() { packetType = BlocksPacket; }
    void encode(std::vector<bcos::bytes> const& _blockRLPs);
    void singleEncode(bcos::bytes const& _blockRLP);
};

class SyncReqBlockPacket : public SyncMsgPacket
{
public:
    SyncReqBlockPacket() { packetType = ReqBlocskPacket; }
    void encode(int64_t _from, unsigned _size);
};

// transaction status packet
class SyncTxsStatusPacket : public SyncMsgPacket
{
public:
    SyncTxsStatusPacket() { packetType = TxsStatusPacket; }
    void encode(int64_t const& _number, std::shared_ptr<std::set<bcos::h256>> _txsHash);
};

// transaction request packet
class SyncTxsReqPacket : public SyncMsgPacket
{
public:
    SyncTxsReqPacket() { packetType = TxsRequestPacekt; }
    void encode(std::shared_ptr<std::vector<bcos::h256>> _requestedTxs);
};

}  // namespace sync
}  // namespace bcos
