/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : Generate transaction DAG for parallel execution
 * @author: jimmyshi
 * @date: 2019-1-8
 */

#include "TxDAG.h"
#include "Common.h"
#include <map>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;

#define DAG_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG::init(
    ExecutiveContext::Ptr _ctx, std::shared_ptr<dev::eth::Transactions> _txs, int64_t _blockHeight)
{
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("blockHeight", _blockHeight)
                   << LOG_KV("transactionNum", _txs->size());

    m_txs = _txs;
    m_dag.init(_txs->size());

    CriticalField<string> latestCriticals;

    for (ID id = 0; id < _txs->size(); ++id)
    {
        auto tx = (*_txs)[id];

        // Is para transaction?
        auto criticals = _ctx->getTxCriticals(*tx);
        if (criticals)
        {
            // DAG transaction: Conflict with certain critical fields
            // Get critical field

            // Add edge between critical transaction
            for (string const& c : *criticals)
            {
                ID pId = latestCriticals.get(c);
                if (pId != INVALID_ID)
                {
                    DAG_LOG(TRACE)
                        << LOG_DESC("Add edge") << LOG_KV("from", pId) << LOG_KV("to", id);
                    m_dag.addEdge(pId, id);  // add DAG edge
                }
            }

            for (string const& c : *criticals)
            {
                latestCriticals.update(c, id);
            }
        }
        else
        {
            // Normal transaction: Conflict with all transaction
            latestCriticals.foreachField([&](std::pair<string, ID> _fieldAndId) {
                ID pId = _fieldAndId.second;
                // Add edge from all critical transaction
                m_dag.addEdge(pId, id);
                return true;
            });

            // set all critical to my id
            latestCriticals.setCriticalAll(id);
        }
    }

    // Generate DAG
    m_dag.generate();

    m_totalParaTxs = _txs->size();

    DAG_LOG(TRACE) << LOG_DESC("End init transaction DAG") << LOG_KV("blockHeight", _blockHeight);
}

// Set transaction execution function
void TxDAG::setTxExecuteFunc(ExecuteTxFunc const& _f)
{
    f_executeTx = _f;
}

int TxDAG::executeUnit()
{
    // PARA_LOG(TRACE) << LOG_DESC("executeUnit") << LOG_KV("exeCnt", m_exeCnt)
    //              << LOG_KV("total", m_txs->size());
    ID id = m_dag.waitPop();
    if (id == INVALID_ID)
        return 0;


    int exeCnt = 0;
    // PARA_LOG(TRACE) << LOG_DESC("executeUnit transaction") << LOG_KV("txid", id);
    // TODO catch execute exception
    while (id != INVALID_ID)
    {
        exeCnt += 1;
        {
            Guard l(x_exeCnt);
            m_exeCnt += 1;
        }
        f_executeTx(*((*m_txs)[id]), id);

        id = m_dag.consume(id);

        // PARA_LOG(TRACE) << LOG_DESC("executeUnit finish") << LOG_KV("exeCnt", m_exeCnt)
        //                << LOG_KV("total", m_txs->size());
    }
    return exeCnt;
}
