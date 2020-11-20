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
#include "libdevcrypto/CryptoInterface.h"
#include <libethcore/Block.h>
#include <test/tools/libutils/TestOutputHelper.h>
namespace bcos
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
    PrepareReq::Ptr prepare_req = std::make_shared<PrepareReq>(FakePrepareReq(key_pair));
    req_cache.addRawPrepare(prepare_req);
    BOOST_CHECK(req_cache.isExistPrepare(*prepare_req));
    checkPBFTMsg(req_cache.rawPrepareCache(), key_pair, prepare_req->height, prepare_req->view,
        prepare_req->idx, req_cache.rawPrepareCache().timestamp, prepare_req->block_hash);
    checkPBFTMsg(req_cache.prepareCache());

    /// test addSignReq
    SignReq::Ptr sign_req = std::make_shared<SignReq>(*prepare_req, key_pair, prepare_req->idx);
    sign_req->view = prepare_req->view + 1;
    req_cache.addSignReq(sign_req);
    BOOST_CHECK(req_cache.isExistSign(*sign_req));
    auto fakeFutureReqCheckNum = 100;
    BOOST_CHECK(req_cache.getSigCacheSize(sign_req->block_hash, fakeFutureReqCheckNum) == 1);
    /// test addCommitReq
    CommitReq::Ptr commit_req =
        std::make_shared<CommitReq>(*prepare_req, key_pair, prepare_req->idx);
    commit_req->view = prepare_req->view + 1;
    req_cache.addCommitReq(commit_req);
    BOOST_CHECK(req_cache.isExistCommit(*commit_req));
    BOOST_CHECK(req_cache.getCommitCacheSize(commit_req->block_hash, fakeFutureReqCheckNum) == 1);
    /// test addPrepareReq
    req_cache.addPrepareReq(prepare_req);
    /// test invalid signReq and commitReq removement
    checkPBFTMsg(req_cache.prepareCache(), key_pair, prepare_req->height, prepare_req->view,
        prepare_req->idx, req_cache.prepareCache().timestamp, prepare_req->block_hash);
    BOOST_CHECK(!req_cache.isExistSign(*sign_req));
    BOOST_CHECK(!req_cache.isExistCommit(*commit_req));
    BOOST_CHECK(req_cache.getSigCacheSize(sign_req->block_hash, fakeFutureReqCheckNum) == 0);
    BOOST_CHECK(req_cache.getCommitCacheSize(commit_req->block_hash, fakeFutureReqCheckNum) == 0);

    /// test addFuturePrepareCache
    req_cache.addFuturePrepareCache(prepare_req);
    BOOST_CHECK(req_cache.futurePrepareCacheSize() == 1);
    auto p_future = req_cache.futurePrepareCache(prepare_req->height);
    checkPBFTMsg(*(p_future), key_pair, prepare_req->height, prepare_req->view, prepare_req->idx,
        p_future->timestamp, prepare_req->block_hash);
    req_cache.eraseHandledFutureReq(prepare_req->height);
    BOOST_CHECK(req_cache.futurePrepareCacheSize() == 0);

    /// test removeInvalidFutureCache
    for (int i = 0; i < 12; i++)
    {
        PrepareReq::Ptr req = std::make_shared<PrepareReq>(*prepare_req);
        req->height = i;
        req_cache.addFuturePrepareCache(req);
    }
    // at most 10 futurePrepareCache
    BOOST_CHECK(req_cache.futurePrepareCacheSize() == 10);
    BlockHeader highest;
    highest.setNumber(8);
    req_cache.removeInvalidFutureCache(highest.number());
    BOOST_CHECK(req_cache.futurePrepareCacheSize() == 1);
}
/// test generateAndSetSigList
BOOST_AUTO_TEST_CASE(testSigListSetting)
{
    size_t node_num = 3;
    PBFTReqCache req_cache;
    /// fake prepare req
    KeyPair key_pair;
    PrepareReq::Ptr prepare_req = std::make_shared<PrepareReq>(FakePrepareReq(key_pair));
    for (size_t i = 0; i < node_num; i++)
    {
        KeyPair key = KeyPair::create();
        /// fake commit req from faked prepare req
        CommitReq::Ptr commit_req =
            std::make_shared<CommitReq>(*prepare_req, key, prepare_req->idx);
        req_cache.addCommitReq(commit_req);
        BOOST_CHECK(req_cache.isExistCommit(*commit_req));
    }
    BOOST_CHECK(req_cache.getCommitCacheSize(prepare_req->block_hash, node_num) == node_num);
    /// generateAndSetSigList
    Block block;
    bool ret = req_cache.generateAndSetSigList(block, node_num);
    BOOST_CHECK(ret == false);
    /// add prepare
    req_cache.addPrepareReq(prepare_req);
    ret = req_cache.generateAndSetSigList(block, node_num);
    BOOST_CHECK(ret);
    BOOST_CHECK(block.sigList()->size() == node_num);
    std::vector<std::pair<u256, std::vector<unsigned char>>> sig_list = *(block.sigList());
    /// check the signature
    for (auto& item : sig_list)
    {
        auto p = bcos::crypto::Recover(
            bcos::crypto::SignatureFromBytes(item.second), prepare_req->block_hash);
        BOOST_CHECK(!!p);
    }
}
/// test collectGarbage
BOOST_AUTO_TEST_CASE(testCollectGarbage)
{
    PBFTReqCache req_cache;
    PrepareReq::Ptr req = std::make_shared<PrepareReq>();
    size_t invalidHeightNum = 2;
    size_t invalidHash = 1;
    size_t validNum = 3;
    h256 invalid_hash = crypto::Hash("invalid_hash");
    BlockHeader highest;
    highest.setNumber(100);
    /// test signcache
    FakeInvalidReq<SignReq>(req, req_cache, req_cache.mutableSignCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    /// test commit cache
    FakeInvalidReq<CommitReq>(req, req_cache, req_cache.mutableCommitCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    req_cache.collectGarbage(highest);
    BOOST_CHECK(req_cache.getSigCacheSize(req->block_hash, validNum) == validNum);
    BOOST_CHECK(req_cache.getSigCacheSize(invalid_hash, validNum) == 0);

    BOOST_CHECK(req_cache.getCommitCacheSize(req->block_hash, validNum) == validNum);
    BOOST_CHECK(req_cache.getCommitCacheSize(invalid_hash, validNum) == 0);
    /// test delCache
    req_cache.delCache(highest);
    BOOST_CHECK(req_cache.getSigCacheSize(req->block_hash, validNum) == 0);
    BOOST_CHECK(req_cache.getCommitCacheSize(req->block_hash, validNum) == 0);
}
/// test canTriggerViewChange
BOOST_AUTO_TEST_CASE(testCanTriggerViewChange)
{
    KeyPair key_pair;
    PBFTReqCache req_cache;
    PrepareReq::Ptr prepare_req = std::make_shared<PrepareReq>(FakePrepareReq(key_pair));
    req_cache.addRawPrepare(prepare_req);
    prepare_req->height += 1;
    /// update prepare cache to commited prepare
    req_cache.updateCommittedPrepare();

    BlockHeader header;
    header.setNumber(prepare_req->height - 1);
    IDXTYPE maxInvalidNodeNum = 1;
    VIEWTYPE toView = prepare_req->view;
    int64_t consensusBlockNumber = prepare_req->height - 1;
    /// fake viewchange
    ViewChangeReq::Ptr viewChange_req = std::make_shared<ViewChangeReq>(key_pair,
        prepare_req->height, prepare_req->view, prepare_req->idx, prepare_req->block_hash);
    ViewChangeReq::Ptr viewChange_req2 = std::make_shared<ViewChangeReq>(*viewChange_req);
    viewChange_req2->idx += 1;
    viewChange_req2->view += 1;
    ViewChangeReq::Ptr viewChange_req3 = std::make_shared<ViewChangeReq>(*viewChange_req);
    viewChange_req3->view += 2;
    viewChange_req3->idx += 2;

    req_cache.addViewChangeReq(viewChange_req);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req));
    req_cache.addViewChangeReq(viewChange_req2);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req2));
    req_cache.addViewChangeReq(viewChange_req3);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req3));
    VIEWTYPE minView;
    BOOST_CHECK(req_cache.canTriggerViewChange(
        minView, maxInvalidNodeNum, toView, header, consensusBlockNumber));
    BOOST_CHECK(minView == viewChange_req2->view);
    maxInvalidNodeNum = 3;
    BOOST_CHECK(req_cache.canTriggerViewChange(
                    minView, maxInvalidNodeNum, toView, header, consensusBlockNumber) == false);
}

