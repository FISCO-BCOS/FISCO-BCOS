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
 * @brief Construct a new boost auto test case object for Guards
 *
 * @file Guards.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libutilities/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <thread>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Guards, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testDevGuards)
{
    Mutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        Guard l(mutex);
        count++;
        usleep(1000);  // 1 ms
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    BOOST_CHECK(count == max);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
}

BOOST_AUTO_TEST_CASE(testWriteGuard)
{
    SharedMutex mutex;
    int count = 0;
    int max = 8;

    auto f = [&]() {
        WriteGuard l(mutex);
        count++;
        usleep(1000);  // 1 ms
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;

    BOOST_CHECK((end_time - begin_time) > (uint64_t(max) * 1000));
    BOOST_CHECK(count == max);
}

BOOST_AUTO_TEST_CASE(testRecursiveGuard)
{
    RecursiveMutex mutex;
    int count = 0;
    int max = 8;

    auto f0 = [&]() {
        RecursiveGuard l(mutex);
        count++;
    };

    auto f1 = [&]() {
        RecursiveGuard l(mutex);
        count++;
        usleep(1000);  // 1 ms
        f0();          // recursive
    };

    struct timeval begin;
    gettimeofday(&begin, NULL);

    thread* t = new thread[max];
    for (int i = 0; i < max; i++)
    {
        t[i] = thread(f1);
    }

    for (int i = 0; i < max; i++)
    {
        t[i].join();
    }

    struct timeval end;
    gettimeofday(&end, NULL);

    uint64_t end_time = end.tv_sec * 1000000 + end.tv_usec;
    uint64_t begin_time = begin.tv_sec * 1000000 + begin.tv_usec;
    BOOST_CHECK((end_time - begin_time) >= (uint64_t(max) * 1000));
    BOOST_CHECK(count == 2 * max);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
