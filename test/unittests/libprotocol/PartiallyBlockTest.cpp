/**
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
 * (c) 2016-2019 fisco-dev contributors.
 *
 * @brief
 *
 * @file PartiallyBlock.cpp
 * @author: yujiechen
 * @date 2019-11-22
 */

#include "FakeBlock.h"
#include <libprotocol/BlockFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

namespace bcos
{
namespace test
{
class FakePartiallyBlock
{
public:
    FakePartiallyBlock(size_t const& _txsSize, uint64_t const& _blockNumber)
    {
        // create PartiallyBlockFactory
        m_blockFactory = std::make_shared<PartiallyBlockFactory>();
        auto fakedBlock = std::make_shared<FakeBlock>(
            _txsSize, KeyPair::create().secret(), _blockNumber, m_blockFactory);
        m_block = std::dynamic_pointer_cast<PartiallyBlock>(fakedBlock->m_block);
        size_t index = 0;
        for (auto tx : *m_block->transactions())
        {
            tx->setNonce(utcTime() + index);
            tx->setBlockLimit(200);
            std::shared_ptr<crypto::Signature> sig =
                bcos::crypto::Sign(fakedBlock->m_keyPair, tx->hash(WithoutSignature));
            tx->updateSignature(sig);
            index++;
        }
    }

    FakePartiallyBlock(std::shared_ptr<PartiallyBlock> _block)
    {
        m_block = std::make_shared<PartiallyBlock>(_block);
    }

    std::shared_ptr<PartiallyBlockFactory> m_blockFactory;
    std::shared_ptr<PartiallyBlock> m_block;
};

void checkFillBlock(std::shared_ptr<FakePartiallyBlock> fakePartiallyBlockReceiver,
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlock, size_t const& _txsSize)
{
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->txsAllHit() == false);
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->missedTxs()->size() == _txsSize);

    // step2: request transactions to the sender
    std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
    fakePartiallyBlockReceiver->m_block->encodeMissedInfo(encodedData);
    // step3: the sender response the transactions
    std::shared_ptr<bytes> fetchedTxs = std::make_shared<bytes>();
    fakePartiallyBlock->m_block->fetchMissedTxs(
        fetchedTxs, ref(*encodedData), fakePartiallyBlockReceiver->m_block->header().hash());
    // step4: the receiver fill the block according to responsed transactions
    fakePartiallyBlockReceiver->m_block->fillBlock(ref(*fetchedTxs));
    // check the result
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->missedTransactions()->size() == _txsSize);
    size_t index = 0;
    for (auto _tx : *fakePartiallyBlockReceiver->m_block->transactions())
    {
        BOOST_CHECK(*_tx == *((*fakePartiallyBlock->m_block->transactions())[index]));
        index++;
    }
}

BOOST_FIXTURE_TEST_SUITE(PartiallyBlockTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testEncodeDecodeProposal)
{
    // fake PartiallyBlock with 5 transactions, and the block number is 1
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlock =
        std::make_shared<FakePartiallyBlock>(5, 1);

    // test _onlyTxsHash is true
    std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
    BOOST_CHECK_NO_THROW(fakePartiallyBlock->m_block->encodeProposal(encodedData, true));

    std::shared_ptr<FakePartiallyBlock> fakeReceiver =
        std::make_shared<FakePartiallyBlock>(fakePartiallyBlock->m_block);
    BOOST_CHECK(fakePartiallyBlock->m_block->blockHeader() == fakeReceiver->m_block->blockHeader());

    fakeReceiver->m_block->transactions()->clear();

    BOOST_CHECK_NO_THROW(fakeReceiver->m_block->decodeProposal(ref(*encodedData), true));
    // check transaction size
    BOOST_CHECK(fakeReceiver->m_block->transactions()->size() == 5);
    // check txsHash
    BOOST_CHECK(fakeReceiver->m_block->txsHash()->size() == 5);
    // check block header
    BOOST_CHECK(fakeReceiver->m_block->blockHeader() == fakePartiallyBlock->m_block->blockHeader());
    // check transaction hash
    size_t index = 0;
    for (auto tx : *(fakePartiallyBlock->m_block->transactions()))
    {
        BOOST_CHECK(tx->hash() == (*fakePartiallyBlock->m_block->txsHash())[index]);
        index++;
    }

    // test _onlyTxsHash is false
    BOOST_CHECK_NO_THROW(fakePartiallyBlock->m_block->encodeProposal(encodedData, false));
    fakeReceiver = std::make_shared<FakePartiallyBlock>(fakePartiallyBlock->m_block);
    BOOST_CHECK_NO_THROW(fakeReceiver->m_block->decodeProposal(ref(*encodedData), false));
    // check transaction size
    BOOST_CHECK(fakeReceiver->m_block->transactions()->size() == 5);
    // check block header
    BOOST_CHECK(fakeReceiver->m_block->blockHeader() == fakePartiallyBlock->m_block->blockHeader());
    // check transaction
    index = 0;
    for (auto tx : *(fakePartiallyBlock->m_block->transactions()))
    {
        BOOST_CHECK(*tx == *((*fakeReceiver->m_block->transactions())[index]));
        index++;
    }
}

