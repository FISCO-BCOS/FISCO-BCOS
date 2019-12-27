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
#include "PBFTEngine.h"
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
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    BOOST_CHECK(fake_pbft.consensus()->keyPair().pub() != h512());
    BOOST_CHECK(fake_pbft.consensus()->broadCastCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache());
    /// init pbft env
    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_emptyBlockGenTime * 3);
    /// check level db has already been openend
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    BOOST_CHECK(fake_pbft.consensus()->consensusBlockNumber() == 0);
    BOOST_CHECK(fake_pbft.consensus()->toView() == 0);
    BOOST_CHECK(fake_pbft.consensus()->view() == 0);
    /// check resetConfig
    BOOST_CHECK(fake_pbft.consensus()->accountType() != NodeAccountType::SealerAccount);
    fake_pbft.m_sealerList.push_back(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->appendSealer(fake_pbft.consensus()->keyPair().pub());
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->accountType() == NodeAccountType::SealerAccount);
    BOOST_CHECK(fake_pbft.consensus()->nodeIdx() == fake_pbft.m_sealerList.size() - 1);
    BOOST_CHECK(fake_pbft.consensus()->sealerList().size() == fake_pbft.m_sealerList.size());
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == fake_pbft.m_sealerList.size());
    BOOST_CHECK(fake_pbft.consensus()->fValue() == (fake_pbft.consensus()->nodeNum() - 1) / 3);

    /// check reloadMsg: empty committedPrepareCache
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check m_timeManager
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_viewTimeout ==
                fake_pbft.consensus()->timeManager().m_emptyBlockGenTime * 3);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_changeCycle == 0);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastGarbageCollection <=
                std::chrono::steady_clock::now());
}

/// test onRecvPBFTMessage
BOOST_AUTO_TEST_CASE(testOnRecvPBFTMessage)
{
    KeyPair key_pair;
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    /// fake FakePBFTEngine
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    NodeIPEndpoint endpoint;
    /// fake session
    std::shared_ptr<FakeSession> session = FakeSessionFunc(key_pair.pub());
    ///------ test invalid case(recv message from own-node)
    /// check onreceive prepare request
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, prepare_req, PrepareReqPacket, false);
    /// check onreceive sign request
    SignReq sign_req(prepare_req, key_pair, prepare_req.idx + 100);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, sign_req, SignReqPacket, false);
    /// check onReceive commit request
    CommitReq commit_req(prepare_req, key_pair, prepare_req.idx + 10);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session, commit_req, CommitReqPacket, false);
    /// test viewchange case
    ViewChangeReq viewChange_req(
        key_pair, prepare_req.height, prepare_req.view, prepare_req.idx, prepare_req.block_hash);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session, viewChange_req, ViewChangeReqPacket, false);

    KeyPair key_pair2 = KeyPair::create();
    std::shared_ptr<FakeSession> session2 = FakeSessionFunc(fake_pbft.m_sealerList[0]);
    /// test invalid case: this node is not sealer
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, false);
    ///----- test valid case
    /// test recv packet from other nodes
    FakePBFTSealer(fake_pbft);  // set this node to be sealer
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, prepare_req, PrepareReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, sign_req, SignReqPacket, true);
    CheckOnRecvPBFTMessage(fake_pbft.consensus(), session2, commit_req, CommitReqPacket, true);
    CheckOnRecvPBFTMessage(
        fake_pbft.consensus(), session2, viewChange_req, ViewChangeReqPacket, true);
}
/// test broadcastMsg
BOOST_AUTO_TEST_CASE(testBroadcastMsg)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    KeyPair peer_keyPair = KeyPair::create();
    /// append session info1
    appendSessionInfo(fake_pbft, peer_keyPair.pub());
    /// append session info2
    KeyPair peer2_keyPair = KeyPair::create();
    appendSessionInfo(fake_pbft, peer2_keyPair.pub());
    KeyPair key_pair;
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    bytes data;
    prepare_req.encode(data);
    /// case1: all peer is not the sealer, stop broadcasting
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.uniqueKey(), ref(data));
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == false);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 0);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 0);

    /// case2: only one peer is the sealer, broadcast to one node
    fake_pbft.m_sealerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer_keyPair.pub());
    FakePBFTSealer(fake_pbft);
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.uniqueKey(), ref(data));

    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == true);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 0);

    /// case3: two nodes are all sealers , but one peer is in the cache, broadcast to one node
    fake_pbft.m_sealerList.push_back(peer2_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer2_keyPair.pub());
    FakePBFTSealer(fake_pbft);
    fake_pbft.consensus()->broadcastMsg(PrepareReqPacket, prepare_req.uniqueKey(), ref(data));

    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == true);
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer2_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == true);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
    compareAsyncSendTime(fake_pbft, peer2_keyPair.pub(), 1);

    /// case3: the sealer-peer in the filter
    KeyPair peer3_keyPair = KeyPair::create();
    appendSessionInfo(fake_pbft, peer3_keyPair.pub());
    fake_pbft.m_sealerList.push_back(peer3_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer3_keyPair.pub());
    FakePBFTSealer(fake_pbft);
    /// fake the filter with node id of peer3
    std::unordered_set<h512> filter;
    filter.insert(peer3_keyPair.pub());
    fake_pbft.consensus()->broadcastMsg(
        PrepareReqPacket, prepare_req.uniqueKey(), ref(data), filter);

    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer3_keyPair.pub(), PrepareReqPacket, prepare_req.uniqueKey()) == true);
    compareAsyncSendTime(fake_pbft, peer3_keyPair.pub(), 0);
}

