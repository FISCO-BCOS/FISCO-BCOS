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
#include <libdevcore/RLP.h>
#include <libnetwork/Common.h>
#include <libp2p/P2PMessageFactory.h>
#include <libp2p/Service.h>

namespace dev
{
namespace sync
{
class SyncMsgPacket
{
public:
    SyncMsgPacket()
    {
        /// TODO:
        /// 1. implement these packet with SyncMsgPacketFactory
        /// 2. modify sync and PBFT related packet from reference to pointer
        if (g_BCOSConfig.version() >= dev::RC2_VERSION)
        {
            m_p2pFactory = std::make_shared<dev::p2p::P2PMessageFactoryRC2>();
        }
        else if (g_BCOSConfig.version() <= dev::RC1_VERSION)
        {
            m_p2pFactory = std::make_shared<dev::p2p::P2PMessageFactory>();
        }
    }
    /// Extract data by decoding the message
    bool decode(
        std::shared_ptr<dev::p2p::P2PSession> _session, std::shared_ptr<dev::p2p::P2PMessage> _msg);

    /// encode is implement in derived class
    /// basic encode function
    RLPStream& prep(RLPStream& _s, unsigned _id, unsigned _args);

    /// Generate p2p message after encode
    std::shared_ptr<dev::p2p::P2PMessage> toMessage(PROTOCOL_ID _protocolId);

    RLP const& rlp() const { return m_rlp; }

public:
    SyncPacketType packetType;
    NodeID nodeId;

protected:
    RLP m_rlp;              /// The result of decode
    RLPStream m_rlpStream;  // The result of encode
    std::shared_ptr<dev::p2p::P2PMessageFactory> m_p2pFactory;

private:
    bool checkPacket(bytesConstRef _msg);
};

class SyncTxsStatusPacket : public SyncMsgPacket
{
public:
    SyncTxsStatusPacket() { packetType = TxsStatusPacket; }
    void encode(int64_t const& _number, std::shared_ptr<std::set<dev::h256>> _txsHash);
};

class SyncTxsReqPacket : public SyncMsgPacket
{
public:
    SyncTxsReqPacket() { packetType = TxsRequestPacekt; }
    void encode(std::shared_ptr<std::vector<dev::h256>> _requestedTxs);
};

class SyncStatusPacket : public SyncMsgPacket
{
public:
    SyncStatusPacket() { packetType = StatusPacket; }
    void encode(int64_t _number, h256 const& _genesisHash, h256 const& _latestHash);
};

class SyncTransactionsPacket : public SyncMsgPacket
{
public:
    SyncTransactionsPacket() { packetType = TransactionsPacket; }
    void encode(std::vector<bytes> const& _txRLPs);
    void encodeRC2(std::vector<bytes> const& _txRLPs);
    dev::p2p::P2PMessage::Ptr toMessage(PROTOCOL_ID _protocolId, bool const& _fromRPC = false);
};

class SyncBlocksPacket : public SyncMsgPacket
{
public:
    SyncBlocksPacket() { packetType = BlocksPacket; }
    void encode(std::vector<dev::bytes> const& _blockRLPs);
    void singleEncode(dev::bytes const& _blockRLP);
};

class SyncReqBlockPacket : public SyncMsgPacket
{
public:
    SyncReqBlockPacket() { packetType = ReqBlocskPacket; }
    void encode(int64_t _from, unsigned _size);
};


}  // namespace sync
}  // namespace dev
