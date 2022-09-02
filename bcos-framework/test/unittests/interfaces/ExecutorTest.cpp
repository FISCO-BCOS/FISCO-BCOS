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
 * @brief Unit tests for the Executor
 * @file ExecutorTest.cpp
 */

#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include <boost/test/unit_test.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct ExecutorTestFixture
{
    ParallelTransactionExecutorInterface* executor;

    ExecutorTestFixture() : executor(nullptr) {}

    ~ExecutorTestFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(ExecutorTest, ExecutorTestFixture)

BOOST_AUTO_TEST_CASE(ExecutionParams)
{
    shared_ptr<ExecutionParams> p = nullptr;
}

BOOST_AUTO_TEST_CASE(ExecutionResult)
{
    shared_ptr<ExecutionResult> p = nullptr;
}

BOOST_AUTO_TEST_CASE(TableHash)
{
    shared_ptr<TableHash> p = nullptr;
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
