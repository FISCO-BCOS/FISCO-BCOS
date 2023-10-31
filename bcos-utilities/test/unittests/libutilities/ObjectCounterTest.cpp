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
 *
 * @brief Unit tests for the ObjectCounter
 * @file ObjectCounter.cpp
 */
#include "bcos-utilities/ObjectCounter.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
using namespace bcos;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ObjectCounter, TestPromptFixture)

class ObjectCounterA : public bcos::ObjectCounter<ObjectCounterA>
{
};

template <typename T>
class ObjectCounterB : public bcos::ObjectCounter<ObjectCounterB<T>>
{
};

BOOST_AUTO_TEST_CASE(testObjectCounter)
{
    BOOST_CHECK(ObjectCounterA::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterA::createdObjCount() == 0);
    BOOST_CHECK(ObjectCounterA::destroyedObjCount() == 0);

    {
        ObjectCounterA a1;

        BOOST_CHECK(ObjectCounterA::aliveObjCount() == 1);
        BOOST_CHECK(ObjectCounterA::createdObjCount() == 1);
        BOOST_CHECK(ObjectCounterA::destroyedObjCount() == 0);
    }

    BOOST_CHECK(ObjectCounterA::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterA::createdObjCount() == 1);
    BOOST_CHECK(ObjectCounterA::destroyedObjCount() == 1);


    {
        ObjectCounterA a2;
        ObjectCounterA a3;
        ObjectCounterA a4;

        BOOST_CHECK(ObjectCounterA::aliveObjCount() == 3);
        BOOST_CHECK(ObjectCounterA::createdObjCount() == 4);
        BOOST_CHECK(ObjectCounterA::destroyedObjCount() == 1);
    }

    BOOST_CHECK(ObjectCounterA::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterA::createdObjCount() == 4);
    BOOST_CHECK(ObjectCounterA::destroyedObjCount() == 4);
}


BOOST_AUTO_TEST_CASE(testObjectCounterTemplate)
{
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::createdObjCount() == 0);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::destroyedObjCount() == 0);

    {
        ObjectCounterB<ObjectCounterA> b1;

        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::aliveObjCount() == 1);
        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::createdObjCount() == 1);
        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::destroyedObjCount() == 0);
    }

    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::createdObjCount() == 1);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::destroyedObjCount() == 1);


    {
        ObjectCounterB<ObjectCounterA> b2;
        ObjectCounterB<ObjectCounterA> b3;
        ObjectCounterB<ObjectCounterA> b4;

        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::aliveObjCount() == 3);
        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::createdObjCount() == 4);
        BOOST_CHECK(ObjectCounterB<ObjectCounterA>::destroyedObjCount() == 1);
    }

    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::aliveObjCount() == 0);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::createdObjCount() == 4);
    BOOST_CHECK(ObjectCounterB<ObjectCounterA>::destroyedObjCount() == 4);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