/// test broadcastSignReq and broadcastCommitReq
BOOST_AUTO_TEST_CASE(testBroadcastSignAndCommitReq)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    /// check broadcastSignReq
    SignReq sign_req;
    checkBroadcastSpecifiedMsg(fake_pbft, sign_req, SignReqPacket);
    /// check broadcastCommitReq
    CommitReq commit_req;
    checkBroadcastSpecifiedMsg(fake_pbft, commit_req, CommitReqPacket);
}

/// test broadcastViewChangeReq
BOOST_AUTO_TEST_CASE(testBroadcastViewChangeReq)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);

    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    BlockHeader highest = p_blockChain->getBlockByNumber(p_blockChain->number())->header();
    fake_pbft.consensus()->setHighest(highest);

    ViewChangeReq viewChange_req(fake_pbft.consensus()->keyPair(), highest.number(),
        fake_pbft.consensus()->toView(), 0, highest.hash());
    std::string key = viewChange_req.uniqueKey();

    KeyPair peer_keyPair = KeyPair::create();
    /// append session info
    appendSessionInfo(fake_pbft, peer_keyPair.pub());

    /// case1: the peer node is not sealer
    fake_pbft.consensus()->broadcastViewChangeReq();
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), ViewChangeReqPacket, key) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 0);

    /// case2: the the peer node is a sealer
    fake_pbft.m_sealerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer_keyPair.pub());
    FakePBFTSealer(fake_pbft);
    fake_pbft.consensus()->broadcastViewChangeReq();
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), ViewChangeReqPacket, key) == false);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
}

/// test timeout
BOOST_AUTO_TEST_CASE(testTimeout)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_emptyBlockGenTime);

    VIEWTYPE oriToView = fake_pbft.consensus()->toView();
    TimeManager& timeManager = fake_pbft.consensus()->mutableTimeManager();
    unsigned oriChangeCycle = timeManager.m_changeCycle;

    ///< expect to no timeout
    timeManager.m_lastConsensusTime = timeManager.m_lastSignTime = utcSteadyTime();
    fake_pbft.consensus()->checkTimeout();
    BOOST_CHECK(fake_pbft.consensus()->toView() == oriToView);

    ///< expect to timeout, first timeout interval is 3000 because m_changeCycle is 0
    timeManager.m_lastConsensusTime = timeManager.m_lastSignTime = utcSteadyTime() - 5000;
    fake_pbft.consensus()->checkTimeout();
    BOOST_CHECK(fake_pbft.consensus()->toView() == oriToView + 1);
    BOOST_CHECK(timeManager.m_changeCycle == oriChangeCycle + 1);

    ///< expect to no timeout
    fake_pbft.consensus()->checkTimeout();
    BOOST_CHECK(fake_pbft.consensus()->toView() == oriToView + 1);
    BOOST_CHECK(timeManager.m_changeCycle == oriChangeCycle + 1);
}

