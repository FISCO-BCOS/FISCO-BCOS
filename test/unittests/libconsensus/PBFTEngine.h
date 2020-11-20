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
#include "libdevcrypto/CryptoInterface.h"
#include <libconsensus/pbft/PBFTEngine.h>
#include <libethcore/Protocol.h>
#include <libutilities/TopicInfo.h>
#include <test/unittests/libsync/FakeSyncToolsSet.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::eth;
using namespace bcos::blockverifier;
using namespace bcos::txpool;
using namespace bcos::blockchain;

namespace bcos
{
namespace test
{
template <typename T>
static void FakePBFTSealerByKeyPair(FakeConsensus<T>& fake_pbft, KeyPair const& key_pair)
{
    fake_pbft.m_sealerList.push_back(key_pair.pub());
    fake_pbft.m_secrets.push_back(key_pair.secret());
    fake_pbft.m_keyPair.push_back(key_pair);
    fake_pbft.m_nodeID2KeyPair[key_pair.pub()] = key_pair;
    fake_pbft.consensus()->appendSealer(key_pair.pub());
    fake_pbft.resetSessionInfo();
}

/// update the sealer list of PBFT Consensus
template <typename T>
static void FakePBFTSealer(FakeConsensus<T>& fake_pbft)
{
    FakePBFTSealerByKeyPair(fake_pbft, fake_pbft.consensus()->keyPair());
}

template <typename T>
static void compareAsyncSendTime(
    FakeConsensus<T>& fake_pbft, bcos::h512 const& nodeID, size_t asyncSendTime)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    BOOST_CHECK(service->getAsyncSendSizeByNodeID(nodeID) == asyncSendTime);
}

template <typename T>
static void compareAndClearAsyncSendTime(
    FakeConsensus<T>& fake_pbft, bcos::h512 const& nodeID, size_t asyncSendTime)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    BOOST_CHECK(service->getAsyncSendSizeByNodeID(nodeID) == asyncSendTime);
    service->clearMessageByNodeID(nodeID);
}


/// Fake sessionInfosByProtocolID
template <typename T>
static void appendSessionInfo(FakeConsensus<T>& fake_pbft, Public const& node_id)
{
    FakeService* service =
        dynamic_cast<FakeService*>(fake_pbft.consensus()->mutableService().get());
    NodeIPEndpoint m_endpoint(boost::asio::ip::make_address("127.0.0.1"), 30303);

    bcos::network::NodeInfo node_info;
    node_info.nodeID = node_id;
    std::set<bcos::TopicItem> topicList;
    P2PSessionInfo info(node_info, m_endpoint, topicList);
    size_t origin_size =
        service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size();
    service->appendSessionInfo(info);
    BOOST_CHECK(service->sessionInfosByProtocolID(fake_pbft.consensus()->protocolId()).size() ==
                (origin_size + 1));
}
/// fake session according to node id of the peer
std::shared_ptr<FakeSession> FakeSessionFunc(Public node_id);

/// fake message
template <typename T, typename S>
P2PMessage::Ptr FakeReqMessage(std::shared_ptr<S> pbft, T const& req, PACKET_TYPE const& packetType,
    PROTOCOL_ID const&, unsigned const& ttl = 0)
{
    bytes data;
    req.encode(data);
    return pbft->transDataToMessageWrapper(ref(data), packetType, ttl);
}

/// check the data received from the network
template <typename T>
void CheckOnRecvPBFTMessage(std::shared_ptr<FakePBFTEngine> pbft,
    std::shared_ptr<P2PSession> session, T const& req, PACKET_TYPE const& packetType,
    bool const& valid = false)
{
    P2PMessage::Ptr message_ptr = FakeReqMessage(pbft, req, packetType, ProtocolID::PBFT);
    pbft->onRecvPBFTMessage(NetworkException(), session, message_ptr);
    std::pair<bool, PBFTMsgPacket::Ptr> ret = pbft->mutableMsgQueue().tryPop(unsigned(5));
    if (valid == true)
    {
        BOOST_CHECK(ret.first == true);
        BOOST_CHECK(ret.second->packet_id == packetType);
        T decoded_req;
        decoded_req.decode(ref(ret.second->data));
        BOOST_CHECK(decoded_req == req);
    }
    else
        BOOST_CHECK(ret.first == false);
}