BOOST_AUTO_TEST_CASE(testEncodeFetchMissedTxs)
{
    /// fake PartiallyBlock with 10 transactions, and the block number is 20
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlock =
        std::make_shared<FakePartiallyBlock>(10, 20);
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlockReceiver =
        std::make_shared<FakePartiallyBlock>(fakePartiallyBlock->m_block);

    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->blockHeader() ==
                fakePartiallyBlock->m_block->blockHeader());

    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->transactions()->size() == 10);
    fakePartiallyBlockReceiver->m_block->transactions()->clear();
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->transactions()->size() == 0);

    std::shared_ptr<TxPoolFixture> txPoolFixture = std::make_shared<TxPoolFixture>(5, 5);
    auto txPool = txPoolFixture->m_txPool;

    std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
    fakePartiallyBlock->m_block->encodeProposal(encodedData, true);
    fakePartiallyBlockReceiver->m_block->decodeProposal(ref(*encodedData), true);
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->txsHash()->size() == 10);
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->missedTxs()->size() == 0);
    // for resize
    BOOST_CHECK(fakePartiallyBlockReceiver->m_block->transactions()->size() == 10);

    /// case1: miss all the txs
    // step1: fetch transactions from txPool
    auto allHit = txPool->initPartiallyBlock(fakePartiallyBlockReceiver->m_block);
    BOOST_CHECK(allHit == false);
    checkFillBlock(fakePartiallyBlockReceiver, fakePartiallyBlock, 10);

    // case2: hit two transactions
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlockReceiver2 =
        std::make_shared<FakePartiallyBlock>(fakePartiallyBlock->m_block);
    fakePartiallyBlockReceiver2->m_block->transactions()->clear();
    fakePartiallyBlockReceiver2->m_block->decodeProposal(ref(*encodedData), true);
    for (size_t index = 0; index < 2; index++)
    {
        txPool->submitTransactions((*fakePartiallyBlock->m_block->transactions())[index]);
    }
    allHit = txPool->initPartiallyBlock(fakePartiallyBlockReceiver2->m_block);
    BOOST_CHECK(allHit == false);
    checkFillBlock(fakePartiallyBlockReceiver2, fakePartiallyBlock, 8);

    // case3: hit all the transactions
    std::shared_ptr<FakePartiallyBlock> fakePartiallyBlockReceiver3 =
        std::make_shared<FakePartiallyBlock>(fakePartiallyBlock->m_block);
    fakePartiallyBlockReceiver3->m_block->transactions()->clear();
    fakePartiallyBlockReceiver3->m_block->decodeProposal(ref(*encodedData), true);

    for (size_t index = 2; index < fakePartiallyBlock->m_block->transactions()->size(); index++)
    {
        txPool->submitTransactions((*fakePartiallyBlock->m_block->transactions())[index]);
    }
    allHit = txPool->initPartiallyBlock(fakePartiallyBlockReceiver3->m_block);
    BOOST_CHECK(allHit == true);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
