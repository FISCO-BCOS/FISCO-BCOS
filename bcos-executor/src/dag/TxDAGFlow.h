/**
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
 * @file TxDAGFlow.h
 * @author: Jimmy Shi
 * @date 2023/1/31
 */

#pragma once
#include "./CriticalFields.h"
#include "./TxDAGInterface.h"
#include "tbb/flow_graph.h"
#include <vector>

#define DAGFLOW_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR") << LOG_BADGE("DAGFlow")

namespace bcos
{
namespace executor
{


class FlowTask
{
public:
    enum Type
    {
        Normal,
        DAG
    };

    using Ptr = std::shared_ptr<FlowTask>;
    FlowTask(ExecuteTxFunc _f) : f_executeTx(_f){};
    virtual ~FlowTask() = default;
    virtual void run() = 0;
    virtual Type type() = 0;

protected:
    ExecuteTxFunc f_executeTx;
};

class NormalTxTask : public FlowTask
{
public:
    NormalTxTask(ExecuteTxFunc _f, critical::ID id) : FlowTask(_f), m_id(id) {}
    ~NormalTxTask() override = default;
    void run() override
    {
        DAGFLOW_LOG(TRACE) << "Task run: NormalTxTask";
        f_executeTx(m_id);
    };
    Type type() override { return Type::Normal; }

private:
    critical::ID m_id;
};

class DagTxsTask : public FlowTask
{
    using Task = tbb::flow::continue_node<tbb::flow::continue_msg>;
    using Msg = const tbb::flow::continue_msg&;

public:
    DagTxsTask(ExecuteTxFunc _f) : FlowTask(_f), m_startTask(m_dag) {}
    ~DagTxsTask() override = default;
    void run() override;
    Type type() override { return Type::DAG; }
    void makeEdge(critical::ID from, critical::ID to);
    void makeVertex(critical::ID id);

private:
    tbb::flow::graph m_dag;
    tbb::flow::broadcast_node<tbb::flow::continue_msg> m_startTask;
    std::map<critical::ID, Task> m_tasks = std::map<critical::ID, Task>();
};

class TxDAGFlow : public virtual TxDAGInterface
{
    using Task = tbb::flow::continue_node<tbb::flow::continue_msg>;

public:
    using Ptr = std::shared_ptr<TxDAGFlow>;

    TxDAGFlow() {}

    virtual ~TxDAGFlow() {}

    void setExecuteTxFunc(ExecuteTxFunc const& _f) override { f_executeTx = _f; };
    void init(critical::CriticalFieldsInterface::Ptr _txsCriticals) override;

    void run(unsigned int threadNum) override;
    void pause() { m_paused = true; };

    bool hasFinished() { return currentTaskItr > m_tasks.size(); }

    bool isDagTx(critical::ID id) { return m_txsCriticals->contains(id); }

private:
    void runExecuteTxFunc(uint32_t id) { f_executeTx(id); }

    ExecuteTxFunc f_executeTx;

    std::vector<FlowTask::Ptr> m_tasks;
    critical::CriticalFieldsInterface::Ptr m_txsCriticals;
    size_t currentTaskItr = 0;
    bool m_paused = false;
};

}  // namespace executor
}  // namespace bcos
