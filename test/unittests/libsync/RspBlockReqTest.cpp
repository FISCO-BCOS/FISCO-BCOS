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
 * @brief : sync master test
 * @author: jimmyshi
 * @date: 2019-05-29
 */

#include <libsync/Common.h>
#include <libsync/RspBlockReq.h>
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
BOOST_FIXTURE_TEST_SUITE(RspBlockReqTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(PushAndTopTest)
{
    // Example:
    // top[x] (fromNumber, size)    range       merged range    merged tops(fromNumber, size)
    // top[0] (1, 3)                [1, 4)      [1, 4)          (1, 3)
    // top[1] (1, 4)                [1, 5)      [1, 5)          (1, 4)
    // top[2] (2, 1)                [2, 3)      [1, 5)          (1, 4)
    // top[3] (2, 4)                [2, 6)      [1, 6)          (1, 5)
    // top[4] (6, 2)                [6, 8)      [1, 8)          (1, 7)
    // top[5] (10, 2)               [10, 12]    can not merge into (1, 7) leave it for next turn

    DownloadRequestQueue queue(NodeID(100));
    BOOST_CHECK(queue.empty());

    queue.push(1, 3);
    queue.push(1, 4);
    queue.push(2, 1);
    queue.push(2, 4);
    queue.push(6, 2);
    queue.push(10, 2);

    DownloadRequest top = queue.topAndPop();
    BOOST_CHECK_EQUAL(top.fromNumber, 1);
    BOOST_CHECK_EQUAL(top.size, 7);

    BOOST_CHECK(!queue.empty());

    top = queue.topAndPop();
    BOOST_CHECK_EQUAL(top.fromNumber, 10);
    BOOST_CHECK_EQUAL(top.size, 2);

    BOOST_CHECK(queue.empty());
}

BOOST_AUTO_TEST_CASE(disableTest)
{
    DownloadRequestQueue queue(NodeID(100));
    BOOST_CHECK(queue.empty());

#if 0
    queue.disablePush();
    queue.push(1, 3);
    BOOST_CHECK(queue.empty());
#endif

    queue.enablePush();
    queue.push(1, 3);
    BOOST_CHECK(!queue.empty());
}

BOOST_AUTO_TEST_CASE(fullTest)
{
    DownloadRequestQueue queue(NodeID(100));
    BOOST_CHECK(queue.empty());

    for (size_t i = 0; i < c_maxReceivedDownloadRequestPerPeer; i++)
    {
        queue.push(1, 3);
    }
    BOOST_CHECK(!queue.empty());

    queue.push(100, 3);  // must not success
    BOOST_CHECK(!queue.empty());
    queue.topAndPop();
    BOOST_CHECK(queue.empty());  // Only has (1, 3), no (100, 3)
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
