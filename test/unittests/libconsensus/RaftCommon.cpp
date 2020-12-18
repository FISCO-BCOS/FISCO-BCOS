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
 * @brief: unit test for libconsensus/raft/Common.h
 * @file: RaftCommon.cpp
 * @author: catli
 * @date: 2018-12-27
 */
#include <libconsensus/raft/Common.h>
#include <libutilities/RLP.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace std;
using namespace bcos;
using namespace bcos::test;
using namespace bcos::consensus;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
template <typename _MT>
void InsertBasicMsgInfo(_MT& _msg)
{
    _msg.idx = raft::NodeIndex(0x1);
    _msg.term = 10;
    _msg.height = 10;
    _msg.blockHash =
        h256(*fromHexString("0xeb8b84af3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c"));
}

template <typename _MT>
bool BasisMsgEqual(_MT& _msg1, _MT& _msg2)
{
    if (_msg1.idx != _msg2.idx)
    {
        return false;
    }

    if (_msg1.term != _msg2.term)
    {
        return false;
    }

    if (_msg1.height != _msg2.height)
    {
        return false;
    }

    if (_msg1.blockHash != _msg2.blockHash)
    {
        return false;
    }

    return true;
}

BOOST_FIXTURE_TEST_SUITE(RaftCommonTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testRaftVoteReq)
{
    RaftVoteReq req1;
    RaftVoteReq req2;

    InsertBasicMsgInfo(req1);
    req1.candidate = raft::NodeIndex(0x0);
    req1.lastLeaderTerm = 10;
    req1.lastBlockNumber = 10;

    RLPStream s;
    req1.streamRLPFields(s);
    RLPStream l;
    l.appendList(1).append(s.out());
    bytes out;
    l.swapOut(out);

    RLP r(ref(out));
    req2.populate(r[0]);

    BOOST_CHECK(BasisMsgEqual(req1, req2) == true);
    BOOST_CHECK(req1.candidate == req2.candidate);
    BOOST_CHECK(req1.lastLeaderTerm == req2.lastLeaderTerm);
    BOOST_CHECK(req1.lastBlockNumber == req2.lastBlockNumber);
}

BOOST_AUTO_TEST_CASE(testRaftVoteResp)
{
    RaftVoteResp resp1;
    RaftVoteResp resp2;

    InsertBasicMsgInfo(resp1);
    resp1.voteFlag = VoteRespFlag::VOTE_RESP_GRANTED;
    resp1.lastLeaderTerm = 10;

    RLPStream s;
    resp1.streamRLPFields(s);
    RLPStream l;
    l.appendList(1).append(s.out());
    bytes out;
    l.swapOut(out);

    RLP r(ref(out));
    resp2.populate(r[0]);

    BOOST_CHECK(BasisMsgEqual(resp1, resp2) == true);
    BOOST_CHECK(resp1.voteFlag == resp2.voteFlag);
    BOOST_CHECK(resp1.lastLeaderTerm == resp2.lastLeaderTerm);
}

BOOST_AUTO_TEST_CASE(testRaftHeartBeat)
{
    RaftHeartBeat hb1;
    RaftHeartBeat hb2;

    InsertBasicMsgInfo(hb1);
    hb1.leader = raft::NodeIndex(0x2);
    hb1.uncommitedBlock = bytes();
    hb1.uncommitedBlockNumber = 0x2;

    RLPStream s;
    hb1.streamRLPFields(s);
    RLPStream l;
    l.appendList(1).append(s.out());
    bytes out;
    l.swapOut(out);

    RLP r(ref(out));
    hb2.populate(r[0]);

    BOOST_CHECK(BasisMsgEqual(hb1, hb2) == true);
    BOOST_CHECK(hb1.leader == hb2.leader);
    BOOST_CHECK(hb1.uncommitedBlock == hb2.uncommitedBlock);
    BOOST_CHECK(hb1.uncommitedBlockNumber == hb2.uncommitedBlockNumber);
}

BOOST_AUTO_TEST_CASE(testRaftHeartBeatResp)
{
    RaftHeartBeatResp resp1;
    RaftHeartBeatResp resp2;

    InsertBasicMsgInfo(resp1);
    resp1.uncommitedBlockHash =
        h256(*fromHexString("0xaaaaaaf3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c"));

    RLPStream s;
    resp1.streamRLPFields(s);
    RLPStream l;
    l.appendList(1).append(s.out());
    bytes out;
    l.swapOut(out);

    RLP r(ref(out));
    resp2.populate(r[0]);
    BOOST_CHECK(BasisMsgEqual(resp1, resp2) == true);
    BOOST_CHECK(resp1.uncommitedBlockHash == resp2.uncommitedBlockHash);
}

BOOST_AUTO_TEST_CASE(testBadMessage)
{
    RaftMsg msg;
    InsertBasicMsgInfo(msg);

    RLPStream s;
    msg.streamRLPFields(s);
    RLPStream l;
    l.appendList(1).append(s.out());
    bytes out;
    l.swapOut(out);
    RLP r(ref(out));

    RaftVoteReq voteReq;
    BOOST_CHECK_THROW(voteReq.populate(r[0]), Exception);
    RaftVoteResp voteResp;
    BOOST_CHECK_THROW(voteResp.populate(r[0]), Exception);
    RaftHeartBeat hb;
    BOOST_CHECK_THROW(hb.populate(r[0]), Exception);
    RaftHeartBeatResp hbResp;
    BOOST_CHECK_THROW(hbResp.populate(r[0]), Exception);
    BOOST_CHECK_THROW(msg.populate(RLP()), Exception);
}

BOOST_AUTO_TEST_CASE(testMsgPacket)
{
    RaftMsgPacket packet1;

    packet1.setOtherField(2, Public("0x1234"), "127.0.0.1");
    packet1.packetType = RaftPacketType::RaftHeartBeatPacket;
    packet1.data = *fromHexString("0xfff0fff");

    bytes data;
    packet1.encode(data);

    RaftMsgPacket packet2;
    packet2.decode(ref(data));

    BOOST_CHECK(packet1.packetType == packet2.packetType);
    BOOST_CHECK(packet1.data == packet2.data);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