/// test checkAndChangeView
BOOST_AUTO_TEST_CASE(testCheckAndChangeView)
{
    // 7 nodes
    FakeConsensus<FakePBFTEngine> fake_pbft(7, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_emptyBlockGenTime);
    fake_pbft.consensus()->resetConfig();

    ///< timeout situation
    VIEWTYPE oriToView = fake_pbft.consensus()->toView();
    TimeManager& timeManager = fake_pbft.consensus()->mutableTimeManager();
    unsigned oriChangeCycle = timeManager.m_changeCycle;
    timeManager.m_lastConsensusTime = timeManager.m_lastSignTime = utcSteadyTime() - 5000;
    fake_pbft.consensus()->checkTimeout();
    BOOST_CHECK(fake_pbft.consensus()->toView() == oriToView + 1);
    BOOST_CHECK(timeManager.m_changeCycle == oriChangeCycle + 1);

    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    BlockHeader highest = p_blockChain->getBlockByNumber(p_blockChain->number())->header();
    fake_pbft.consensus()->setHighest(highest);

    ///< receiving another 4 viewchange mesages will trigger view change in consensus
    VIEWTYPE oriview = fake_pbft.consensus()->view();
    for (size_t i = 1; i < fake_pbft.m_sealerList.size(); i++)
    {
        ViewChangeReq viewChange_req(KeyPair(fake_pbft.m_secrets[i]), highest.number(),
            fake_pbft.consensus()->toView(), i, highest.hash());
        fake_pbft.consensus()->reqCache()->addViewChangeReq(viewChange_req);
        IDXTYPE size =
            fake_pbft.consensus()->reqCache()->getViewChangeSize(fake_pbft.consensus()->toView());
        fake_pbft.consensus()->checkAndChangeView();
        if (3 >= size)
        {
            BOOST_CHECK(fake_pbft.consensus()->view() == oriview);
        }
        else if (4 == size)
        {
            ///< view change triggered, view += 1
            BOOST_CHECK(fake_pbft.consensus()->view() == fake_pbft.consensus()->toView());
            break;
        }
    }
}

