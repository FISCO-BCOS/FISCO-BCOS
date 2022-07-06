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
 */

#include "../../../src/CallParameters.h"
#include "../../../src/Common.h"
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
        for (int i = 1; i <= 20; ++i)
        {
            if (i <= 10)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
                input->contextID = i;
                input->seq = 0;
                txInputs->push_back(std::move(input));
            }
            else
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::REVERT);
                input->contextID = i;
                input->seq = 1;
                txInputs->push_back(std::move(input));
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
    std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs;
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveStackFlow, ExecutiveStackFlowFixture)
BOOST_AUTO_TEST_CASE(RunTest)
{
    std::vector<int64_t> sequence;
    executiveStackFlow->submit(txInputs);
    EXECUTOR_LOG(DEBUG) << "submit 20 transaction success!";
    auto input1 = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
    input1->contextID = 21;
    input1->seq = 0;
    executiveStackFlow->submit(std::move(input1));
    EXECUTOR_LOG(DEBUG) << "submit 1 transaction success!";

    executiveStackFlow->asyncRun(
        // onTxReturn
        [this, sequence](CallParameters::UniquePtr output) {
            if (output->seq == 0)
            {
                // callback(BCOS_ERROR_UNIQUE_PTR(
                //              ExecuteError::STOPPED, "TransactionExecutor is not running"),
                //     {});
                EXECUTOR_LOG(DEBUG) << "one transaction perform failed! the seq is :" << output->seq
                                    << ",the conntextID is:" << output->contextID;
                return;
            }
            EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                                << ",the conntextID is:" << output->contextID;
            sequence.push_back(output->contextID);
        },
        // onFinished
        [this, sequence](bcos::Error::UniquePtr error) {
            if (error != nullptr)
            {
                EXECUTOR_LOG(ERROR)
                    << "ExecutiveFlow asyncRun error: " << LOG_KV("errorCode", error->errorCode())
                    << LOG_KV("errorMessage", error->errorMessage());
                sequence.clear();
                // callback(std::move(error), std::vector<protocol::ExecutionMessage::UniquePtr>());
            }
            else
            {
                EXECUTOR_LOG(DEBUG) << "all transaction perform end.";
                // do nothing
            }
        });
    EXECUTOR_LOG(DEBUG) << "asyncRun end. " << LOG_KV("the sequence size is :", sequence.size())
                        << LOG_KV("  the sequence is ", sequence);

    BOOST_CHECK(sequence.size() != 0);
    bool flag = true;
    for (int i = 0; i < sequence.size() - 1; ++i)
    {
        if (i <= 10)
        {
            if (sequence[i] != 11 + i)
            {
                flag = false;
                break;
            }
        }
        else
        {
            if (sequence[i] != i - 9)
            {
                flag = false;
                break;
            }
        }
    }
    if (sequence[20] != 21)
        flag = false;

    BOOST_CHECK(flag);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
