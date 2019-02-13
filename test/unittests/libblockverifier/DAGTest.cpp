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
 * @brief : unitest for DAG(Directed Acyclic Graph) basic implement
 * @author: jimmyshi
 * @date: 2019-1-9
 */

#include <libblockverifier/DAG.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <set>

using namespace std;
using namespace dev;
using namespace dev::blockverifier;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(DAGTest, TestOutputHelperFixture)

bool have(set<ID>& _set, ID _id)
{
    return _set.find(_id) != _set.end();
}

BOOST_AUTO_TEST_CASE(DAGPopConsumeTest)
{
    DAG dag;
    dag.init(9);
    dag.addEdge(0, 1);
    dag.addEdge(1, 2);
    dag.addEdge(4, 5);
    dag.addEdge(2, 4);
    dag.addEdge(3, 4);
    dag.addEdge(0, 3);
    dag.addEdge(6, 7);
    // single 8 vertex

    // BOOST_CHECK(dag.isQueueEmpty());

    dag.generate();
    // BOOST_CHECK(!dag.isQueueEmpty());

    set<ID> topSet;
    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 3);
    BOOST_CHECK(have(topSet, 0));
    BOOST_CHECK(have(topSet, 6));
    BOOST_CHECK(have(topSet, 8));
    dag.consume(0);
    dag.consume(6);
    dag.consume(8);
    topSet.clear();

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 3);
    BOOST_CHECK(have(topSet, 1));
    BOOST_CHECK(have(topSet, 3));
    BOOST_CHECK(have(topSet, 7));
    dag.consume(1);
    dag.consume(3);
    dag.consume(7);
    topSet.clear();

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 2));
    dag.consume(2);
    topSet.clear();

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 4));
    dag.consume(4);
    topSet.clear();

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 5));
    dag.consume(5);
    topSet.clear();

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop();
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 0);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