/// test checkAndSave && reportBlock
BOOST_AUTO_TEST_CASE(testCheckAndSave)
{
    /// fake collected commitReq and signReq
    BlockHeader highest;
    size_t invalid_height = 2;
    size_t invalid_hash = 0;
    size_t valid = 4;
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_emptyBlockGenTime);
    PrepareReq prepare_req;
    FakeValidNodeNum(fake_pbft, valid);
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash, valid, 2);
    /// exception case: invalid view
    fake_pbft.consensus()->setView(prepare_req.view + 1);
    CheckBlockChain(fake_pbft, false);
    /// exception case2: invalid height
    fake_pbft.consensus()->setView(prepare_req.view);
    fake_pbft.consensus()->mutableHighest().setNumber(prepare_req.height);
    CheckBlockChain(fake_pbft, false);
    /// normal case: test checkAndSave(import success)
    fake_pbft.consensus()->mutableHighest().setNumber(prepare_req.height - 1);
    CheckBlockChain(fake_pbft, true);
}
/// test checkAndCommit
BOOST_AUTO_TEST_CASE(testCheckAndCommit)
{
    /// fake collected commitReq and signReq
    BlockHeader highest;
    size_t invalid_height = 2;
    size_t invalid_hash = 0;
    size_t valid = 4;
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * fake_pbft.consensus()->timeManager().m_emptyBlockGenTime);
    int64_t block_number = obtainBlockNumber(fake_pbft);
    PrepareReq prepare_req;

    KeyPair peer_keyPair = KeyPair::create();
    fake_pbft.m_sealerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer_keyPair.pub());

    FakeValidNodeNum(fake_pbft, valid);
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash,
        fake_pbft.consensus()->minValidNodes(), 0);

    /// case1: invalid view, return directly(import failed)
    fake_pbft.consensus()->setView(prepare_req.view + 1);
    fake_pbft.consensus()->checkAndCommit();
    /// check submit failed
    CheckBlockChain(fake_pbft, block_number);
    /// check update commitedPrepareCache failed

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() !=
                fake_pbft.consensus()->reqCache()->committedPrepareCache());

    /// check backupMsg failed
    bytes data = bytes();
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);

    //// check no broadcasft
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), CommitReqPacket, prepare_req.uniqueKey()) == false);

    /// case2: valid view
    fake_pbft.consensus()->setView(prepare_req.view);
    fake_pbft.consensus()->checkAndCommit();
    /// check backupMsg succ
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);
    /// submit failed for collected commitReq is not enough
    CheckBlockChain(fake_pbft, block_number);

    /// case3: enough commitReq && SignReq
    fake_pbft.consensus()->reqCache()->clearAll();
    FakeSignAndCommitCache(fake_pbft, prepare_req, highest, invalid_height, invalid_hash,
        fake_pbft.consensus()->minValidNodes(), 2);
    fake_pbft.consensus()->checkAndCommit();
    /// check backupMsg succ
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);
    /// check submit block scc
    CheckBlockChain(fake_pbft, block_number + 1);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    /// check reportBlock
    /// checkReportBlock(fake_pbft, highest, false);
    /// BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastSignTime <= utcTime());
}
/// test isValidPrepare
BOOST_AUTO_TEST_CASE(testIsValidPrepare)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    PrepareReq req;
    TestIsValidPrepare(fake_pbft, req, true);
    TestIsValidPrepare(fake_pbft, req, false);
}
/// test handlePrepareReq
BOOST_AUTO_TEST_CASE(testHandlePrepareReq)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    fake_pbft.consensus()->initPBFTEnv(
        3 * (fake_pbft.consensus()->timeManager().m_emptyBlockGenTime));
    PrepareReq req;
    TestIsValidPrepare(fake_pbft, req, true);
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_sealerList[i]);
    }
    /// case1: without omit empty block, re-generate prepareReq and broadcast
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->setOmitEmpty(false);
    fake_pbft.consensus()->handlePrepareMsg(req);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->prepareCache().block_hash == req.block_hash);
    /// check broadcastSign
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        compareAsyncSendTime(fake_pbft, fake_pbft.m_sealerList[i], 1);
    }
    /// check checkAndCommit(without enough sign and commit requests)
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() !=
                fake_pbft.consensus()->reqCache()->committedPrepareCache());

    /// case2: with enough sign and commit: submit block
    fake_pbft.consensus()->reqCache()->clearAll();
    BlockHeader highest;
    FakeSignAndCommitCache(
        fake_pbft, req, highest, 0, 0, fake_pbft.consensus()->minValidNodes() - 1, 2, false, false);
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handlePrepareMsg(req);

    /// BOOST_CHECK(fake_pbft.consensus()->reqCache()->prepareCache().block_hash == h256());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->rawPrepareCache() ==
                fake_pbft.consensus()->reqCache()->committedPrepareCache());
    bytes data;
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);
    /// submit failed for collected commitReq is not enough
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testIsValidSignReq)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    SignReq signReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidSignReq(fake_pbft, pbftMsg, signReq, prepareReq, peer_keyPair, false);
}

/// test handleSignMsg
BOOST_AUTO_TEST_CASE(testHandleSignMsg)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);

    fake_pbft.consensus()->initPBFTEnv(
        fake_pbft.consensus()->timeManager().m_emptyBlockGenTime * 3);

    PBFTMsgPacket pbftMsg;
    SignReq signReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidSignReq(fake_pbft, pbftMsg, signReq, prepareReq, peer_keyPair, true);
    /// test handleSignMsg
    /// case1: without enough signReq and commitReq, only addSign to the cache
    SignReq signReq2;
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);
    BOOST_CHECK(signReq2 == signReq);
    /// check the signReq has been added to the cache
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(signReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case2： with enough SignReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    BlockHeader highest;
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes() - 1, 0, false, false);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->committedPrepareCache() ==
                fake_pbft.consensus()->reqCache()->rawPrepareCache());
    /// check backupMsg
    bytes data;
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);
    CheckBlockChain(fake_pbft, block_number);

    /// case3: with enough SignReq and CommitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes() - 1, 2, false, false);
    fake_pbft.consensus()->handleSignMsg(signReq2, pbftMsg);

    BOOST_CHECK(fake_pbft.consensus()->reqCache()->committedPrepareCache() ==
                fake_pbft.consensus()->reqCache()->rawPrepareCache());
    /// check backupMsg
    fake_pbft.consensus()->reqCache()->committedPrepareCache().encode(data);
    checkBackupMsg(fake_pbft, FakePBFTEngine::backupKeyCommitted(), data);
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testIsCommitReqValid)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    CommitReq commitReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidCommitReq(fake_pbft, pbftMsg, commitReq, prepareReq, peer_keyPair, false);
}

