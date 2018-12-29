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
 * @brief: unit test for libconsensus/pbft/PBFTEngine.*
 * @file: PBFTEngine.h
 * @author: yujiechen
 * @date: 2018-10-12
 */
#pragma once
#include "FakePBFTEngine.h"
#include "PBFTReqCache.h"
#include <libconsensus/pbft/PBFTEngine.h>
#include <libethcore/Protocol.h>
#include <test/unittests/libsync/FakeSyncToolsSet.h>
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
static void FakePBFTMinerByKeyPair(
    FakeConsensus<FakePBFTEngine>& fake_pbft, KeyPair const& key_pair)
{
    fake_pbft.m_minerList.push_back(key_pair.pub());
    fake_pbft.m_secrets.push_back(key_pair.secret());
    fake_pbft.consensus()->appendMiner(key_pair.pub());
    fake_pbft.resetSessionInfo();
}

/// update the miner list of PBFT Consensus
static void FakePBFTMiner(FakeConsensus<FakePBFTEngine>& fake_pbft)
{
    FakePBFTMinerByKeyPair(fake_pbft, fake_pbft.consensus()->keyPair());
}


static void compareAsyncSendTime(
    FakeConsensus<FakePBFTEngine>& fake_pbft, dev::h512 const& nodeID, size_t asyncSendTime)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    BOOST_CHECK(service->getAsyncSendSizeByNodeID(nodeID) == asyncSendTime);
}

/// Fake sessionInfosByProtocolID
static void appendSessionInfo(FakeConsensus<FakePBFTEngine>& fake_pbft, Public const& node_id)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    P2PSessionInfo info(node_id, m_endpoint, std::set<std::string>());
    size_t origin_size =
        service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size();
    service->appendSessionInfo(info);
    BOOST_CHECK(service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size() ==
                (origin_size + 1));
}
/// fake session according to node id of the peer
static std::shared_ptr<FakeSession> FakeSessionFunc(Public node_id)
{
    std::shared_ptr<FakeSession> session = std::make_shared<FakeSession>(node_id);
    return session;
}

/// fake message
template <typename T>
P2PMessage::Ptr FakeReqMessage(std::shared_ptr<FakePBFTEngine> pbft, T const& req,
    PACKET_TYPE const& packetType, PROTOCOL_ID const& protocolId, unsigned const& ttl = 0)
{
    bytes data;
    req.encode(data);
    return pbft->transDataToMessage(ref(data), packetType, protocolId, ttl);
}

/// check the data received from the network
template <typename T>
void CheckOnRecvPBFTMessage(std::shared_ptr<FakePBFTEngine> pbft,
    std::shared_ptr<P2PSession> session, T const& req, PACKET_TYPE const& packetType,
    bool const& valid = false)
{
    P2PMessage::Ptr message_ptr = FakeReqMessage(pbft, req, packetType, ProtocolID::PBFT);
    pbft->onRecvPBFTMessage(NetworkException(), session, message_ptr);
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

static void FakeSignAndCommitCache(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& prepareReq,
    BlockHeader& highest, size_t invalidHeightNum, size_t invalidHash, size_t validNum, int type,
    bool shouldFake = true, bool shouldAdd = true)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    highest = p_blockChain->getBlockByNumber(p_blockChain->number())->header();
    fake_pbft.consensus()->setHighest(highest);
    /// fake prepare req
    KeyPair key_pair;
    if (shouldFake)
    {
        prepareReq = FakePrepareReq(key_pair);
        Block block;
        fake_pbft.consensus()->resetBlock(block);
        block.encode(prepareReq.block);  /// encode block
        prepareReq.block_hash = block.header().hash();
        prepareReq.height = block.header().number();
        prepareReq.pBlock = std::make_shared<dev::eth::Block>(std::move(block));
    }
    fake_pbft.consensus()->mutableConsensusNumber() = prepareReq.height;
    if (shouldAdd)
    {
        fake_pbft.consensus()->reqCache()->addRawPrepare(prepareReq);
        fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    }

    h256 invalid_hash = sha3("invalid" + toString(utcTime()));

    /// fake SignReq
    if (type == 0 || type == 2)
    {
        FakeInvalidReq<SignReq>(prepareReq, *(fake_pbft.consensus()->reqCache().get()),
            fake_pbft.consensus()->reqCache()->mutableSignCache(), highest, invalid_hash,
            invalidHeightNum, invalidHash, validNum, false);
        fake_pbft.consensus()->reqCache()->collectGarbage(highest);
        BOOST_CHECK(
            fake_pbft.consensus()->reqCache()->getSigCacheSize(prepareReq.block_hash) == validNum);
    }
    /// fake commitReq
    if (type == 1 || type == 2)
    {
        FakeInvalidReq<CommitReq>(prepareReq, *(fake_pbft.consensus()->reqCache().get()),
            fake_pbft.consensus()->reqCache()->mutableCommitCache(), highest, invalid_hash,
            invalidHeightNum, invalidHash, validNum, false);
        fake_pbft.consensus()->reqCache()->collectGarbage(highest);
        BOOST_CHECK(fake_pbft.consensus()->reqCache()->getCommitCacheSize(prepareReq.block_hash) ==
                    validNum);
    }
}


