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
 * @brief interface definition of TransactionFlow
 * @file ExecutiveFlowInterface.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "../CallParameters.h"
#include <bcos-utilities/ThreadPool.h>

namespace bcos
{
namespace executor
{
class ExecutiveFlowInterface
{
public:
    using Ptr = std::shared_ptr<ExecutiveFlowInterface>;

    virtual void submit(CallParameters::UniquePtr txInput) = 0;
    virtual void submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs) = 0;

    virtual void asyncRun(
        // onTxReturn(output)
        std::function<void(CallParameters::UniquePtr)> onTxReturn,

        // onFinished(success, errorMessage)
        std::function<void(bcos::Error::UniquePtr)> onFinished) = 0;


protected:
    template <class S, class F>
    void asyncTo(S self, F f)
    {
        // f();
        getPoolInstance()->enqueue([self = std::move(self), f = std::move(f)]() { f(); });
    }

private:
    bcos::ThreadPool* getPoolInstance()
    {
        static bcos::ThreadPool* m_pool;
        if (!m_pool)
        {
            static bcos::RecursiveMutex x_pool;
            bcos::RecursiveGuard lock(x_pool);
            if (!m_pool)
            {
                m_pool = new bcos::ThreadPool("ExecutiveFlow", std::thread::hardware_concurrency());
            }
        }
        return m_pool;
    }
};

}  // namespace executor
}  // namespace bcos
