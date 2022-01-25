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
 * @brief : Generate transaction DAG for parallel execution
 * @author: jimmyshi
 * @date: 2019-1-8
 */

#include "TxDAG.h"
#include "CriticalFields.h"
#include <tbb/parallel_for.h>
#include <map>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor::critical;

#define DAG_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG::init(critical::CriticalFieldsInterface::Ptr _txsCriticals, ExecuteTxFunc const& _f)
{
    auto txsSize = _txsCriticals->size();
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("transactionNum", txsSize);

    f_executeTx = _f;
    m_totalParaTxs = _txsCriticals->size();

    // init DAG
    m_dag.init(txsSize);

    // define conflict handler
    auto onConflictHandler = [&](ID pId, ID id) { m_dag.addEdge(pId, id); };
    auto onFirstConflictHandler = [&](ID id) {
        // do nothing
        (void)id;
    };
    auto onEmptyConflictHandler = [&](ID id) {
        // do nothing
        (void)id;
    };
    auto onAllConflictHandler = [&](ID id) {
        // do nothing
        // ignore normal tx, only handle DAG tx, normal tx has been sent back to be executed by DMT
        (void)id;
    };

    // parse criticals
    _txsCriticals->traverseDag(
        onConflictHandler, onFirstConflictHandler, onEmptyConflictHandler, onAllConflictHandler);

    // Generate DAG
    m_dag.generate();

    DAG_LOG(TRACE) << LOG_DESC("End init transaction DAG");
}

void TxDAG::run(unsigned int threadNum)
{
    auto parallelTimeOut = utcSteadyTime() + 30000;  // 30 timeout

    std::atomic<bool> isWarnedTimeout(false);
    tbb::parallel_for(tbb::blocked_range<unsigned int>(0, threadNum),
        [&](const tbb::blocked_range<unsigned int>& _r) {
            (void)_r;

            while (!hasFinished())
            {
                if (!isWarnedTimeout.load() && utcSteadyTime() >= parallelTimeOut)
                {
                    isWarnedTimeout.store(true);
                    EXECUTOR_LOG(WARNING)
                        << LOG_BADGE("executeBlock") << LOG_DESC("Para execute block timeout")
                        << LOG_KV("txNum", m_totalParaTxs);
                }
                executeUnit();
            }
        });
}

int TxDAG::executeUnit()
{
    int exeCnt = 0;
    ID id = m_dag.waitPop();
    while (id != INVALID_ID)
    {
        do
        {
            exeCnt += 1;
            f_executeTx(id);
            id = m_dag.consume(id);
        } while (id != INVALID_ID);
        id = m_dag.waitPop();
    }
    if (exeCnt > 0)
    {
        Guard l(x_exeCnt);
        m_exeCnt += exeCnt;
    }
    return exeCnt;
}
