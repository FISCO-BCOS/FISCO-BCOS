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
using namespace bcos;
using namespace bcos::eth;
using namespace bcos::sync;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(DownloadingBlockQueueTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(AllTest)
{
    DownloadingBlockQueue fakeQueue;
    auto pushFunc = [&fakeQueue](int start, int end) {
        vector<shared_ptr<Block>> blocks;
        for (auto i = start; i < end; ++i)
        {
            FakeBlock fakeBlock;
            fakeBlock.getBlock()->header().setNumber(static_cast<int64_t>(i));
            shared_ptr<Block> blockPtr = make_shared<Block>(*fakeBlock.getBlock());
            blocks.emplace_back(blockPtr);
        }
        fakeQueue.push(blocks);
    };

    // Empty
    BOOST_CHECK(fakeQueue.empty() == true);

    // Push
    pushFunc(0, 5);
    fakeQueue.flushBufferToQueue();
    // 0, 1, 2, 3, 4 should in queue
    BOOST_CHECK(fakeQueue.size() == 5);

    // Push double unit
    pushFunc(0, 1);
    fakeQueue.flushBufferToQueue();
    // 0, 0, 1, 2, 3, 4 should in queue
    BOOST_CHECK(fakeQueue.size() == 6);

    // Top
    // 0, 0, 1, 2, 3, 4 should in queue
    BOOST_CHECK(fakeQueue.top()->header().number() == 0);

    // Pop 1 uint
    // 0, 1, 2, 3, 4 should in queue
    fakeQueue.pop();
    BOOST_CHECK(fakeQueue.top()->header().number() == 0);

    // Pop 2 uint
    // 2, 3, 4 should in queue
    fakeQueue.pop();
    fakeQueue.pop();
    BOOST_CHECK(fakeQueue.top()->header().number() == 2);

    // Clear
    fakeQueue.clear();
    BOOST_CHECK(fakeQueue.top() == nullptr);
    BOOST_CHECK(fakeQueue.size() == 0);

    // buffer full test
    for (size_t i = 0; i < c_maxDownloadingBlockQueueBufferSize; i++)
        pushFunc(0, 1);
    BOOST_CHECK(fakeQueue.size() == c_maxDownloadingBlockQueueBufferSize);
    pushFunc(0, 1);
    BOOST_CHECK(fakeQueue.size() == c_maxDownloadingBlockQueueBufferSize);

    // queue full test
    fakeQueue.clear();
    pushFunc(0, c_maxDownloadingBlockQueueSize);
    fakeQueue.flushBufferToQueue();
    pushFunc(0, c_maxDownloadingBlockQueueSize);
    fakeQueue.flushBufferToQueue();
    cout << "fakeQueue size: " << fakeQueue.size()
         << " c_maxDownloadingBlockQueueSize: " << c_maxDownloadingBlockQueueSize << endl;
    BOOST_CHECK(fakeQueue.size() == c_maxDownloadingBlockQueueSize);

    // all full test
    for (size_t i = 0; i < c_maxDownloadingBlockQueueBufferSize; i++)
        pushFunc(0, 1);
    BOOST_CHECK(
        fakeQueue.size() == c_maxDownloadingBlockQueueSize + c_maxDownloadingBlockQueueBufferSize);
    pushFunc(0, 1);
    BOOST_CHECK(
        fakeQueue.size() == c_maxDownloadingBlockQueueSize + c_maxDownloadingBlockQueueBufferSize);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
