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
            if (i <= 5)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
                input->contextID = i;
                input->seq = 0;
                txInputs->push_back(std::move(input));
            }
            else if (i <= 10)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::KEY_LOCK);
                input->contextID = i;
                input->seq = 1;
                txInputs->push_back(std::move(input));
            }
            else if (i <= 15)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::FINISHED);
                input->contextID = i;
                input->seq = 1;
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
    }
    std::shared_ptr<ExecutiveStackFlow> executiveStackFlow;
    std::shared_ptr<MockExecutiveFactory> executiveFactory;
    std::shared_ptr<ExecutiveState> executiveState;
    std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs =
        make_shared<std::vector<CallParameters::UniquePtr>>();
};

BOOST_FIXTURE_TEST_SUITE(TestExecutiveStackFlow, ExecutiveStackFlowFixture)

BOOST_AUTO_TEST_CASE(RunTest)
{
    EXECUTOR_LOG(DEBUG) << "RunTest begin";
    std::shared_ptr<std::vector<int64_t>> sequence = make_shared<std::vector<int64_t>>();
    ExecutiveStackFlow::Ptr executiveStackFlow =
        std::make_shared<ExecutiveStackFlow>(executiveFactory);
    BOOST_CHECK(executiveStackFlow != nullptr);

    executiveStackFlow->submit(txInputs);
    EXECUTOR_LOG(DEBUG) << "submit 20 transaction success!";
    auto input1 = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
    input1->contextID = 11;
    input1->seq = 0;
    executiveStackFlow->submit(std::move(input1));
    EXECUTOR_LOG(DEBUG) << "submit 1 transaction success!";


    executiveStackFlow->asyncRun(
        // onTxReturn
        [this, sequence](CallParameters::UniquePtr output) {
            EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                                << ",the conntextID is:" << output->contextID;
            sequence->push_back(output->contextID);
        },
        // onFinished
        [this, sequence](bcos::Error::UniquePtr error) {
            if (error != nullptr)
            {
                EXECUTOR_LOG(ERROR)
                    << "ExecutiveFlow asyncRun error: " << LOG_KV("errorCode", error->errorCode())
                    << LOG_KV("errorMessage", error->errorMessage());
                EXECUTOR_LOG(ERROR) << "all transaction perform error, sequence clear!";
                sequence->clear();
                // callback(std::move(error), std::vector<protocol::ExecutionMessage::UniquePtr>());
            }
            else
            {
                EXECUTOR_LOG(DEBUG) << "all transaction perform end.";
                BOOST_CHECK_EQUAL(sequence->size(), 15);
            }
        });

    executiveStackFlow->asyncRun(
        // onTxReturn
        [this, sequence](CallParameters::UniquePtr output) {
            EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                                << ",the conntextID is:" << output->contextID;
            sequence->push_back(output->contextID);
        },
        // onFinished
        [this, sequence](bcos::Error::UniquePtr error) {
            if (error != nullptr)
            {
                EXECUTOR_LOG(ERROR)
                    << "ExecutiveFlow asyncRun error: " << LOG_KV("errorCode", error->errorCode())
                    << LOG_KV("errorMessage", error->errorMessage());
                EXECUTOR_LOG(ERROR) << "all transaction perform error, sequence clear!";
                sequence->clear();
                // callback(std::move(error), std::vector<protocol::ExecutionMessage::UniquePtr>());
            }
            else
            {
                EXECUTOR_LOG(DEBUG) << "all transaction perform end.";
            }
        });

    EXECUTOR_LOG(DEBUG) << "asyncRun end. " << LOG_KV("the sequence size is :", sequence->size());
    bool flag = true;
    for (int i = 0u; i < sequence->size(); ++i)
    {
        if (i <= 10)
        {
            if (sequence->at(i) != 11 + i)
            {
                flag = false;
                break;
            }
        }
        else
        {
            if (sequence->at(i) != i - 9)
            {
                flag = false;
                break;
            }
        }
    }
    // BOOST_CHECK(flag);
}

BOOST_AUTO_TEST_CASE(DagTest)
{
    std::shared_ptr<std::vector<CallParameters::UniquePtr>> DAGtxInputs =
        make_shared<std::vector<CallParameters::UniquePtr>>();

    for (size_t i = 0; i < 3; ++i)
    {
        auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
        input->contextID = i;
        input->seq = 1;
        DAGtxInputs->push_back(std::move(input));
    }

    EXECUTOR_LOG(DEBUG) << "DagTest begin";
    std::shared_ptr<std::vector<int64_t>> sequence = make_shared<std::vector<int64_t>>();
    ExecutiveStackFlow::Ptr executiveStackFlow =
        std::make_shared<ExecutiveStackFlow>(executiveFactory);
    BOOST_CHECK(executiveStackFlow != nullptr);

    executiveStackFlow->submit(DAGtxInputs);
    EXECUTOR_LOG(DEBUG) << "submit 3 transaction success!";
    executiveStackFlow->asyncRun(
        // onTxReturn
        [this, sequence](CallParameters::UniquePtr output) {
            EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                                << ",the conntextID is:" << output->contextID;
            if (output->contextID == 0)
            {
                uint16_t count = 0;
                std::vector<std::string> m_KeyLocks = out->keyLocks;
                for (size_t i = 0; i < m_KeyLocks.size(); ++i)
                {
                    auto value = m_KeyLocks[i].compare("key" + std::to_string(i));
                    BOOST_CHECK_EQUAL(value, 0);
                    count++;
                }
                BOOST_CHECK_EQUAL(count, 1);
                BOOST_CHECK(m_KeyLocks);
            }
            else if (output->contextID == 1)
            {
                uint16_t count = 0;
                std::vector<std::string> m_KeyLocks = out->keyLocks;
                for (size_t i = 0; i < m_KeyLocks.size(); ++i)
                {
                    auto value = m_KeyLocks[i].compare("key" + std::to_string(i));
                    BOOST_CHECK_EQUAL(value, 0);
                    count++;
                }
                BOOST_CHECK_EQUAL(count, 2);
                BOOST_CHECK(output->keyLocks);
                BOOST_CHECK(output->type == CallParameters::KEY_LOCK);
            }
            else if (output->contextID == 2)
            {
                uint16_t count = 0;
                std::vector<std::string> m_KeyLocks = out->keyLocks;
                for (size_t i = 0; i < m_KeyLocks.size(); ++i)
                {
                    sudo value = m_KeyLocks[i].compare("key" + std::to_string(i));
                    BOOST_CHECK_EQUAL(value, 0);
                    count++;
                }
                BOOST_CHECK_EQUAL(count, 3);
                BOOST_CHECK(output->keyLocks);
                BOOST_CHECK(output->type == CallParameters::KEY_LOCK);
            }
            sequence->push_back(output->contextID);
        },
        // onFinished
        [this, sequence](bcos::Error::UniquePtr error) {
            if (error != nullptr)
            {
                EXECUTOR_LOG(ERROR)
                    << "ExecutiveFlow asyncRun error: " << LOG_KV("errorCode", error->errorCode())
                    << LOG_KV("errorMessage", error->errorMessage());
                EXECUTOR_LOG(ERROR) << "all transaction perform error, sequence clear!";
                sequence->clear();
                // callback(std::move(error), std::vector<protocol::ExecutionMessage::UniquePtr>());
            }
            else
            {
                EXECUTOR_LOG(DEBUG) << "all transaction perform end.";
                BOOST_CHECK_EQUAL(sequence->size(), 15);
            }
        });
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
