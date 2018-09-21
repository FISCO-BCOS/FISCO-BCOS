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
BOOST_FIXTURE_TEST_SUITE(BlockTest, TestOutputHelperFixture);
/// test constructors
BOOST_AUTO_TEST_CASE(testConstructors)
{
    /// test constructor
    FakeBlock fake_block;
    Block m_block = fake_block.getBlock();
    std::cout << "### m_block.blockHeader hash:" << m_block.headerHash() << std::endl;
    std::cout << "### fake_block.getBlockHeader().hash():" << fake_block.getBlockHeader().hash()
              << std::endl;
    BOOST_CHECK(m_block.blockHeader() == fake_block.getBlockHeader());
    BOOST_CHECK(m_block.headerHash() == fake_block.getBlockHeader().hash());
    BOOST_CHECK(m_block.hash() == sha3(fake_block.getBlockData()));
    std::vector<std::pair<u256, Signature>> sig_list;
    BOOST_CHECK(m_block.sigList() == sig_list);

    /// test copy
}
/// test operators
BOOST_AUTO_TEST_CASE(testOperators) {}

/// test encode and decode
BOOST_AUTO_TEST_CASE(testEncodeAndDecode) {}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev