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

void consumeAndPush(DAG& _dag, ID _id, set<ID>& _topSet)
{
    ID top = _dag.consume(_id);
    if (top != INVALID_ID)
        _topSet.insert(top);
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
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
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
    topSet.clear();
    consumeAndPush(dag, 0, topSet);
    std::cout << "consume " << 0 << std::endl;
    consumeAndPush(dag, 6, topSet);
    std::cout << "consume " << 6 << std::endl;
    consumeAndPush(dag, 8, topSet);
    std::cout << "consume " << 8 << std::endl;

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
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
    topSet.clear();
    consumeAndPush(dag, 1, topSet);
    std::cout << "consume " << 1 << std::endl;
    consumeAndPush(dag, 3, topSet);
    std::cout << "consume " << 3 << std::endl;
    consumeAndPush(dag, 7, topSet);
    std::cout << "consume " << 7 << std::endl;

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 2));
    topSet.clear();
    consumeAndPush(dag, 2, topSet);
    std::cout << "consume " << 2 << std::endl;

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 4));
    topSet.clear();
    consumeAndPush(dag, 4, topSet);
    std::cout << "consume " << 4 << std::endl;

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
        if (id == INVALID_ID)
        {
            break;
        }
        topSet.insert(id);
    }
    BOOST_CHECK_EQUAL(topSet.size(), 1);
    BOOST_CHECK(have(topSet, 5));
    topSet.clear();
    consumeAndPush(dag, 5, topSet);
    std::cout << "consume " << 5 << std::endl;

    for (int i = 0; i < 9; i++)
    {
        auto id = dag.waitPop(false);
        std::cout << "pop " << id << std::endl;
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
