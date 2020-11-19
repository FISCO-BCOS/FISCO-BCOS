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
 * @brief: unit test for PartiallyPBFTReqCache
 * @file: PartiallyPBFTReqCacheTest.cpp
 * @author: yujiechen
 * @date: 2020-1-16
 */

#include "Common.h"
#include <libconsensus/pbft/PartiallyPBFTReqCache.h>
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/test/unit_test.hpp>
using namespace bcos::consensus;
using namespace bcos::eth;

namespace bcos
{
namespace test
{
class PartiallyPBFTReqCacheTestFixture : public TestOutputHelperFixture
{
public:
    // fake block with _txsSize transactions
    std::shared_ptr<Block> fakeBlock(int64_t const& _txsSize)
    {
        return std::make_shared<FakeBlock>(_txsSize)->m_block;
    }

    void checkPrepare(PrepareReq::Ptr _prepareReq, PrepareReq::Ptr _decodedPrepareReq)
    {
        BOOST_CHECK(_prepareReq->block_hash == _decodedPrepareReq->block_hash);
        BOOST_CHECK(_prepareReq->view == _decodedPrepareReq->view);
        BOOST_CHECK(_prepareReq->height == _decodedPrepareReq->height);
        BOOST_CHECK(_prepareReq->idx == _decodedPrepareReq->idx);

        // check the block
        _decodedPrepareReq->pBlock = std::make_shared<PartiallyBlock>();
        _decodedPrepareReq->pBlock->decodeProposal(ref(*_decodedPrepareReq->block), true);
        BOOST_CHECK(
            _decodedPrepareReq->pBlock->blockHeader() == _prepareReq->pBlock->blockHeader());
        BOOST_CHECK(_decodedPrepareReq->pBlock->blockHeader().hash() ==
                    _prepareReq->pBlock->blockHeader().hash());
        BOOST_CHECK(_decodedPrepareReq->pBlock->getTransactionSize() ==
                    _prepareReq->pBlock->getTransactionSize());
    }
};

BOOST_FIXTURE_TEST_SUITE(PartiallyPBFTReqCacheTest, PartiallyPBFTReqCacheTestFixture)

BOOST_AUTO_TEST_CASE(testFetchMissedTxsAndFillBlock)
{
    // fake for the leader
    PartiallyPBFTReqCache::Ptr leaderReqCache = std::make_shared<PartiallyPBFTReqCache>();
    // fake block
    PartiallyBlock::Ptr block = std::make_shared<PartiallyBlock>();
    std::shared_ptr<Block> tmp = block;
    auto commonBlock = fakeBlock(5);
    *tmp = *commonBlock;

    VIEWTYPE view = 10;
    IDXTYPE nodeIdx = 1;
    PrepareReq::Ptr prepareReq =
        std::make_shared<PrepareReq>(block, KeyPair::create(), view, nodeIdx, true);
    leaderReqCache->addRawPrepare(prepareReq);
    // encode proposal
    std::shared_ptr<bcos::bytes> encodedPrepareData = std::make_shared<bcos::bytes>();
    prepareReq->encode(*encodedPrepareData);

    // fake for the follow receive prepare packet from the leader
    PartiallyPBFTReqCache::Ptr followerReqCache = std::make_shared<PartiallyPBFTReqCache>();
    PrepareReq::Ptr followsPrepareReq = std::make_shared<PrepareReq>();
    followsPrepareReq->decode(ref(*encodedPrepareData));
    checkPrepare(prepareReq, followsPrepareReq);
    followerReqCache->addPartiallyRawPrepare(followsPrepareReq);
    BOOST_CHECK(followsPrepareReq == followerReqCache->partiallyRawPrepare());

    // encode the missedInfo
    std::shared_ptr<bcos::txpool::TxPool> txPool = std::make_shared<bcos::txpool::TxPool>();
    txPool->initPartiallyBlock(followsPrepareReq->pBlock);

    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(followsPrepareReq->pBlock);
    std::shared_ptr<bytes> encodedMissTxsInfo = std::make_shared<bytes>();
    partiallyBlock->encodeMissedInfo(encodedMissTxsInfo);

    // case1: fetch the missed transactions from the leader
    std::shared_ptr<bytes> encodedTxsBytes = std::make_shared<bytes>();
    BOOST_CHECK(leaderReqCache->fetchMissedTxs(encodedTxsBytes, ref(*encodedMissTxsInfo)) == true);

    // the follow fill the block when receive the missed transactions
    followerReqCache->fillBlock(ref(*encodedTxsBytes));
    // the follow trans PartiallyPrepare into rawPrepare
    followerReqCache->transPartiallyPrepareIntoRawPrepare();
    BOOST_CHECK(followerReqCache->rawPrepareCache() == *followsPrepareReq);
    // check encode
    BOOST_CHECK(followerReqCache->rawPrepareCache().block->size() > 0);
    std::shared_ptr<PartiallyBlock> tmpBlock = std::make_shared<PartiallyBlock>();
    tmpBlock->decode(ref(*followerReqCache->rawPrepareCache().block));

    // check the the block
    int64_t i = 0;
    for (auto tx : *(followsPrepareReq->pBlock->transactions()))
    {
        BOOST_CHECK(*tx == *((*prepareReq->pBlock->transactions())[i]));
        BOOST_CHECK(*tx == *((*tmpBlock->transactions())[i]));
    }

    // case2: the leader has already modified the rawPrepareReq
    PrepareReq::Ptr updatedPrepareReq = std::make_shared<PrepareReq>();
    updatedPrepareReq->block_hash = crypto::Hash("update");
    leaderReqCache->addRawPrepare(updatedPrepareReq);
    BOOST_CHECK(leaderReqCache->fetchMissedTxs(encodedTxsBytes, ref(*encodedMissTxsInfo)) == false);

    // case2: the leader hasn't add prepareReq into rawPrepareCache when receive missed transactions
    // request
    leaderReqCache->addPreRawPrepare(prepareReq);
    BOOST_CHECK(leaderReqCache->fetchMissedTxs(encodedTxsBytes, ref(*encodedMissTxsInfo)) == true);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
