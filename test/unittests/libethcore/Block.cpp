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
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file Block.cpp
 * @author: yujiechen
 * @date 2018-09-21
 */
#include "FakeBlock.h"
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Transaction.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(BlockTest, TestOutputHelperFixture)
void checkBlock(Block& m_block, FakeBlock const& fake_block, size_t trans_size, size_t sig_size)
{
    BOOST_CHECK(m_block.blockHeader() == fake_block.m_blockHeader);
    BOOST_CHECK(m_block.headerHash() == fake_block.m_block->blockHeader().hash());
    BOOST_CHECK(*m_block.transactions() == *fake_block.m_transaction);
    BOOST_CHECK(m_block.transactions()->size() == trans_size);
    BOOST_CHECK(m_block.sigList()->size() == sig_size);
    BOOST_CHECK(*m_block.sigList() == *fake_block.m_sigList);
}
/// test constructors and operators
BOOST_AUTO_TEST_CASE(testConstructorsAndOperators)
{
    /// test constructor
    FakeBlock fake_block(5);
    Block m_block = *fake_block.getBlock();
    checkBlock(m_block, fake_block, 5, 5);
    /// test copy constructor
    Block copied_block(m_block);
    checkBlock(copied_block, fake_block, 5, 5);
    /// test operators==
    BOOST_CHECK(copied_block.equalAll(m_block));
    BOOST_CHECK(copied_block.equalHeader(m_block));
    BOOST_CHECK(copied_block.equalWithoutSig(m_block));
    BlockHeader emptyHeader;
    copied_block.setBlockHeader(emptyHeader);
    /// test operator !=
    BOOST_CHECK(copied_block.equalAll(m_block) == false);
    BOOST_CHECK(copied_block.equalHeader(m_block) == false);
    BOOST_CHECK(copied_block.equalWithoutSig(m_block) == false);
    /// test operator =
    copied_block = m_block;
    checkBlock(copied_block, fake_block, 5, 5);
    BOOST_CHECK(copied_block.equalAll(m_block));
    BOOST_CHECK(copied_block.equalHeader(m_block));
    BOOST_CHECK(copied_block.equalWithoutSig(m_block));

    /// test empty case
    FakeBlock fake_block_empty;
    Block m_empty_block = *fake_block_empty.getBlock();
    checkBlock(m_empty_block, fake_block_empty, 0, 0);
    m_empty_block = m_block;
    checkBlock(m_empty_block, fake_block, 5, 5);
    BOOST_CHECK(m_empty_block.equalAll(m_block));
    BOOST_CHECK(m_empty_block.equalHeader(m_block));
    BOOST_CHECK(m_empty_block.equalWithoutSig(m_block));
}

/// test Exceptions
BOOST_AUTO_TEST_CASE(testExceptionCases)
{
    /// test constructor
    FakeBlock fake_block;
    fake_block.CheckInvalidBlockData(1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
