/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
/**
 * @brief : unitest for DAG(Directed Acyclic Graph) basic implement
 * @author: jimmyshi
 * @date: 2019-1-9
 */

#include "executor/DAG.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <set>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct DAGFixture
{
    DAGFixture() {}
};
BOOST_FIXTURE_TEST_SUITE(DAGTest, DAGFixture)

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
}  // namespace bcos
