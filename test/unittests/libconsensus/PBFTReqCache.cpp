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
 * @brief: unit test for libconsensus/pbft/PBFTReqCache.h
 * @file: PBFTReqCache.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "PBFTReqCache.h"
#include <libethcore/Block.h>
#include <test/tools/libutils/TestOutputHelper.h>
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBFTReqCacheTest, TestOutputHelperFixture)
/// test add-and-exists related functions
BOOST_AUTO_TEST_CASE(testAddAndExistCase)
{
    PBFTReqCache req_cache;
    KeyPair key_pair;
    /// test addRawPrepare
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    req_cache.addRawPrepare(prepare_req);
    BOOST_CHECK(req_cache.isExistPrepare(prepare_req));
    checkPBFTMsg(req_cache.rawPrepareCache(), key_pair, prepare_req.height, prepare_req.view,
        prepare_req.idx, req_cache.rawPrepareCache().timestamp, prepare_req.block_hash);
    checkPBFTMsg(req_cache.prepareCache());

    /// test addSignReq
    SignReq sign_req(prepare_req, key_pair, prepare_req.idx);
    sign_req.view = prepare_req.view + u256(1);
    req_cache.addSignReq(sign_req);
    BOOST_CHECK(req_cache.isExistSign(sign_req));
    BOOST_CHECK(req_cache.getSigCacheSize(sign_req.block_hash) == u256(1));
    /// test addCommitReq
    CommitReq commit_req(prepare_req, key_pair, prepare_req.idx);
    commit_req.view = prepare_req.view + u256(1);
    req_cache.addCommitReq(commit_req);
    BOOST_CHECK(req_cache.isExistCommit(commit_req));
    BOOST_CHECK(req_cache.getCommitCacheSize(commit_req.block_hash) == u256(1));
    /// test addPrepareReq
    req_cache.addPrepareReq(prepare_req);
    /// test invalid signReq and commitReq removement
    checkPBFTMsg(req_cache.prepareCache(), key_pair, prepare_req.height, prepare_req.view,
        prepare_req.idx, req_cache.prepareCache().timestamp, prepare_req.block_hash);
    BOOST_CHECK(!req_cache.isExistSign(sign_req));
    BOOST_CHECK(!req_cache.isExistCommit(commit_req));
    BOOST_CHECK(req_cache.getSigCacheSize(sign_req.block_hash) == u256(0));
    BOOST_CHECK(req_cache.getCommitCacheSize(commit_req.block_hash) == u256(0));
}

BOOST_AUTO_TEST_CASE(testSigListSetting)
{
    size_t node_num = 3;
    PBFTReqCache req_cache;
    /// fake prepare req
    KeyPair key_pair;
    PrepareReq prepare_req = FakePrepareReq(key_pair);
    for (size_t i = 0; i < node_num; i++)
    {
        KeyPair key = KeyPair::create();
        /// fake commit req from faked prepare req
        CommitReq commit_req(prepare_req, key, prepare_req.idx);
        req_cache.addCommitReq(commit_req);
        BOOST_CHECK(req_cache.isExistCommit(commit_req));
    }
    BOOST_CHECK(req_cache.getCommitCacheSize(prepare_req.block_hash) == u256(node_num));
    /// generateAndSetSigList
    Block block;
    bool ret = req_cache.generateAndSetSigList(block, u256(node_num));
    BOOST_CHECK(ret == false);
    /// add prepare
    req_cache.addPrepareReq(prepare_req);
    ret = req_cache.generateAndSetSigList(block, u256(node_num));
    BOOST_CHECK(ret);
    BOOST_CHECK(block.sigList().size() == node_num);
    std::vector<std::pair<u256, Signature>> sig_list = block.sigList();
    /// check the signature
    for (auto item : sig_list)
    {
        auto p = dev::recover(item.second, prepare_req.block_hash);
        BOOST_CHECK(!!p);
    }
}

