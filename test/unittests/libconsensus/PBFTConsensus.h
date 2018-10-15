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
 * @brief: unit test for libconsensus/pbft/PBFTConsensus.*
 * @file: PBFTConsensus.h
 * @author: yujiechen
 * @date: 2018-10-12
 */
#pragma once
#include "FakeConsensus.h"
#include "PBFTReqCache.h"
#include <libconsensus/pbft/PBFTConsensus.h>
#include <libethcore/Protocol.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <boost/test/unit_test.hpp>
#include <memory>
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::txpool;
using namespace dev::blockchain;

namespace dev
{
namespace test
{
/// update the miner list of PBFT Consensus
static void FakePBFTMiner(FakeConsensus<FakePBFTConsensus>& fake_pbft)
{
    fake_pbft.m_minerList.push_back(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->setMinerList(fake_pbft.m_minerList);
    fake_pbft.consensus()->resetConfig();
}

static void compareAsyncSendTime(
    FakeConsensus<FakePBFTConsensus>& fake_pbft, dev::h512 const& nodeID, size_t asyncSendTime)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    BOOST_CHECK(service->getAsyncSendSizeByNodeID(nodeID) == asyncSendTime);
}

/// Fake sessionInfosByProtocolID
static void appendSessionInfo(FakeConsensus<FakePBFTConsensus>& fake_pbft, Public const& node_id)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    SessionInfo info(node_id, m_endpoint, std::vector<std::string>());
    size_t origin_size =
        service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size();
    service->appendSessionInfo(info);
    BOOST_CHECK(service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size() ==
                (origin_size + 1));
}
/// fake session according to node id of the peer
static std::shared_ptr<Session> FakeSession(Public node_id)
{
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
    std::shared_ptr<Session> session = std::make_shared<Session>(
        nullptr, fake_socket, std::make_shared<Peer>(node_id, m_endpoint), PeerSessionInfo());
    return session;
}

/// fake message
template <typename T>
Message::Ptr FakeReqMessage(std::shared_ptr<FakePBFTConsensus> pbft, T const& req,
    uint16_t const& packetType, uint16_t const& protocolId)
{
    bytes data;
    req.encode(data);
    return pbft->transDataToMessage(ref(data), packetType, protocolId);
}

/// check the data received from the network
template <typename T>
void CheckOnRecvPBFTMessage(std::shared_ptr<FakePBFTConsensus> pbft,
    std::shared_ptr<Session> session, T const& req, uint16_t const& packetType,
    bool const& valid = false)
{
    Message::Ptr message_ptr = FakeReqMessage(pbft, req, packetType, ProtocolID::PBFT);
    pbft->onRecvPBFTMessage(P2PException(), session, message_ptr);
    std::pair<bool, PBFTMsgPacket> ret = pbft->mutableMsgQueue().tryPop(unsigned(5));
    if (valid == true)
    {
        BOOST_CHECK(ret.first == true);
        BOOST_CHECK(ret.second.packet_id == packetType);
        T decoded_req;
        decoded_req.decode(ref(ret.second.data));
        BOOST_CHECK(decoded_req == req);
    }
    else
        BOOST_CHECK(ret.first == false);
}

}  // namespace test
}  // namespace dev
