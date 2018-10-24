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
 * @brief : downloading block queue test
 * @author: catli
 * @date: 2018-10-23
 */

#include <libsync/DownloadingBlockQueue.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <memory>

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
    BlockPtrVec blocks = fakeQueue.popSequent(0, 5);
    BOOST_CHECK(blocks.size() == 0);

    BlockPtrVec fakeBlocks;
    auto pushFunc =
        [&fakeQueue](int start, int end) {
            for (auto i = start; i < end; ++i)
            {
                FakeBlock fakeBlock;
                fakeBlock.getBlock().header().setNumber(static_cast<int64_t>(i));
                shared_ptr<Block> blockPtr = make_shared<Block>(fakeBlock.getBlock());
                fakeQueue.push(blockPtr);
            }
        };

    pushFunc(0, 5);
    // 0, 1, 2, 3, 4 should in queue
    BOOST_CHECK(fakeQueue.size() == 5);
    blocks = fakeQueue.popSequent(0, 3);
    // 3, 4 should in queue
    BOOST_CHECK(fakeQueue.size() == 2);
    BOOST_CHECK(blocks.size() == 3);
    BOOST_CHECK(blocks[0]->header().number() == 0);
    BOOST_CHECK(blocks[1]->header().number() == 1);
    BOOST_CHECK(blocks[2]->header().number() == 2);

    pushFunc(7, 10);
    // 3, 4, 7, 8, 9 should in queue
    BOOST_CHECK(fakeQueue.size() == 5);
    blocks = fakeQueue.popSequent(3, 3);
    // 7, 8, 9 should in queue
    BOOST_CHECK(fakeQueue.size() == 3);
    BOOST_CHECK(blocks.size() == 2);
    BOOST_CHECK(blocks[0]->header().number() == 3);
    BOOST_CHECK(blocks[1]->header().number() == 4);

    blocks = fakeQueue.popSequent(8, 1);
    // 9 should in queue
    BOOST_CHECK(fakeQueue.size() == 1);
    BOOST_CHECK(blocks.size() == 1);
    BOOST_CHECK(blocks[0]->header().number() == 8);

    blocks = fakeQueue.popSequent(9, 1);
    // queue should be empty
    BOOST_CHECK(fakeQueue.size() == 0);
    BOOST_CHECK(blocks.size() == 1);
    BOOST_CHECK(blocks[0]->header().number() == 9);

    pushFunc(0, c_maxDownloadingBlockQueueBufferSize);
    BOOST_CHECK(fakeQueue.size() == c_maxDownloadingBlockQueueBufferSize);
    pushFunc(c_maxDownloadingBlockQueueBufferSize, c_maxDownloadingBlockQueueBufferSize + 1);
    // queue is full
    BOOST_CHECK(fakeQueue.size() == c_maxDownloadingBlockQueueBufferSize);
    blocks = fakeQueue.popSequent(0, c_maxDownloadingBlockQueueBufferSize);
    BOOST_CHECK(fakeQueue.size() == 0);
    BOOST_CHECK(blocks.size() == c_maxDownloadingBlockQueueBufferSize);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
