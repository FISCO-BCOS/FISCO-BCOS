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
#include "../mock/MockLedger.h"
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
            if ( i == 1){
                auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
                input->contextID = i;
                input->seq = 0;
                txInputs->push_back(std::move(input));
            }
            else if (i <= 5)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::KEY_LOCK);
                input->contextID = i;
                input->seq = 1;
                txInputs->push_back(std::move(input));
            }
            else if (i <= 10)
            {
                auto input = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
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

        LedgerCache::Ptr ledgerCache =
            std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
        std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(
            nullptr, ledgerCache, nullptr, 0, h256(), 0, 0, FiscoBcosSchedule, false, false);

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
    //    std::shared_ptr<std::vector<int64_t>> sequence = make_shared<std::vector<int64_t>>();
    auto sequence = std::make_shared<tbb::concurrent_vector<int64_t>>();
    ExecutiveStackFlow::Ptr executiveStackFlow =
        std::make_shared<ExecutiveStackFlow>(executiveFactory);
    BOOST_CHECK(executiveStackFlow != nullptr);

    executiveStackFlow->submit(txInputs);
    EXECUTOR_LOG(DEBUG) << "submit 20 transaction success!";

    std::promise<void> finish1;
    std::promise<void> finish2;

    executiveStackFlow->asyncRun(
        // onTxReturn
        [sequence](CallParameters::UniquePtr output) {
          EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                              << ",the conntextID is:" << output->contextID;
          sequence->push_back(output->contextID);
        },
        // onFinished
        [sequence,&finish1](bcos::Error::UniquePtr error) {
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
              BOOST_CHECK_EQUAL(sequence->size(), 19);
          }
          finish1.set_value();
        });

    executiveStackFlow->asyncRun(
        // onTxReturn
        [sequence](CallParameters::UniquePtr output) {
          EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                              << ",the conntextID is:" << output->contextID;
          sequence->push_back(output->contextID);
        },
        // onFinished
        [sequence,&finish2](bcos::Error::UniquePtr error) {
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
          finish2.set_value();
        });

    finish1.get_future().get();
    finish2.get_future().get();

    EXECUTOR_LOG(DEBUG) << "asyncRun end. " << LOG_KV("the sequence size is :", sequence->size());
    bool flag = true;
    for (int64_t i = 0u; i < (int64_t)sequence->size(); ++i)
    {
        if (sequence->at(i) != 20 - i)
        {
            flag = false;
            break;
        }
    }
    BOOST_CHECK(flag);

    //clear pausedPool
    std::promise<void> finish3;
    auto input1 = std::make_unique<CallParameters>(CallParameters::Type::REVERT);
    input1->contextID = 1;
    input1->seq = 1;
    executiveStackFlow->submit(std::move(input1));
    EXECUTOR_LOG(DEBUG) << "submit 1 transaction success!";

    executiveStackFlow->asyncRun(
        // onTxReturn
        [sequence](CallParameters::UniquePtr output) {
          EXECUTOR_LOG(DEBUG) << "one transaction perform success! the seq is :" << output->seq
                              << ",the conntextID is:" << output->contextID;
          sequence->push_back(output->contextID);
        },
        // onFinished
        [sequence,&finish3](bcos::Error::UniquePtr error) {
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
          finish3.set_value();
        });

    finish3.get_future().get();

    EXECUTOR_LOG(DEBUG) << "asyncRun end. " << LOG_KV("the sequence size is :", sequence->size());
    bool flag1 = true;
    for (int64_t i = 0u; i < (int64_t)sequence->size(); ++i)
    {
        if (sequence->at(i) != 20 - i)
        {
            flag1 = false;
            break;
        }
    }
    BOOST_CHECK(sequence->size()==20);
    BOOST_CHECK(flag1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
