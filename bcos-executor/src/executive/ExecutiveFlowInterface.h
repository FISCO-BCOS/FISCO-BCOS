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
#include "bcos-utilities/IOServicePool.h"

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

    virtual void stop()
    {
        // IOServicePool lifecycle is managed by Initializer, no explicit stop needed
    }

    void setThreadPool(bcos::IOServicePool::Ptr pool)
    {
        bcos::RecursiveGuard lock(x_pool);
        m_pool = pool;
    }

protected:
    template <class F>
    void asyncTo(F f)
    {
        getPoolInstance()->post([f = std::move(f)]() { f(); });
    }

private:
    bcos::IOServicePool::Ptr getPoolInstance()
    {
        if (!m_pool)
        {
            // No longer lazily create - pool must be set externally
            throw std::runtime_error("ExecutiveFlow pool not initialized");
        }

        return m_pool;
    }

    bcos::IOServicePool::Ptr m_pool;
    bcos::RecursiveMutex x_pool;
};

}  // namespace executor
}  // namespace bcos