static void FakeValidNodeNum(FakeConsensus<FakePBFTEngine>& fake_pbft, IDXTYPE validNum)
{
    IDXTYPE node_num = (validNum * 3) / 2 + 1;
    IDXTYPE f_value = validNum / 2 + 1;
    fake_pbft.consensus()->setNodeNum(node_num);
    fake_pbft.consensus()->setF(f_value);
}

/// check blockchain
static void CheckBlockChain(FakeConsensus<FakePBFTEngine>& fake_pbft, bool succ)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    int64_t block_number = p_blockChain->m_blockNumber;

    fake_pbft.consensus()->checkAndSave();
    if (succ)
        block_number += 1;
    BOOST_CHECK(p_blockChain->m_blockNumber == block_number);
    BOOST_CHECK(p_blockChain->m_blockHash.size() == (uint64_t)block_number);
    BOOST_CHECK(p_blockChain->m_blockChain.size() == (uint64_t)block_number);
}

static int64_t obtainBlockNumber(FakeConsensus<FakePBFTEngine>& fake_pbft)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    return p_blockChain->m_blockNumber;
}

static void CheckBlockChain(FakeConsensus<FakePBFTEngine>& fake_pbft, int64_t blockNumber)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    BOOST_CHECK(p_blockChain->m_blockNumber == blockNumber);
    BOOST_CHECK(p_blockChain->m_blockHash.size() == (uint64_t)blockNumber);
    BOOST_CHECK(p_blockChain->m_blockChain.size() == (uint64_t)blockNumber);
}

static inline void checkResetConfig(FakeConsensus<FakePBFTEngine>& fake_pbft, bool isMiner)
{
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == fake_pbft.consensus()->minerList().size());
    if (isMiner)
    {
        BOOST_CHECK(fake_pbft.consensus()->nodeIdx() != MAXIDX);
        BOOST_CHECK(fake_pbft.consensus()->cfgErr() == false);
    }
    else
    {
        BOOST_CHECK(fake_pbft.consensus()->nodeIdx() == MAXIDX);
        BOOST_CHECK(fake_pbft.consensus()->cfgErr() == true);
    }
    /// check m_f
    BOOST_CHECK(fake_pbft.consensus()->fValue() == (fake_pbft.consensus()->nodeNum() - 1) / 3);
}

static inline void checkClearAllExceptCommitCache(FakeConsensus<FakePBFTEngine>& fake_pbft)
{
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->prepareCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->mutableSignCache().size() == 0);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->mutableViewChangeCache().size() == 0);
}

static inline void checkDelCommitCache(
    FakeConsensus<FakePBFTEngine>& fake_pbft, BlockHeader const& header)
{
    auto p_commit = fake_pbft.consensus()->reqCache()->mutableCommitCache().find(header.hash());
    BOOST_CHECK(p_commit == fake_pbft.consensus()->reqCache()->mutableCommitCache().end());
}