BOOST_AUTO_TEST_CASE(testCollectGarbage)
{
    PBFTReqCache req_cache;
    PrepareReq req;
    size_t invalidHeightNum = 2;
    size_t invalidHash = 1;
    size_t validNum = 3;
    h256 invalid_hash = sha3("invalid_hash");
    BlockHeader highest;
    highest.setNumber(100);
    /// test signcache
    FakeInvalidSignReq<SignReq>(req, req_cache, req_cache.signCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    /// test commit cache
    FakeInvalidSignReq<CommitReq>(req, req_cache, req_cache.commitCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    req_cache.collectGarbage(highest);
    std::cout << "#### getSigCacheSize after collectGarbage:"
              << req_cache.getSigCacheSize(req.block_hash) << std::endl;
    BOOST_CHECK(req_cache.getSigCacheSize(req.block_hash) == u256(validNum));
    BOOST_CHECK(req_cache.getSigCacheSize(invalid_hash) == u256(0));

    BOOST_CHECK(req_cache.getCommitCacheSize(req.block_hash) == u256(validNum));
    BOOST_CHECK(req_cache.getCommitCacheSize(invalid_hash) == u256(0));
    /// test delCache
    req_cache.delCache(req.block_hash);
    BOOST_CHECK(req_cache.getSigCacheSize(req.block_hash) == u256(0));
    BOOST_CHECK(req_cache.getCommitCacheSize(req.block_hash) == u256(0));
}

/// test ViewChange related functions
BOOST_AUTO_TEST_CASE(testViewChangeReqRelated)
{
    KeyPair key_pair = KeyPair::create();
    ViewChangeReq viewChange_req(key_pair, 100, u256(1), u256(1), sha3("test_view"));
    PBFTReqCache req_cache;
    /// test exists of viewchange
    req_cache.addViewChangeReq(viewChange_req);
    BOOST_CHECK(req_cache.isExistViewChange(viewChange_req));
    BOOST_CHECK(req_cache.getViewChangeSize(u256(1)) == u256(1));
    /// generate and add a new viewchange request with the same blockhash, but different views
    ViewChangeReq viewChange_req2(key_pair, 101, u256(1), u256(2), sha3("test_view"));
    req_cache.addViewChangeReq(viewChange_req2);
    BOOST_CHECK(req_cache.getViewChangeSize(u256(1)) == u256(2));
    BOOST_CHECK(req_cache.isExistViewChange(viewChange_req2));
    ViewChangeReq viewChange_req3(viewChange_req2);
    viewChange_req3.height = 102;
    viewChange_req3.idx = u256(3);
    /// generate faked block header
    BlockHeader header;
    header.setNumber(101);
    viewChange_req3.block_hash = header.hash();
    req_cache.addViewChangeReq(viewChange_req3);
    BOOST_CHECK(req_cache.getViewChangeSize(u256(1)) == u256(3));
    BOOST_CHECK(req_cache.isExistViewChange(viewChange_req3));
    //// test removeInvalidViewChange
    req_cache.removeInvalidViewChange(u256(1), header);
    BOOST_CHECK(req_cache.getViewChangeSize(u256(1)) == u256(1));
    BOOST_CHECK(req_cache.isExistViewChange(viewChange_req3));

    ///
    PrepareReq req;
    BlockHeader highest;
    highest.setNumber(100);
    size_t invalidHeightNum = 2;
    size_t invalidHash = 1;
    size_t validNum = 3;
    h256 invalid_hash = sha3("invalid_hash");
    /// test signcache
    FakeInvalidSignReq<SignReq>(req, req_cache, req_cache.signCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    /*
    bool canTriggerViewChange(u256& minView, u256 const& minInvalidNodeNum, u256 const& toView,
        dev::eth::BlockHeader const& highestBlock, int64_t const& consensusBlockNumber,
        ViewChangeReq const& req);

    inline void triggerViewChange(u256 const& curView)
    {
        m_rawPrepareCache.clear();
        m_prepareCache.clear();
        m_signCache.clear();
        m_commitCache.clear();
        removeInvalidViewChange(curView);
    }*/

    /*
    inline void delInvalidViewChange(dev::eth::BlockHeader const& curHeader)
    {
        removeInvalidEntryFromCache(curHeader, m_recvViewChangeReq);
    }
    inline void clearAll()
    {
        m_prepareCache.clear();
        m_rawPrepareCache.clear();
        m_signCache.clear();
        m_recvViewChangeReq.clear();
        m_commitCache.clear();
    }
    void addFuturePrepareCache(PrepareReq const& req);
    void resetFuturePrepare() { m_futurePrepareCache = PrepareReq(); }*/
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