/// test handleCommitMsg
BOOST_AUTO_TEST_CASE(testHandleCommitMsg)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    PBFTMsgPacket pbftMsg;
    CommitReq commitReq;
    PrepareReq prepareReq;
    KeyPair peer_keyPair = KeyPair::create();
    TestIsValidCommitReq(fake_pbft, pbftMsg, commitReq, prepareReq, peer_keyPair, true);
    /// case1: without enough signReq and commitReq
    CommitReq commitReq2;
    int64_t block_number = obtainBlockNumber(fake_pbft);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    BOOST_CHECK(commitReq2 == commitReq);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistCommit(commitReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case2: with enough signReq but not commitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    BlockHeader highest;
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes(), 0, false, false);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistCommit(commitReq2));
    CheckBlockChain(fake_pbft, block_number);

    /// case3 : with enough signReq and commitReq
    fake_pbft.consensus()->reqCache()->clearAll();
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    FakeSignAndCommitCache(fake_pbft, prepareReq, highest, 0, 0,
        fake_pbft.consensus()->minValidNodes(), 2, false, false);
    fake_pbft.consensus()->handleCommitMsg(commitReq2, pbftMsg);
    CheckBlockChain(fake_pbft, block_number + 1);
}

BOOST_AUTO_TEST_CASE(testShouldSeal)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    /// case 1: m_cfgErr is true
    fake_pbft.consensus()->resetConfig();
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);

    /// case 2: the node is a sealer, but getLeader failed for highest is a invalid block
    FakePBFTSealer(fake_pbft);
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);

    /// case 3: normal case
    PrepareReq prepareReq;
    TestIsValidPrepare(fake_pbft, prepareReq, true);

    fake_pbft.consensus()->setNodeIdx(fake_pbft.consensus()->getLeader().second);
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == true);
    /// update the committedPrepareCache, callback reHandleCommitPrepareCache
    fake_pbft.consensus()->setConsensusBlockNumber(prepareReq.height);

    fake_pbft.consensus()->reqCache()->addRawPrepare(prepareReq);
    fake_pbft.consensus()->reqCache()->updateCommittedPrepare();
    /// update the rawPrepareReq
    PrepareReq new_req;
    new_req.height = fake_pbft.consensus()->consensusBlockNumber() + 1;
    fake_pbft.consensus()->reqCache()->addRawPrepare(new_req);

    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_sealerList[i]);
    }
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);
    testReHandleCommitPrepareCache(fake_pbft, prepareReq);

    ///====== test notifySealing ======
    FakeBlock block(12, KeyPair::create().secret(), fake_pbft.consensus()->consensusBlockNumber());
    fake_pbft.consensus()->onNotifyNextLeaderReset();
    /// the leader calls notifySealing
    fake_pbft.consensus()->setNodeIdx(fake_pbft.consensus()->getLeader().second);
    BOOST_CHECK(fake_pbft.consensus()->notifyNextLeaderSeal() == false);
    fake_pbft.consensus()->notifySealing(*block.m_block);
    BOOST_CHECK(fake_pbft.consensus()->notifyNextLeaderSeal() == false);

    /// the nextLeader calls notifySealing
    fake_pbft.consensus()->setNodeIdx(fake_pbft.consensus()->getNextLeader());
    fake_pbft.consensus()->notifySealing(*block.m_block);
    BOOST_CHECK(fake_pbft.consensus()->notifyNextLeaderSeal() == true);
    ///====== test notifySealing end ======

    /// case 4: the node is not the leader
    fake_pbft.consensus()->setNodeIdx(
        (fake_pbft.consensus()->getLeader().second + 1) % fake_pbft.consensus()->nodeNum());
    FakeService* fake_service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    fake_service->setConnected();
    BOOST_CHECK(fake_pbft.consensus()->shouldSeal() == false);
}

