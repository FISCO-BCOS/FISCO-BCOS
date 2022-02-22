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
#include <tbb/parallel_for.h>
#include <map>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

#define DAG_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG::init(size_t count, const std::vector<std::vector<std::string>>& _txsCriticals)
{
    auto txsSize = count;
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("transactionNum", txsSize);
    m_dag.init(txsSize);

    CriticalField<string> latestCriticals;

    for (ID id = 0; id < txsSize; ++id)
    {
        auto criticals = _txsCriticals[id];
        if (!criticals.empty())
        {
            // DAG transaction: Conflict with certain critical fields
            // Get critical field

            // Add edge between critical transaction
            for (string const& c : criticals)
            {
                ID pId = latestCriticals.get(c);
                if (pId != INVALID_ID)
                {
                    m_dag.addEdge(pId, id);  // add DAG edge
                }
            }

            for (string const& c : criticals)
            {
                latestCriticals.update(c, id);
            }
        }
        else
        {
            continue;
        }
    }

    // Generate DAG
    m_dag.generate();

    m_totalParaTxs = txsSize;

    DAG_LOG(TRACE) << LOG_DESC("End init transaction DAG");
}

// Set transaction execution function
void TxDAG::setTxExecuteFunc(ExecuteTxFunc const& _f)
{
    f_executeTx = _f;
}

int TxDAG::executeUnit(const vector<TransactionExecutive::Ptr>& allExecutives,
    vector<std::unique_ptr<CallParameters>>& allCallParameters,
    const std::vector<gsl::index>& allIndex)
{
    int exeCnt = 0;
    ID id = m_dag.waitPop();
    while (id != INVALID_ID)
    {
        do
        {
            exeCnt += 1;
            if (allExecutives[id] && allCallParameters.at(id))
            {
                f_executeTx(allExecutives[id], std::move(allCallParameters.at(id)), allIndex[id]);
            }
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
