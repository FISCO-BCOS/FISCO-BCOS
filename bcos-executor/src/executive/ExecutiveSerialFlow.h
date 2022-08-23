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
 * @brief Executive flow for serial execution
 * @file ExecutiveSerialFlow.h
 * @author: jimmyshi
 * @date: 2022-07-21
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
class ExecutiveSerialFlow : public virtual ExecutiveFlowInterface,
                            public std::enable_shared_from_this<ExecutiveSerialFlow>
{
public:
    ExecutiveSerialFlow(ExecutiveFactory::Ptr executiveFactory)
      : m_executiveFactory(executiveFactory)
    {}

    virtual ~ExecutiveSerialFlow() {}

    void submit(CallParameters::UniquePtr txInput) override;
    void submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs) override;

    void asyncRun(
        // onTxReturn(output)
        std::function<void(CallParameters::UniquePtr)> onTxReturn,

        // onFinished(success, errorMessage)
        std::function<void(bcos::Error::UniquePtr)> onFinished) override;

    void stop() override
    {
        m_isRunning = false;
        ExecutiveFlowInterface::stop();
    };

private:
    using SerialMap = std::map<int64_t, CallParameters::UniquePtr, std::less<>>;
    using SerialMapPtr = std::shared_ptr<SerialMap>;

    void run(std::function<void(CallParameters::UniquePtr)> onTxReturn,
        std::function<void(bcos::Error::UniquePtr)> onFinished);

    template <class F>
    void asyncTo(F f)
    {
        // call super function
        ExecutiveFlowInterface::asyncTo<ExecutiveSerialFlow::Ptr, F>(
            shared_from_this(), std::move(f));
    }


    // <ContextID> -> Executive
    SerialMapPtr m_txInputs;

    ExecutiveFactory::Ptr m_executiveFactory;

    mutable SharedMutex x_lock;

    bool m_isRunning = true;
};


}  // namespace executor
}  // namespace bcos