static void checkReportBlock(
    FakeConsensus<FakePBFTEngine>& fake_pbft, BlockHeader const& highest, bool isMiner = false)
{
    BOOST_CHECK(fake_pbft.consensus()->mutableConsensusNumber() ==
                fake_pbft.consensus()->mutableHighest().number() + 1);
    BOOST_CHECK(fake_pbft.consensus()->leaderFailed() == false);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastConsensusTime > utcTime() - 100000);
    BOOST_CHECK(fake_pbft.consensus()->timeManager().m_lastConsensusTime <= utcTime());
    /// check resetConfig
    checkResetConfig(fake_pbft, isMiner);
    /// check clearAllExceptCommitCache
    checkClearAllExceptCommitCache(fake_pbft);
    /// check delCommitCache
    checkDelCommitCache(fake_pbft, highest);
}

static void checkBackupMsg(FakeConsensus<FakePBFTEngine>& fake_pbft, std::string const& key,
    bytes const& msgData, bool shouldClean = true)
{
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    /// insert succ
    std::string data = fake_pbft.consensus()->backupDB()->lookup(key);
    if (msgData.size() == 0)
        BOOST_CHECK(data.empty() == true);
    else
    {
        BOOST_CHECK(data == toHex(msgData));
        /// remove the key
        std::string empty = "";
        if (shouldClean)
            fake_pbft.consensus()->backupDB()->insert(key, empty);
    }
}

template <typename T>
static void checkBroadcastSpecifiedMsg(
    FakeConsensus<FakePBFTEngine>& fake_pbft, T& tmp_req, unsigned packetType)
{
    KeyPair key_pair;
    KeyPair peer_keyPair = KeyPair::create();
    /// fake prepare_req
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    /// obtain sig of SignReq
    tmp_req = T(prepare_req, fake_pbft.consensus()->keyPair(), fake_pbft.consensus()->nodeIdx());
    std::string key = tmp_req.uniqueKey();
    /// append session info
    appendSessionInfo(fake_pbft, peer_keyPair.pub());
    /// case1: the peer node is not miner
    if (packetType == SignReqPacket)
    {
        fake_pbft.consensus()->broadcastSignReq(prepare_req);
        /// check broadcast cache
        BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                        peer_keyPair.pub(), SignReqPacket, key) == false);
    }
    if (packetType == CommitReqPacket)
    {
        fake_pbft.consensus()->broadcastCommitReq(prepare_req);
        BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                        peer_keyPair.pub(), CommitReqPacket, key) == false);
    }
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 0);

    /// case2: the the peer node is a miner
    fake_pbft.m_minerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->appendMiner(peer_keyPair.pub());
    FakePBFTMiner(fake_pbft);

    T req(prepare_req, fake_pbft.consensus()->keyPair(), fake_pbft.consensus()->nodeIdx());
    key = req.uniqueKey();
    bytes data;
    req.encode(data);
    fake_pbft.consensus()->broadcastMsg(SignReqPacket, key, ref(data));
    BOOST_CHECK(
        fake_pbft.consensus()->broadcastFilter(peer_keyPair.pub(), SignReqPacket, key) == true);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
}

/// check isExistPrepare
static void testIsExistPrepare(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        fake_pbft.consensus()->reqCache()->addRawPrepare(req);
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        fake_pbft.consensus()->reqCache()->clearAllExceptCommitCache();
    }
}
static void testIsConsensused(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        req.view = fake_pbft.consensus()->view() - 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        req.height = org_height;
    }
}

static void testIsFuture(
    FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ, bool shouldFix = true)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        VIEWTYPE org_view = req.view;
        req.height = fake_pbft.consensus()->mutableConsensusNumber();
        req.view = fake_pbft.consensus()->view() + 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        if (shouldFix)
        {
            req.height = org_height;
            req.view = org_view;
        }
    }
}

static void testIsValidLeader(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        fake_pbft.consensus()->mutableLeaderFailed() = true;
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        fake_pbft.consensus()->mutableLeaderFailed() = false;
    }
}