template <typename T>
static void FakeSignAndCommitCache(FakeConsensus<T>& fake_pbft, PrepareReq::Ptr prepareReq,
    BlockHeader& highest, size_t invalidHeightNum, size_t invalidHash, size_t validNum, int type,
    bool shouldFake = true, bool shouldAdd = true)
{
    auto blockChain = fake_pbft.consensus()->blockChain();
    assert(blockChain);
    highest = blockChain->getBlockByNumber(blockChain->number())->header();
    fake_pbft.consensus()->setHighest(highest);
    /// fake prepare req
    KeyPair key_pair;
    if (shouldFake)
    {
        *prepareReq = FakePrepareReq(key_pair);
        Block block;
        fake_pbft.consensus()->resetBlock(block);
        block.encode(*prepareReq->block);  /// encode block
        prepareReq->block_hash = block.header().hash();
        prepareReq->height = block.header().number();
        prepareReq->pBlock = std::make_shared<bcos::eth::Block>(std::move(block));
    }
    fake_pbft.consensus()->setConsensusBlockNumber(prepareReq->height);
    if (shouldAdd)
    {
        PrepareReq::Ptr copiedPrepareReq = std::make_shared<PrepareReq>(*prepareReq);
        fake_pbft.consensus()->reqCache()->addRawPrepare(copiedPrepareReq);
        PrepareReq::Ptr signReq = std::make_shared<PrepareReq>(*prepareReq);
        fake_pbft.consensus()->reqCache()->addPrepareReq(signReq);
    }

    h256 invalid_hash = crypto::Hash("invalid" + toString(utcTime()));

    /// fake SignReq
    if (type == 0 || type == 2)
    {
        FakeInvalidReq<SignReq>(prepareReq, *(fake_pbft.consensus()->reqCache().get()),
            fake_pbft.consensus()->reqCache()->mutableSignCache(), highest, invalid_hash,
            invalidHeightNum, invalidHash, validNum, false);
        fake_pbft.consensus()->reqCache()->collectGarbage(highest);
        BOOST_CHECK(fake_pbft.consensus()->reqCache()->getSigCacheSize(
                        prepareReq->block_hash, validNum) == validNum);
    }
    /// fake commitReq
    if (type == 1 || type == 2)
    {
        FakeInvalidReq<CommitReq>(prepareReq, *(fake_pbft.consensus()->reqCache().get()),
            fake_pbft.consensus()->reqCache()->mutableCommitCache(), highest, invalid_hash,
            invalidHeightNum, invalidHash, validNum, false);
        fake_pbft.consensus()->reqCache()->collectGarbage(highest);
        BOOST_CHECK(fake_pbft.consensus()->reqCache()->getCommitCacheSize(
                        prepareReq->block_hash, validNum) == validNum);
    }
}

template <typename T>
static void FakeValidNodeNum(FakeConsensus<T>& fake_pbft, IDXTYPE validNum)
{
    IDXTYPE node_num = (validNum * 3) / 2 + 1;
    IDXTYPE f_value = validNum / 2 + 1;
    fake_pbft.consensus()->setNodeNum(node_num);
    fake_pbft.consensus()->setF(f_value);
}

/// check blockchain
template <typename T>
static void CheckBlockChain(FakeConsensus<T>& fake_pbft, bool succ)
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

template <typename T>
static int64_t obtainBlockNumber(FakeConsensus<T>& fake_pbft)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    assert(p_blockChain);
    return p_blockChain->m_blockNumber;
}

template <typename T>
static void CheckBlockChain(FakeConsensus<T>& fake_pbft, int64_t blockNumber)
{
    FakeBlockChain* p_blockChain =
        dynamic_cast<FakeBlockChain*>(fake_pbft.consensus()->blockChain().get());
    BOOST_CHECK(p_blockChain->m_blockNumber == blockNumber);
    BOOST_CHECK(p_blockChain->m_blockHash.size() == (uint64_t)blockNumber);
    BOOST_CHECK(p_blockChain->m_blockChain.size() == (uint64_t)blockNumber);
}

template <typename T>
static void checkResetConfig(FakeConsensus<T>& fake_pbft, bool isSealer)
{
    BOOST_CHECK(fake_pbft.consensus()->nodeNum() == fake_pbft.consensus()->sealerList().size());
    if (isSealer)
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

template <typename T>
static inline void checkClearAllExceptCommitCache(FakeConsensus<T>& fake_pbft)
{
    checkPBFTMsg(fake_pbft.consensus()->reqCache()->prepareCache());
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->mutableSignCache().size() == 0);
    BOOST_CHECK(fake_pbft.consensus()->reqCache()->mutableViewChangeCache().size() == 0);
}