BOOST_AUTO_TEST_CASE(testCollectGarbage)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    BlockHeader highest;
    PrepareReq prepareReq;
    FakeSignAndCommitCache(
        fake_pbft, prepareReq, highest, 0, 0, fake_pbft.consensus()->minValidNodes(), 2);
    fake_pbft.consensus()->mutableTimeManager().m_lastGarbageCollection =
        std::chrono::steady_clock::now();
    /// can't trigger collectGarbage
    fake_pbft.consensus()->collectGarbage();
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getSigCacheSize(prepareReq.block_hash) ==
                fake_pbft.consensus()->minValidNodes());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getCommitCacheSize(prepareReq.block_hash) ==
                fake_pbft.consensus()->minValidNodes());

    /// can trigger collectGarbage
    fake_pbft.consensus()->mutableTimeManager().m_lastGarbageCollection =
        std::chrono::steady_clock::now() -
        std::chrono::seconds(fake_pbft.consensus()->timeManager().CollectInterval + 10);
    fake_pbft.consensus()->collectGarbage();
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getSigCacheSize(highest.hash()) == 0);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->getCommitCacheSize(highest.hash()) == 0);
}
/// test handle future block
BOOST_AUTO_TEST_CASE(testHandleFutureBlock)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
    PrepareReq prepareReq;
    TestIsValidPrepare(fake_pbft, prepareReq, true);
    prepareReq.height = fake_pbft.consensus()->consensusBlockNumber();
    prepareReq.view = fake_pbft.consensus()->view();
    fake_pbft.consensus()->reqCache()->addFuturePrepareCache(prepareReq);
    BOOST_CHECK(
        fake_pbft.consensus()->reqCache()->futurePrepareCache(prepareReq.height)->block_hash !=
        h256());
    fake_pbft.consensus()->handleFutureBlock();
    /// check the functurePrepareCache has been cleared
    BOOST_CHECK(
        fake_pbft.consensus()->reqCache()->futurePrepareCache(prepareReq.height) == nullptr);
}

/// test handleViewChangeMsg
BOOST_AUTO_TEST_CASE(testHandleViewchangeMsg)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(4, ProtocolID::PBFT);
    fake_pbft.consensus()->resetConfig();
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_sealerList[i]);
    }
    ViewChangeReq viewchange_req;
    PBFTMsgPacket viewchange_packet;
    /// construct viewchange triggered by the 0th sealer
    TestIsValidViewChange(fake_pbft, viewchange_req);
    IDXTYPE nodeIdxSource = 2;
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);

    /// test handleViewChangeReq
    ViewChangeReq viewchange_req_decoded;
    fake_pbft.consensus()->handleViewChangeMsg(viewchange_req_decoded, viewchange_packet);
    BOOST_CHECK(viewchange_req_decoded == viewchange_req);
    /// viewchange no consensused
    BOOST_CHECK(fake_pbft.consensus()->view() != viewchange_req_decoded.view);

    /// fake viewchange req generated by the 1st sealer
    fakeValidViewchange(fake_pbft, viewchange_req, 1);
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);
    fake_pbft.consensus()->handleViewChangeMsg(viewchange_req_decoded, viewchange_packet);
    BOOST_CHECK(viewchange_req_decoded == viewchange_req);
    /// viewchange no consensused
    BOOST_CHECK(fake_pbft.consensus()->view() == viewchange_req_decoded.view);
}

