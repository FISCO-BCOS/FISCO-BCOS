/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief interface definition of ExecutiveFlow
 * @file ExecutiveStackFlow.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "../../../src/CallParameters.h"
#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveFlowInterface.h"
#include "../../../src/executive/ExecutiveStackFlow.h"
#include "../../../src/executive/ExecutiveState.h"
#include "../mock/MockExecutiveFactory.h"
#include <tbb/concurrent_unordered_map.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <stack>


using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct ExecutiveStackFlowFixture
{
    ExecutiveStackFlowFixture()
    {
        for (int i = 0; i < 20; ++i)
        {
            if (i % 2 == 0)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
                input->contextID = i;
                input->seq = 0;
                txInputs.push_back(std::move(input));
            }
            else
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::REVERT);
                input->contextID = i;
                input->seq = 1;
                txInputs.push_back(std::move(input));
            }
        }

        std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(
            nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);

        executiveFactory = std::make_shared<MockExecutiveFactory>(
            blockContext, nullptr, nullptr, nullptr, nullptr);

        ExecutiveStackFlow::Ptr executiveStackFlow =
            std::make_shared<ExecutiveStackFlow>(executiveFactory);
    }
    std::shared_ptr<ExecutiveStackFlow> executiveStackFlow;
    std::shared_ptr<MockExecutiveFactory> executiveFactory;
    std::shared_ptr<ExecutiveState> executiveState;
    std::vector<CallParameters::UniquePtr> txInputs;
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveStackFlow, ExecutiveStackFlowFixture)
BOOST_AUTO_TEST_CASE(RunTest)
{
    executiveStackFlow->submit(txInputs);
    auto input1 = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
    input1->contextID = 21;
    input1->seq = 0;
    executiveStackFlow->submit(input1);
}
BOOST_AUTO_TEST_SUITE_END()


}  // namespace test
}  // namespace bcos
