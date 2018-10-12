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
 * @file: PBFTConsensus.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "PBFTConsensus.h"
#include "Common.h"
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(pbftConsensusTest, TestOutputHelperFixture)

/// test constructor (invalid ProtocolID will cause exception)
/// test initPBFTEnv (normal case)
BOOST_AUTO_TEST_CASE(testInitPBFTEnvNormalCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    BOOST_CHECK(fake_pbft.consensus()->keyPair().pub() != h512());
    BOOST_CHECK(fake_pbft.consensus()->broadCastCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache());
    /// init pbft env
    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    /// check level db has already been openend
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    BOOST_CHECK(fake_pbft.consensus()->consensusBlockNumber() == 0);
    BOOST_CHECK(fake_pbft.consensus()->toView() == u256(0));
    BOOST_CHECK(fake_pbft.consensus()->view() == u256(0));
    /// check resetConfig
    BOOST_CHECK(fake_pbft.consensus()->accountType() != NodeAccountType::MinerAccount);
    fake_pbft.m_minerList.push_back(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->setMinerList(fake_pbft.m_minerList);
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->accountType() == NodeAccountType::MinerAccount);
    BOOST_CHECK(fake_pbft.consensus()->nodeIdx() == u256(fake_pbft.m_minerList.size() - 1));
    BOOST_CHECK(fake_pbft.consensus()->minerList().size() == fake_pbft.m_minerList.size());
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == u256(fake_pbft.m_minerList.size()));
    BOOST_CHECK(
        fake_pbft.consensus()->f() == u256((fake_pbft.consensus()->nodeNum() - u256(1)) / u256(3)));

    /// check reloadMsg: empty committedPrepareCache
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check m_timeManager
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime > 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastExecFinishTime <= utcTime());
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastExecBlockFiniTime));
    BOOST_CHECK((fake_pbft.consensus()->timeManager().m_lastExecFinishTime) ==
                (fake_pbft.consensus()->timeManager().m_lastConsensusTime));
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_viewTimeout ==
                fake_pbft.consensus()->timeManager().m_intervalBlockTime * 3);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_changeCycle == 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastGarbageCollection <=
                std::chrono::system_clock::now());
}
/// test exception case of initPBFT
BOOST_AUTO_TEST_CASE(testInitPBFTExceptionCase)
{
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    boost::filesystem::create_directories(boost::filesystem::path("./invalid"));
    fake_pbft.consensus()->setBaseDir("./invalid");
    BOOST_CHECK_THROW(fake_pbft.consensus()->initPBFTEnv(1000), NotEnoughAvailableSpace);
}


/// test onRecvPBFTMessage
BOOST_AUTO_TEST_CASE(testOnRecvPBFTMessage)
{
    KeyPair key_pair;
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    /// fake FakePBFTConsensus
    FakeConsensus<FakePBFTConsensus> fake_pbft(1, ProtocolID::PBFT);
    NodeIPEndpoint endpoint;
    /// fake session
    std::shared_ptr<Session> session = FakeSession(key_pair.pub());
    ///------ test invalid case(recv message from own-node)
    /// check onreceive prepare request
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, prepare_req, PrepareReqPacket, false);
    /// check onreceive sign request
    SignReq sign_req(prepare_req, key_pair, prepare_req.idx + u256(100));
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, sign_req, SignReqPacket, false);
    /// check onReceive commit request
    CommitReq commit_req(prepare_req, key_pair, prepare_req.idx + u256(10));
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, commit_req, CommitReqPacket, false);
    /// test viewchange case
    ViewChangeReq viewChange_req(
        key_pair, prepare_req.height, prepare_req.view, prepare_req.idx, prepare_req.block_hash);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session, viewChange_req, ViewChangeReqPacket, false);

    KeyPair key_pair2 = KeyPair::create();
    std::shared_ptr<Session> session2 = FakeSession(fake_pbft.m_minerList[0]);
    /// test invalid case: this node is not miner
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, false);
    ///----- test valid case
    /// test recv packet from other nodes
    FakePBFTMiner(fake_pbft);  // set this node to be miner
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, sign_req, SignReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, commit_req, CommitReqPacket, true);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session2, viewChange_req, ViewChangeReqPacket, true);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
