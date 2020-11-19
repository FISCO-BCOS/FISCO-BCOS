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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief: unit test for RPBFTReqCache
 * @file: RPBFTReqCacheTest.cpp
 * @author: yujiechen
 * @date: 2020-1-16
 */
#include "Common.h"
#include <libconsensus/rotating_pbft/RPBFTReqCache.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace bcos::consensus;
using namespace bcos::eth;
namespace bcos
{
namespace test
{
class RPBFTReqCacheTestFixture : public TestOutputHelperFixture
{
public:
    template <typename T>
    std::shared_ptr<T> fakeRawPrepareStatus(bcos::h256 const& _blockHash,
        int64_t const& _blockHeight, VIEWTYPE const& _view, IDXTYPE const& _idx)
    {
        std::shared_ptr<T> pbftMsg = std::make_shared<T>();
        pbftMsg->block_hash = _blockHash;
        pbftMsg->height = _blockHeight;
        pbftMsg->view = _view;
        pbftMsg->idx = _idx;
        return pbftMsg;
    }
};
BOOST_FIXTURE_TEST_SUITE(RPBFTReqCacheTest, RPBFTReqCacheTestFixture)

BOOST_AUTO_TEST_CASE(testCheckReceivedRawPrepareStatus)
{
    RPBFTReqCache::Ptr rpbftReqCache = std::make_shared<RPBFTReqCache>();
    bcos::h256 blockHash = crypto::Hash("test");
    int64_t blockHeight = 10;
    VIEWTYPE view = 10;
    IDXTYPE nodeIdx = 1;
    auto pbftMsg = fakeRawPrepareStatus<PBFTMsg>(blockHash, blockHeight, view, nodeIdx);
    BOOST_CHECK(rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, blockHeight) == false);
    BOOST_CHECK(
        rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, (blockHeight - 1)) == true);

    // addRawPrepare
    PrepareReq::Ptr prepareReq =
        fakeRawPrepareStatus<PrepareReq>(blockHash, blockHeight, view, nodeIdx);
    rpbftReqCache->addRawPrepare(prepareReq);
    BOOST_CHECK(
        rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, (blockHeight - 1)) == false);

    // pbftMsg with larger view, the blockHeight is the same
    pbftMsg->view += 1;
    BOOST_CHECK(
        rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, (blockHeight - 1)) == true);

    //  pbftMsg with larger blockHeight and lower view
    pbftMsg->view -= 2;
    pbftMsg->height += 1;
    BOOST_CHECK(rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, blockHeight) == true);

    // pbftMsg with lower blockHeight and larger view
    pbftMsg->view += 10;
    pbftMsg->height -= 2;
    BOOST_CHECK(
        rpbftReqCache->checkReceivedRawPrepareStatus(pbftMsg, view, (blockHeight - 3)) == false);
}

BOOST_AUTO_TEST_CASE(testCheckAndRequestRawPrepare)
{
    RPBFTReqCache::Ptr pbftReqCache = std::make_shared<RPBFTReqCache>();
    bcos::h256 hash = crypto::Hash("test");
    int64_t blockNumber = 11;
    VIEWTYPE reqView = 12;
    IDXTYPE idx = 1;
    auto pbftMsg = fakeRawPrepareStatus<PBFTMsg>(hash, blockNumber, reqView, idx);

    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == true);
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == false);

    pbftMsg->block_hash = crypto::Hash("test2");
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == true);
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == false);

    pbftReqCache->setMaxRequestedPrepareQueueSize(2);
    pbftMsg->block_hash = crypto::Hash("test3");
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == true);
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == false);

    // check pop
    pbftMsg->block_hash = crypto::Hash("test");
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == true);
    BOOST_CHECK(pbftReqCache->checkAndRequestRawPrepare(pbftMsg) == false);
}

BOOST_AUTO_TEST_CASE(testResponseRawPrepare)
{
    RPBFTReqCache::Ptr reqCache = std::make_shared<RPBFTReqCache>();
    // rawPrepareCache miss
    bcos::h256 blockHash = crypto::Hash("test");
    int64_t blockHeight = 20;
    VIEWTYPE view = 15;
    IDXTYPE nodeIdx = 4;
    auto pbftMsg = fakeRawPrepareStatus<PBFTMsg>(blockHash, blockHeight, view, nodeIdx);
    std::shared_ptr<bcos::bytes> encodedRawPrepare = std::make_shared<bcos::bytes>();
    BOOST_CHECK_THROW(
        reqCache->responseRawPrepare(encodedRawPrepare, pbftMsg), RequestedRawPrepareNotFound);

    // rawPrepare hit
    PrepareReq::Ptr prepareReq =
        fakeRawPrepareStatus<PrepareReq>(blockHash, blockHeight, view, nodeIdx);
    reqCache->addRawPrepare(prepareReq);
    reqCache->responseRawPrepare(encodedRawPrepare, pbftMsg);
    BOOST_CHECK(encodedRawPrepare->size() > 0);
    PrepareReq::Ptr decodedPrepare = std::make_shared<PrepareReq>();
    decodedPrepare->decode(ref(*encodedRawPrepare));
    BOOST_CHECK(*prepareReq == *decodedPrepare);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
