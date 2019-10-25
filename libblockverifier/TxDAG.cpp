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
#include <tbb/parallel_for.h>
#include <map>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;

#define DAG_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("DAG")

// Generate DAG according with given transactions
void TxDAG::init(
    ExecutiveContext::Ptr _ctx, std::shared_ptr<dev::eth::Transactions> _txs, int64_t _blockHeight)
{
    DAG_LOG(TRACE) << LOG_DESC("Begin init transaction DAG") << LOG_KV("blockHeight", _blockHeight)
                   << LOG_KV("transactionNum", _txs->size());

    m_txs = _txs;
    m_dag.init(_txs->size());

    // get criticals
    std::vector<std::shared_ptr<std::vector<std::string>>> txsCriticals;
    auto txsSize = _txs->size();
    txsCriticals.resize(txsSize);
    tbb::parallel_for(
        tbb::blocked_range<uint64_t>(0, txsSize), [&](const tbb::blocked_range<uint64_t>& range) {
            for (uint64_t i = range.begin(); i < range.end(); i++)
            {
                auto& tx = (*_txs)[i];
                txsCriticals[i] = _ctx->getTxCriticals(*tx);
            }
        });

    CriticalField<string> latestCriticals;

    for (ID id = 0; id < txsSize; ++id)
    {
#if 0
        auto& tx = _txs[id];
        // Is para transaction?
        auto criticals = _ctx->getTxCriticals(tx);
#endif
        auto criticals = txsCriticals[id];
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

int TxDAG::executeUnit(Executive::Ptr _executive)
{
    // PARA_LOG(TRACE) << LOG_DESC("executeUnit") << LOG_KV("exeCnt", m_exeCnt)
    //              << LOG_KV("total", m_txs->size());
    int exeCnt = 0;
    ID id = m_dag.waitPop();
    while (id != INVALID_ID)
    {
        do
        {
            exeCnt += 1;
            f_executeTx((*m_txs)[id], id, _executive);
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
