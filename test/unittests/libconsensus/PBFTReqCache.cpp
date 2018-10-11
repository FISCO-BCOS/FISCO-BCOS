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
 * @brief: unit test for the base class of consensus module(libconsensus/consensus.*)
 * @file: consensus.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "Common.h"
#include <libconsensus/pbft/PBFTReqCache.h>
#include <libethcore/BlockHeader.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev::consensus;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBFTReqCacheTest, TestOutputHelperFixture)
/// test add-and-exists related functions
BOOST_AUTO_TEST_CASE(testAddAndExistCase)
{
    PBFTReqCache req_cache;
    /// test addRawPrepare
    KeyPair key_pair = KeyPair::create();
    h256 block_hash = sha3("key_pair");
    PrepareReq prepare_req(key_pair, 1000, u256(1), u256(134), block_hash);
    req_cache.addRawPrepare(prepare_req);
    BOOST_CHECK(req_cache.isExistPrepare(prepare_req));
    checkPBFTMsg(req_cache.rawPrepareCache(), key_pair, 1000, u256(1), u256(134),
        req_cache.rawPrepareCache().timestamp, block_hash);
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
    checkPBFTMsg(req_cache.prepareCache(), key_pair, 1000, u256(1), u256(134),
        req_cache.prepareCache().timestamp, block_hash);
    BOOST_CHECK(!req_cache.isExistSign(sign_req));
    BOOST_CHECK(!req_cache.isExistCommit(commit_req));
    BOOST_CHECK(req_cache.getSigCacheSize(sign_req.block_hash) == u256(0));
    BOOST_CHECK(req_cache.getCommitCacheSize(commit_req.block_hash) == u256(0));
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
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