static void testIsHashSavedAfterCommit(
    FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        h256 org_hash = req.block_hash;
        req.height = fake_pbft.consensus()->reqCache()->committedPrepareCache().height;
        req.block_hash = sha3("invalid");
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        req.height = org_height;
        req.block_hash = org_hash;
    }
}

static void testCheckSign(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        Signature org_sig2 = req.sig2;
        req.sig2 = Signature();
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == false);
        req.sig2 = org_sig2;
    }
}

static void fakeValidPrepare(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req)
{
    /// init the env
    FakePBFTMiner(fake_pbft);
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    BlockHeader highest = p_blockChain->getBlockByNumber(p_blockChain->number())->header();
    /// fake_pbft.consensus()->resetConfig();
    fake_pbft.consensus()->setHighest(highest);
    fake_pbft.consensus()->setView(2);
    req.idx = fake_pbft.consensus()->getLeader().second;
    req.height = fake_pbft.consensus()->mutableConsensusNumber();
    req.view = fake_pbft.consensus()->view();
    Block block;
    fake_pbft.consensus()->resetBlock(block);
    block.header().setSealerList(fake_pbft.consensus()->minerList());
    block.header().setSealer(u256(req.idx));
    block.encode(req.block);
    block.decode(ref(req.block));
    req.block_hash = block.header().hash();
    req.height = block.header().number();
    fake_pbft.consensus()->mutableConsensusNumber() = req.height;
    BOOST_CHECK(fake_pbft.m_secrets.size() > req.idx);
    Secret sec = fake_pbft.m_secrets[req.idx];
    req.sig = dev::sign(sec, req.block_hash);
    req.sig2 = dev::sign(sec, req.fieldsWithoutBlock());
}

/// test isValidPrepare
static void TestIsValidPrepare(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq& req, bool succ)
{
    KeyPair key_pair;
    req = FakePrepareReq(key_pair);
    /// normal case: fake a valid prepare
    fakeValidPrepare(fake_pbft, req);
    BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == true);
    /// exception case: prepareReq has already been cached
    testIsExistPrepare(fake_pbft, req, succ);
    /// exception case: allowSelf is false && the prepare generated from the node-self
    /// exception case: hasConsensused
    testIsConsensused(fake_pbft, req, succ);
    /// exception case: isFutureBlock
    testIsFuture(fake_pbft, req, succ);
    /// exception case: isValidLeader
    testIsValidLeader(fake_pbft, req, succ);
    /// exception case: isHashSavedAfterCommit
    testIsHashSavedAfterCommit(fake_pbft, req, succ);
    /// exception case: checkSign failed
    testCheckSign(fake_pbft, req, succ);
}

/// obtain the PBFTMsgPacket according to given req
template <typename T>
void FakePBFTMsgPacket(
    PBFTMsgPacket& packet, T& req, unsigned const& type, IDXTYPE const& idx, h512 const& nodeId)
{
    req.encode(packet.data);
    packet.packet_id = type;
    packet.setOtherField(idx, nodeId, "");
}

/// fake valid signReq or commitReq
template <typename T>
void FakeValidSignorCommitReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PBFTMsgPacket& packet,
    T& req, PrepareReq& prepareReq, KeyPair const& peer_keyPair)
{
    FakePBFTMiner(fake_pbft);
    FakePBFTMinerByKeyPair(fake_pbft, peer_keyPair);
    KeyPair key_pair;
    prepareReq = FakePrepareReq(key_pair);
    fakeValidPrepare(fake_pbft, prepareReq);
    IDXTYPE node_id = (fake_pbft.consensus()->nodeIdx() + 1) % fake_pbft.consensus()->nodeNum();
    KeyPair tmp_key_pair(fake_pbft.m_secrets[node_id]);
    req = T(prepareReq, tmp_key_pair, node_id);
    /// add prepareReq
    fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    /// reset current consensusNumber and View
    fake_pbft.consensus()->mutableConsensusNumber() = prepareReq.height;
    fake_pbft.consensus()->setView(prepareReq.view);
    FakePBFTMsgPacket(
        packet, req, SignReqPacket, fake_pbft.m_minerList.size() - 1, peer_keyPair.pub());
}

