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
 * @brief : doownloading block queue test
 * @author: catli
 * @date: 2018-10-23
 */

#include <libsync/DownloadingBlockQueue.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <iostream>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;

namespace dev
{
namespace test
{
    BOOST_FIXTURE_TEST_SUITE(DownloadingBlockQueueTest, TestOutputHelperFixture)
    
    BOOST_AUTO_TEST_CASE(EmptyTest)
    {
        DownloadingBlockQueue fakeQueue;
        bool isEmpty = fakeQueue.empty();
        BOOST_CHECK(isEmpty == true);

        FakeBlock fakeBlock;
        shared_ptr<Block> blockPtr = make_shared<Block>(fakeBlock.getBlock());
        fakeQueue.push(blockPtr);
        isEmpty = fakeQueue.empty();
        BOOST_CHECK(isEmpty == false);
    }

    BOOST_AUTO_TEST_CASE(PushAndPopTest)
    {
        DownloadingBlockQueue fakeQueue;
        BOOST_CHECK(fakeQueue.size() == 0);

        BlockPtrVec fakeBlocks;
        for(auto i = 0; i < 5; ++i)
        {
            FakeBlock fakeBlock;
            fakeBlock.getBlock().header().setNumber(static_cast<int64_t>(i));
            shared_ptr<Block> blockPtr = make_shared<Block>(fakeBlock.getBlock());
            fakeQueue.push(blockPtr);
        }

        BOOST_CHECK(fakeQueue.size() == 5);
        fakeQueue.popSequent(0, 3);
        BOOST_CHECK(fakeQueue.size() == 2);

        for(auto i = 7; i < 10; ++i)
        {
            FakeBlock fakeBlock;
            fakeBlock.getBlock().header().setNumber(static_cast<int64_t>(i));
            shared_ptr<Block> blockPtr = make_shared<Block>(fakeBlock.getBlock());
            fakeQueue.push(blockPtr);
        }

        BOOST_CHECK(fakeQueue.size() == 5);
        fakeQueue.popSequent(3, 3);
        cout << fakeQueue.size() << endl;
        BOOST_CHECK(fakeQueue.size() == 3);
    }

    BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