template <typename T>
static inline void checkDelCommitCache(FakeConsensus<T>& fake_pbft, BlockHeader const& header)
{
    auto p_commit = fake_pbft.consensus()->reqCache()->mutableCommitCache().find(header.hash());
    BOOST_CHECK(p_commit == fake_pbft.consensus()->reqCache()->mutableCommitCache().end());
}

static inline std::string getDataFromBackupDB(
    std::string const& _key, bcos::storage::BasicRocksDB::Ptr _backupDB)
{
    std::string value;
    _backupDB->Get(rocksdb::ReadOptions(), _key, value);
    return value;
}

template <typename T>
static void checkBackupMsg(FakeConsensus<T>& fake_pbft, std::string const& key,
    bytes const& msgData, bool shouldClean = true)
{
    BOOST_CHECK(fake_pbft.consensus()->backupDB());
    /// insert succ
    std::string data = getDataFromBackupDB(key, fake_pbft.consensus()->backupDB());
    if (msgData.size() == 0)
        BOOST_CHECK(data.empty() == true);
    else
    {
        // wait the prepare commit to the backupDB
        while (data.empty())
        {
            data = getDataFromBackupDB(key, fake_pbft.consensus()->backupDB());
            sleep(1);
        }
        auto hexData = toHexString(msgData);
        if (data != *hexData)
        {
            std::cout << "error: PBFTBackup: Queried Data:" << data << std::endl;
            std::cout << "error: PBFTBackup: Expected Data:" << *hexData << std::endl;
        }
        BOOST_CHECK(data == *hexData);
        /// remove the key
        std::string empty = "";
        if (shouldClean)
        {
            rocksdb::WriteBatch batch;
            fake_pbft.consensus()->backupDB()->Put(batch, key, empty);
            rocksdb::WriteOptions options;
            fake_pbft.consensus()->backupDB()->Write(options, batch);
        }
    }
}

template <typename T, typename S>
static void checkBroadcastSpecifiedMsg(FakeConsensus<S>& fake_pbft, T& tmp_req, unsigned packetType)
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
    /// case1: the peer node is not sealer
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

    /// case2: the the peer node is a sealer
    fake_pbft.m_sealerList.push_back(peer_keyPair.pub());
    fake_pbft.consensus()->appendSealer(peer_keyPair.pub());
    FakePBFTSealer(fake_pbft);

    T req(prepare_req, fake_pbft.consensus()->keyPair(), fake_pbft.consensus()->nodeIdx());
    bytes data;
    req.encode(data);
    fake_pbft.consensus()->broadcastMsgWrapper(SignReqPacket, req, ref(data));
    BOOST_CHECK(fake_pbft.consensus()->broadcastFilter(
                    peer_keyPair.pub(), SignReqPacket, req.uniqueKey()) == true);
    compareAsyncSendTime(fake_pbft, peer_keyPair.pub(), 1);
}

/// check isExistPrepare
template <typename T>
static void testIsExistPrepare(FakeConsensus<T>& fake_pbft, PrepareReq::Ptr req, bool succ)
{
    if (!succ)
    {
        PrepareReq::Ptr copiedPrepareReq = std::make_shared<PrepareReq>(*req);
        fake_pbft.consensus()->reqCache()->addRawPrepare(copiedPrepareReq);
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(*req) == CheckResult::INVALID);
        fake_pbft.consensus()->reqCache()->clearAllExceptCommitCache();
    }
}

template <typename T>
static void testIsConsensused(FakeConsensus<T>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        req.view = fake_pbft.consensus()->view() - 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == CheckResult::INVALID);
        req.height = org_height;
    }
}

template <typename T>
static void testIsFuture(
    FakeConsensus<T>& fake_pbft, PrepareReq& req, bool succ, bool shouldFix = true)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        VIEWTYPE org_view = req.view;
        req.height = fake_pbft.consensus()->consensusBlockNumber();
        req.view = fake_pbft.consensus()->view() + 1;
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == CheckResult::INVALID);
        if (shouldFix)
        {
            req.height = org_height;
            req.view = org_view;
        }
    }
}
template <typename T>
static void testIsValidLeader(FakeConsensus<T>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        fake_pbft.consensus()->setLeaderFailed(true);
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == CheckResult::INVALID);
        fake_pbft.consensus()->setLeaderFailed(false);
    }
}

