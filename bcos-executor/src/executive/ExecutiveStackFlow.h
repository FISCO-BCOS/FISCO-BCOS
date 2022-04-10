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

#include "ExecutiveFactory.h"
#include "ExecutiveFlowInterface.h"
#include "ExecutiveState.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <stack>

namespace bcos
{
namespace executor
{

class ExecutiveStackFlow : public virtual ExecutiveFlowInterface,
                           public std::enable_shared_from_this<ExecutiveStackFlow>
{
public:
    ExecutiveStackFlow(ExecutiveFactory::Ptr executiveFactory)
      : m_executiveFactory(executiveFactory)
    {}

    virtual ~ExecutiveStackFlow() {}

    void submit(CallParameters::UniquePtr txInput) override;
    void submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs) override;

    void asyncRun(
        // onTxFinished(output)
        std::function<void(CallParameters::UniquePtr)> onTxFinished,

        // onPaused(pushMessages)
        std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,

        // onFinished(success, errorMessage)
        std::function<void(bcos::Error::UniquePtr)> onFinished) override;

private:
    void run(std::function<void(CallParameters::UniquePtr)> onTxFinished,
        std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
        std::function<void(bcos::Error::UniquePtr)> onFinished);

    template <class F>
    void asyncTo(F f)
    {
        // call super function
        ExecutiveFlowInterface::asyncTo<ExecutiveStackFlow::Ptr, F>(
            shared_from_this(), std::move(f));
    }

    std::stack<ExecutiveState::Ptr> m_runPool;
    std::map<std::tuple<int64_t, int64_t>, ExecutiveState::Ptr> m_executives;
    ExecutiveFactory::Ptr m_executiveFactory;
    mutable SharedMutex x_lock;
};


}  // namespace executor
}  // namespace bcos