/// test ViewChange related functions
BOOST_AUTO_TEST_CASE(testViewChangeReqRelated)
{
    KeyPair key_pair = KeyPair::create();
    ViewChangeReq::Ptr viewChange_req =
        std::make_shared<ViewChangeReq>(key_pair, 100, 1, 1, crypto::Hash("test_view"));
    PBFTReqCache req_cache;
    /// test exists of viewchange
    req_cache.addViewChangeReq(viewChange_req);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req));
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 1);
    /// generate and add a new viewchange request with the same blockhash, but different views
    ViewChangeReq::Ptr viewChange_req2 =
        std::make_shared<ViewChangeReq>(key_pair, 101, 1, 2, crypto::Hash("test_view"));
    req_cache.addViewChangeReq(viewChange_req2);
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 2);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req2));
    ViewChangeReq::Ptr viewChange_req3 = std::make_shared<ViewChangeReq>(*viewChange_req2);
    viewChange_req3->height = 102;
    viewChange_req3->idx = 3;
    /// generate faked block header
    BlockHeader header;
    header.setNumber(101);
    viewChange_req3->block_hash = header.hash();
    req_cache.addViewChangeReq(viewChange_req3);
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 3);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req3));
    //// test removeInvalidViewChange
    req_cache.removeInvalidViewChange(1, header);
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 1);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req3));
    /// test delInvalidViewChange
    viewChange_req3->height = header.number() - 1;
    req_cache.addViewChangeReq(viewChange_req3);
    req_cache.delInvalidViewChange(header);
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 0);
    viewChange_req3->height += 1;
    viewChange_req3->block_hash = header.hash();
    req_cache.addViewChangeReq(viewChange_req3);
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 1);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req3));

    /// test tiggerViewChange
    req_cache.addViewChangeReq(viewChange_req);
    req_cache.addViewChangeReq(viewChange_req2);
    PrepareReq::Ptr req = std::make_shared<PrepareReq>();
    BlockHeader highest;
    highest.setNumber(100);
    size_t invalidHeightNum = 2;
    size_t invalidHash = 1;
    size_t validNum = 3;
    h256 invalid_hash = crypto::Hash("invalid_hash");
    /// fake signCache and commitCache
    FakeInvalidReq<SignReq>(req, req_cache, req_cache.mutableSignCache(), highest, invalid_hash,
        invalidHeightNum, invalidHash, validNum);
    /// trigger viewChange
    req_cache.triggerViewChange(0, 0);
    BOOST_CHECK(req_cache.isExistViewChange(*viewChange_req3));
    BOOST_CHECK(req_cache.mutableSignCache().size() == 0);
    BOOST_CHECK(req_cache.mutableCommitCache().size() == 0);
    checkPBFTMsg(req_cache.rawPrepareCache());
    checkPBFTMsg(req_cache.prepareCache());

    /// test clearAll
    req_cache.clearAllExceptCommitCache();
    BOOST_CHECK(req_cache.getViewChangeSize(1) == 0);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