template <typename T>
static void testIsHashSavedAfterCommit(FakeConsensus<T>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        int64_t org_height = req.height;
        h256 org_hash = req.block_hash;
        req.height = fake_pbft.consensus()->reqCache()->committedPrepareCache().height;
        req.block_hash = crypto::Hash("invalid");
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == CheckResult::INVALID);
        req.height = org_height;
        req.block_hash = org_hash;
    }
}

template <typename T>
static void testCheckSign(FakeConsensus<T>& fake_pbft, PrepareReq& req, bool succ)
{
    if (!succ)
    {
        auto org_sig2 = req.sig2;
        req.sig2 = vector<unsigned char>();
        BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(req) == CheckResult::INVALID);
        req.sig2 = org_sig2;
    }
}

template <typename T>
static void fakeValidPrepare(FakeConsensus<T>& fake_pbft, PrepareReq& req)
{
    /// init the env
    FakePBFTSealer(fake_pbft);
    auto blockChain = fake_pbft.consensus()->blockChain();
    BlockHeader highest = blockChain->getBlockByNumber(blockChain->number())->header();
    /// fake_pbft.consensus()->resetConfig();
    fake_pbft.consensus()->setHighest(highest);
    fake_pbft.consensus()->setView(2);
    req.idx = fake_pbft.consensus()->getLeader().second;
    req.height = fake_pbft.consensus()->consensusBlockNumber();
    req.view = fake_pbft.consensus()->view();
    Block block;
    fake_pbft.consensus()->resetBlock(block);
    block.header().setSealerList(fake_pbft.consensus()->sealerList());
    block.header().setSealer(u256(req.idx));
    block.encode(*req.block);
    block.decode(ref(*req.block));
    req.block_hash = block.header().hash();
    req.height = block.header().number();
    req.pBlock = std::make_shared<bcos::eth::Block>(std::move(block));
    fake_pbft.consensus()->setConsensusBlockNumber(req.height);
    // get nodeID according to node index
    bcos::h512 nodeID;
    fake_pbft.consensus()->wrapperGetNodeIDByIndex(nodeID, req.idx);
    BOOST_CHECK((fake_pbft.m_nodeID2KeyPair).count(nodeID));
    auto keyPair = (fake_pbft.m_nodeID2KeyPair)[nodeID];
    req.sig = bcos::crypto::Sign(keyPair, req.block_hash)->asBytes();
    req.sig2 = bcos::crypto::Sign(keyPair, req.fieldsWithoutBlock())->asBytes();
}

/// test isValidPrepare
template <typename T>
static void TestIsValidPrepare(FakeConsensus<T>& fake_pbft, PrepareReq::Ptr req, bool succ)
{
    KeyPair key_pair;
    *req = FakePrepareReq(key_pair);
    /// normal case: fake a valid prepare
    fakeValidPrepare(fake_pbft, *req);
    BOOST_CHECK(fake_pbft.consensus()->isValidPrepare(*req) == CheckResult::VALID);
    /// exception case: prepareReq has already been cached
    testIsExistPrepare(fake_pbft, req, succ);
    /// exception case: allowSelf is false && the prepare generated from the node-self
    /// exception case: hasConsensused
    testIsConsensused(fake_pbft, *req, succ);
    /// exception case: isFutureBlock
    testIsFuture(fake_pbft, *req, succ);
    /// exception case: isValidLeader
    testIsValidLeader(fake_pbft, *req, succ);
    /// exception case: isHashSavedAfterCommit
    testIsHashSavedAfterCommit(fake_pbft, *req, succ);
    /// exception case: checkSign failed
    testCheckSign(fake_pbft, *req, succ);
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
    T& req, PrepareReq::Ptr prepareReq, KeyPair const& peer_keyPair)
{
    FakePBFTSealer(fake_pbft);
    FakePBFTSealerByKeyPair(fake_pbft, peer_keyPair);
    KeyPair key_pair;
    *prepareReq = FakePrepareReq(key_pair);
    fakeValidPrepare(fake_pbft, *prepareReq);
    IDXTYPE node_id = (fake_pbft.consensus()->nodeIdx() + 1) % fake_pbft.consensus()->nodeNum();
    KeyPair tmp_key_pair(fake_pbft.m_secrets[node_id]);
    req = T(*prepareReq, tmp_key_pair, node_id);
    /// add prepareReq
    PrepareReq::Ptr copiedPrepareReq = std::make_shared<PrepareReq>(*prepareReq);
    fake_pbft.consensus()->reqCache()->addPrepareReq(copiedPrepareReq);
    /// reset current consensusNumber and View
    fake_pbft.consensus()->setConsensusBlockNumber(prepareReq->height);
    fake_pbft.consensus()->setView(prepareReq->view);
    FakePBFTMsgPacket(
        packet, req, SignReqPacket, fake_pbft.m_sealerList.size() - 1, peer_keyPair.pub());
}

