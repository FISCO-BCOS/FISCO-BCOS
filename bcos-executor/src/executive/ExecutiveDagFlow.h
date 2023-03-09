/*
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief Executive flow for DAG execution
 * @file ExecutiveDagFlow.h
 * @author: jimmyshi
 * @date: 2022-07-21
 */

#pragma once


#include "../dag/CriticalFields.h"
#include "../dag/TxDAGFlow.h"
#include "../dag/TxDAGInterface.h"
#include "ExecutiveFactory.h"
#include "ExecutiveFlowInterface.h"
#include "ExecutiveStackFlow.h"
#include "ExecutiveState.h"
#include <tbb/concurrent_unordered_map.h>
#include <gsl/util>
#include <vector>
namespace bcos::executor
{


class ExecutiveDagFlow : public ExecutiveStackFlow
{
public:
    using Ptr = std::shared_ptr<ExecutiveDagFlow>;

    ExecutiveDagFlow(ExecutiveFactory::Ptr executiveFactory,
        std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> abiCache)
      : ExecutiveStackFlow(executiveFactory), m_abiCache(abiCache)
    {}
    virtual ~ExecutiveDagFlow() = default;

    void submit(CallParameters::UniquePtr txInput) override;
    void submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs) override;


    void stop() override
    {
        EXECUTOR_LOG(DEBUG) << "Try to stop ExecutiveDagFlow";
        if (!m_isRunning)
        {
            EXECUTOR_LOG(DEBUG) << "Executor has tried to stop";
            return;
        }

        m_isRunning = false;
        ExecutiveStackFlow::stop();
    };

    void setDagFlowIfNotExists(TxDAGFlow::Ptr dagFlow)
    {
        bcos::RecursiveGuard lock(x_lock);
        if (!m_dagFlow)
        {
            m_dagFlow = dagFlow;
        }
    }

    static TxDAGFlow::Ptr prepareDagFlow(const BlockContext& blockContext,
        ExecutiveFactory::Ptr executiveFactory,
        std::vector<std::unique_ptr<CallParameters>>& inputs,
        std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> abiCache)
    {
        critical::CriticalFieldsInterface::Ptr criticals =
            generateDagCriticals(blockContext, executiveFactory, inputs, abiCache);
        return generateDagFlow(blockContext, criticals);
    }

protected:
    void runOriginFlow(std::function<void(CallParameters::UniquePtr)> onTxReturn) override;

    static critical::CriticalFieldsInterface::Ptr generateDagCriticals(
        const BlockContext& blockContext, ExecutiveFactory::Ptr executiveFactory,
        std::vector<std::unique_ptr<CallParameters>>& inputs,
        std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> abiCache);

    static TxDAGFlow::Ptr generateDagFlow(
        const BlockContext& blockContext, critical::CriticalFieldsInterface::Ptr criticals);

    static std::shared_ptr<std::vector<bytes>> extractConflictFields(const FunctionAbi& functionAbi,
        const CallParameters& params, const BlockContext& blockContext);

    template <class F>
    void asyncTo(F f)
    {
        // call super function
        ExecutiveFlowInterface::asyncTo<F>(std::move(f));
    }

    std::shared_ptr<std::vector<CallParameters::UniquePtr>> m_inputs = nullptr;
    TxDAGFlow::Ptr m_dagFlow;
    std::function<void(CallParameters::UniquePtr)> f_onTxReturn;
    mutable bcos::RecursiveMutex x_lock;

    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> m_abiCache;

    unsigned int m_DAGThreadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
};

}  // namespace bcos::executor