/// test isExistSign
static void testIsExistSign(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq const& prepareReq,
    SignReq& signReq, bool succ)
{
    if (!succ)
    {
        fake_pbft.consensus()->reqCache()->addSignReq(signReq);
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == false);
        fake_pbft.consensus()->reqCache()->clearAll();
        fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    }
}

static void testIsExistCommit(FakeConsensus<FakePBFTEngine>& fake_pbft,
    PrepareReq const& prepareReq, CommitReq const& commitReq, bool succ)
{
    if (!succ)
    {
        fake_pbft.consensus()->reqCache()->addCommitReq(commitReq);
        BOOST_CHECK(fake_pbft.consensus()->isValidCommitReq(commitReq) == false);
        fake_pbft.consensus()->reqCache()->clearAll();
        fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
    }
}

/// test checkReq
static void testCheckReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq const& prepareReq,
    SignReq& signReq, bool succ)
{
    if (!succ)
    {
        /// test inconsistent with the prepareCache
        fake_pbft.consensus()->reqCache()->clearAll();
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == false);
        /// test is the future block
        SignReq copiedReq = signReq;
        copiedReq.height = fake_pbft.consensus()->mutableConsensusNumber() + 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(copiedReq) == false);
        BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(copiedReq) == false);
        /// modify the signature
        copiedReq.sig = dev::sign(fake_pbft.m_secrets[copiedReq.idx], copiedReq.block_hash);
        copiedReq.sig2 =
            dev::sign(fake_pbft.m_secrets[copiedReq.idx], copiedReq.fieldsWithoutBlock());
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(copiedReq) == false);
        BOOST_CHECK(fake_pbft.consensus()->reqCache()->isExistSign(copiedReq) == true);

        fake_pbft.consensus()->reqCache()->clearAll();
        fake_pbft.consensus()->reqCache()->addPrepareReq(prepareReq);
        /// test signReq is generated by the node-self
        IDXTYPE org_idx = signReq.idx;
        signReq.idx = fake_pbft.consensus()->nodeIdx();
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == false);
        signReq.idx = org_idx;
        /// test invalid view
        VIEWTYPE org_view = signReq.view;
        signReq.view += 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == false);
        signReq.view = org_view;
        /// test invalid sign
        Signature sig = signReq.sig;
        signReq.sig = Signature();
        BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == false);
        signReq.sig = sig;
    }
}

/// test isValidSignReq
static void TestIsValidSignReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PBFTMsgPacket& packet,
    SignReq& signReq, PrepareReq& prepareReq, KeyPair const& peer_keyPair, bool succ)
{
    FakeValidSignorCommitReq(fake_pbft, packet, signReq, prepareReq, peer_keyPair);
    BOOST_CHECK(fake_pbft.consensus()->isValidSignReq(signReq) == true);
    testIsExistSign(fake_pbft, prepareReq, signReq, succ);
    testCheckReq(fake_pbft, prepareReq, signReq, succ);
}

static void TestIsValidCommitReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PBFTMsgPacket& packet,
    CommitReq& commitReq, PrepareReq& prepareReq, KeyPair const& peer_keyPair, bool succ)
{
    FakeValidSignorCommitReq(fake_pbft, packet, commitReq, prepareReq, peer_keyPair);
    BOOST_CHECK(fake_pbft.consensus()->isValidCommitReq(commitReq) == true);
    testIsExistCommit(fake_pbft, prepareReq, commitReq, succ);
}

static void testReHandleCommitPrepareCache(
    FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq const& req)
{
    /// check callback broadcastMsg
    for (size_t i = 0; i < fake_pbft.m_minerList.size(); i++)
    {
        BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                        fake_pbft.m_minerList[i], PrepareReqPacket, req.uniqueKey()) == false);
    }
}

}  // namespace test
}  // namespace dev
