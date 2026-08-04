/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "dag/DAG.h"
#include <boost/test/unit_test.hpp>
#include <set>

using namespace bcos::executor;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(DAGTest)

BOOST_AUTO_TEST_CASE(linearChainConsumesInOrder)
{
    // 0 -> 1 -> 2 : a strict chain, must come out in order.
    DAG dag;
    dag.init(3);
    dag.addEdge(0, 1);
    dag.addEdge(1, 2);
    dag.generate();

    auto first = dag.waitPop(false);
    BOOST_CHECK_EQUAL(first, 0U);

    auto second = dag.consume(0);  // completing 0 unlocks 1
    BOOST_CHECK_EQUAL(second, 1U);

    auto third = dag.consume(1);  // completing 1 unlocks 2
    BOOST_CHECK_EQUAL(third, 2U);

    // After consuming the last, the DAG is exhausted.
    BOOST_CHECK_EQUAL(dag.consume(2), INVALID_ID);
    BOOST_CHECK_EQUAL(dag.waitPop(false), INVALID_ID);
}

BOOST_AUTO_TEST_CASE(independentVerticesAllTopLevel)
{
    // No edges → all three are independently runnable from the start.
    DAG dag;
    dag.init(3);
    dag.generate();

    std::set<ID> popped;
    popped.insert(dag.waitPop(false));
    popped.insert(dag.waitPop(false));
    popped.insert(dag.waitPop(false));
    BOOST_CHECK_EQUAL(popped.size(), 3U);
    BOOST_CHECK(popped.count(0) && popped.count(1) && popped.count(2));
}

BOOST_AUTO_TEST_CASE(diamondDependency)
{
    // 0 -> 1, 0 -> 2, 1 -> 3, 2 -> 3 (diamond). 0 first, 3 last.
    DAG dag;
    dag.init(4);
    dag.addEdge(0, 1);
    dag.addEdge(0, 2);
    dag.addEdge(1, 3);
    dag.addEdge(2, 3);
    dag.generate();

    BOOST_CHECK_EQUAL(dag.waitPop(false), 0U);
    // Consuming 0 unlocks 1 and 2 (one returned, the other queued).
    auto afterZero = dag.consume(0);
    BOOST_CHECK(afterZero == 1U || afterZero == 2U);
}

BOOST_AUTO_TEST_CASE(clearIsSafeAfterGenerate)
{
    DAG dag;
    dag.init(2);
    dag.addEdge(0, 1);
    dag.generate();
    // clear() releases the internal vertex/edge state without crashing.
    BOOST_REQUIRE_NO_THROW(dag.clear());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