/// test isExistSign
void testIsExistSign(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq::Ptr prepareReq,
    SignReq::Ptr signReq, bool succ);

void testIsExistCommit(FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq::Ptr prepareReq,
    CommitReq::Ptr commitReq, bool succ);

/// test isValidSignReq
void TestIsValidSignReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PBFTMsgPacket& packet,
    SignReq::Ptr signReq, PrepareReq::Ptr prepareReq, KeyPair const& peer_keyPair, bool succ);

void TestIsValidCommitReq(FakeConsensus<FakePBFTEngine>& fake_pbft, PBFTMsgPacket& packet,
    CommitReq::Ptr commitReq, PrepareReq::Ptr prepareReq, KeyPair const& peer_keyPair, bool succ);

void testReHandleCommitPrepareCache(
    FakeConsensus<FakePBFTEngine>& fake_pbft, PrepareReq const& req);

/// fake a valid viewchange
void fakeValidViewchange(FakeConsensus<FakePBFTEngine>& fake_pbft, ViewChangeReq& req,
    IDXTYPE const& idx = 0, bool forFastViewChange = false, bool setToView = true);


/// test isValidViewChangeReq
void TestIsValidViewChange(FakeConsensus<FakePBFTEngine>& fake_pbft, ViewChangeReq::Ptr req);

template <typename T, typename S>
inline void fakePrepareMsg(std::shared_ptr<FakeConsensus<T>> _leaderPBFT,
    std::shared_ptr<FakeConsensus<T>> _followPBFT, PrepareReq::Ptr _prepareReq,
    std::shared_ptr<S> _blockFactory, bool _enablePrepareWithTxsHash = true)
{
    // init for the leaderPBFT: create blockFactory, PartiallyPBFTReqCache for the consensus engine
    _leaderPBFT->consensus()->setBlockFactory(_blockFactory);
    _leaderPBFT->consensus()->setEnablePrepareWithTxsHash(_enablePrepareWithTxsHash);
    _leaderPBFT->consensus()->createPBFTReqCache();

    // init for the followerPBFT
    _followPBFT->consensus()->setBlockFactory(_blockFactory);
    _followPBFT->consensus()->setEnablePrepareWithTxsHash(_enablePrepareWithTxsHash);
    _followPBFT->consensus()->createPBFTReqCache();

    _followPBFT->m_sealerList = _leaderPBFT->m_sealerList = _leaderPBFT->consensus()->sealerList();
    _followPBFT->m_keyPair = _leaderPBFT->m_keyPair;
    _followPBFT->m_nodeID2KeyPair = _leaderPBFT->m_nodeID2KeyPair;

    _followPBFT->consensus()->setSealerList(_leaderPBFT->consensus()->sealerList());

    TestIsValidPrepare(*_followPBFT, _prepareReq, true);
    _leaderPBFT->consensus()->setNodeIdx(_prepareReq->idx);
    _leaderPBFT->consensus()->setKeyPair(_leaderPBFT->m_keyPair[_prepareReq->idx]);
    _leaderPBFT->consensus()->setView(_prepareReq->view);
    std::shared_ptr<FakeBlock> fakedBlock = std::make_shared<FakeBlock>();
    _prepareReq->pBlock->setTransactions(fakedBlock->fakeTransactions(1, _prepareReq->height));

    auto followIdx = (_prepareReq->idx + 1) % 4;
    _followPBFT->consensus()->setNodeIdx(followIdx);
    _followPBFT->consensus()->setKeyPair(_leaderPBFT->m_keyPair[followIdx]);
}

void checkPrepareReqEqual(PrepareReq::Ptr _first, PrepareReq::Ptr _second);

}  // namespace test
}  // namespace bcos