BOOST_AUTO_TEST_CASE(testFastViewChange)
{
    /// test fast view change
    FakeConsensus<FakePBFTEngine> fake_pbft(4, ProtocolID::PBFT);
    fake_pbft.consensus()->resetConfig();
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_sealerList[i]);
    }
    ViewChangeReq viewchange_req;
    PBFTMsgPacket viewchange_packet;
    IDXTYPE nodeIdxSource = 1;
    /// handle the viewchange request received from 2nd sealer
    fakeValidViewchange(fake_pbft, viewchange_req, 0, true);
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);
    fake_pbft.consensus()->handleViewChangeMsg(viewchange_req, viewchange_packet);
    BOOST_CHECK(fake_pbft.consensus()->toView() != viewchange_req.view - 1);

    nodeIdxSource = 2;
    /// handle the viewchange request received from 1st sealer
    fakeValidViewchange(fake_pbft, viewchange_req, 1, true);
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);
    fake_pbft.consensus()->handleViewChangeMsg(viewchange_req, viewchange_packet);
    BOOST_CHECK(fake_pbft.consensus()->toView() == viewchange_req.view - 1);

    /// the node trigger fast viewchange, broadcast ViewChangeReq and changeView
    fake_pbft.consensus()->checkTimeout();
    BOOST_CHECK(fake_pbft.consensus()->view() == viewchange_req.view);
}

/// test checkBlock
BOOST_AUTO_TEST_CASE(testCheckBlock)
{
    FakeConsensus<FakePBFTEngine> fake_pbft(13, ProtocolID::PBFT);
    fake_pbft.consensus()->resetConfig();
    /// ignore the genesis block
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(
                    *fake_pbft.consensus()->blockChain()->getBlockByNumber(0)) == true);

    ///  check sealerList
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(
                    *fake_pbft.consensus()->blockChain()->getBlockByNumber(1)) == false);

    /// fake sealerList: check sealerList && sealer passed && sign
    FakeBlock block(12, KeyPair::create().secret(), 1);
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(*block.m_block) == false);
    fake_pbft.consensus()->setSealerList(block.m_block->blockHeader().sealerList());
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(*block.m_block) == true);

    /// block with too-many transactions
    fake_pbft.consensus()->setMaxBlockTransactions(11);
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(*block.m_block) == false);

    /// block with not-enough sealer
    FakeBlock invalid_block(7, KeyPair::create().secret(), 1);
    BOOST_CHECK(fake_pbft.consensus()->checkBlock(*invalid_block.m_block) == false);
}

/// test handleMsg
BOOST_AUTO_TEST_CASE(testHandleMsg)
{
    /// test fast view change
    FakeConsensus<FakePBFTEngine> fake_pbft(4, ProtocolID::PBFT);
    fake_pbft.consensus()->resetConfig();
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        appendSessionInfo(fake_pbft, fake_pbft.m_sealerList[i]);
    }
    /// handleViewChangeMsg
    ViewChangeReq viewchange_req;
    PBFTMsgPacket viewchange_packet;
    IDXTYPE nodeIdxSource = 1;
    /// handle the viewchange request received from 2nd sealer
    fakeValidViewchange(fake_pbft, viewchange_req, 0, true);
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);

    viewchange_packet.ttl = 1;
    /// no broadcast for ttl
    fake_pbft.consensus()->handleMsg(viewchange_packet);
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        compareAsyncSendTime(fake_pbft, fake_pbft.m_sealerList[i], 0);
    }

    viewchange_packet.ttl = 2;
    /// no broadcast for invalid viewchange request
    fake_pbft.consensus()->handleMsg(viewchange_packet);
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        compareAsyncSendTime(fake_pbft, fake_pbft.m_sealerList[i], 0);
    }

    /// broadcast
    fakeValidViewchange(fake_pbft, viewchange_req, 2, true);
    FakePBFTMsgPacket(viewchange_packet, viewchange_req, ViewChangeReqPacket, nodeIdxSource,
        fake_pbft.m_sealerList[nodeIdxSource]);
    fake_pbft.consensus()->handleMsg(viewchange_packet);
    for (size_t i = 0; i < fake_pbft.m_sealerList.size(); i++)
    {
        if (fake_pbft.m_sealerList[i] != viewchange_packet.node_id && i != viewchange_req.idx)
        {
            compareAsyncSendTime(fake_pbft, fake_pbft.m_sealerList[i], 1);
        }
        /// don't broadcast to the source node
        else
        {
            compareAsyncSendTime(fake_pbft, fake_pbft.m_sealerList[i], 0);
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
