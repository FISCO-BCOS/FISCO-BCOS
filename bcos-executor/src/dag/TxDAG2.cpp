/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 */
/**
 * @brief : Transaction DAG flowGraph implementation
 * @author: jimmyshi
 * @date: 2022-1-4
 */


#include "TxDAG2.h"
#include "CriticalFields.h"

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace tbb::flow;

#define DAG_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG2::init(critical::CriticalFieldsInterface::Ptr _txsCriticals, ExecuteTxFunc const& _f)
{
    auto txsSize = _txsCriticals->size();
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("transactionNum", txsSize);

    f_executeTx = _f;
    m_totalParaTxs = _txsCriticals->size();

    // Generate tasks in m_tasks
    // Note: we must generate every task in m_tasks before make_edge(). Otherwise, may lead to core.
    using Msg = const continue_msg&;
    std::vector<size_t> id2TaskId(txsSize);
    for (ID id = 0; id < _txsCriticals->size(); ++id)
    {
        if (_txsCriticals->contains(id))
        {
            // generate tasks
            auto task = [this, id](Msg) { f_executeTx(id); };
            auto t = Task(m_dag, std::move(task));
            auto taskId = m_tasks.size();
            m_tasks.push_back(t);

            id2TaskId[id] = taskId;
        }
    }

    // define conflict handler
    auto onConflictHandler = [&](ID pId, ID id) {
        auto pTaskId = id2TaskId[pId];
        auto taskId = id2TaskId[id];
        make_edge(m_tasks[pTaskId], m_tasks[taskId]);
    };
    auto onFirstConflictHandler = [&](ID id) {
        auto taskId = id2TaskId[id];
        make_edge(m_startTask, m_tasks[taskId]);
    };
    auto onEmptyConflictHandler = [&](ID id) {
        auto taskId = id2TaskId[id];
        make_edge(m_startTask, m_tasks[taskId]);
    };
    auto onAllConflictHandler = [&](ID id) {
        // do nothing
        // ignore normal tx, only handle DAG tx, normal tx has been sent back to be executed by DMT
        (void)id;
    };

    // parse criticals
    _txsCriticals->parse(
        onConflictHandler, onFirstConflictHandler, onEmptyConflictHandler, onAllConflictHandler);

    DAG_LOG(TRACE) << LOG_DESC("End init transaction DAG");
}

void TxDAG2::run(unsigned int threadNum)
{
    // TODO: add timeout logic
    (void)threadNum;
    m_startTask.try_put(continue_msg());
    m_dag.wait_for_all();
}