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

#pragma once
#include "../CallParameters.h"
#include "../Common.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "DAG.h"
#include "../executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/protocol/Block.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace bcos
{
namespace executor
{
class TransactionExecutive;
using ExecuteTxFunc = std::function<void(bcos::executor::TransactionExecutive::Ptr,
    bcos::executor::CallParameters::UniquePtr, gsl::index)>;

enum ConflictFieldKind : std::uint8_t
{
    All = 0,
    Len,
    Env,
    Var,
    Const,
};

enum EnvKind : std::uint8_t
{
    Caller = 0,
    Origin,
    Now,
    BlockNumber,
    Addr,
};

class TxDAG
{
public:
    TxDAG() : m_dag() {}
    virtual ~TxDAG() {}

    // Generate DAG according with given transactions
    void init(size_t count, const std::vector<std::vector<std::string>>& _txsCriticals);

    // Set transaction execution function
    void setTxExecuteFunc(ExecuteTxFunc const& _f);

    // Called by thread
    // Has the DAG reach the end?
    // process-exit related:
    // if the m_stop is true(may be the storage has exceptioned), return true
    // directly
    bool hasFinished() { return (m_exeCnt >= m_totalParaTxs) || (m_stop.load()); }

    // Called by thread
    // Execute a unit in DAG
    // This function can be parallel
    int executeUnit(const std::vector<TransactionExecutive::Ptr>& allExecutives,
        std::vector<std::unique_ptr<CallParameters>>& allCallParameters,
        const std::vector<gsl::index>& allIndex);

    ID paraTxsNumber() { return m_totalParaTxs; }

    ID haveExecuteNumber() { return m_exeCnt; }
    void stop() { m_stop.store(true); }

private:
    ExecuteTxFunc f_executeTx;
    bcos::protocol::TransactionsPtr m_transactions;
    DAG m_dag;

    ID m_exeCnt = 0;
    ID m_totalParaTxs = 0;

    mutable std::mutex x_exeCnt;
    std::atomic_bool m_stop = {false};
};

template <typename T>
class CriticalField
{
public:
    ID get(T const& _c)
    {
        auto it = m_criticals.find(_c);
        if (it == m_criticals.end())
        {
            if (m_criticalAll != INVALID_ID)
                return m_criticalAll;
            return INVALID_ID;
        }
        return it->second;
    }

    void update(T const& _c, ID _txId) { m_criticals[_c] = _txId; }

    void foreachField(std::function<void(ID)> _f)
    {
        for (auto const& _fieldAndId : m_criticals)
        {
            _f(_fieldAndId.second);
        }

        if (m_criticalAll != INVALID_ID)
            _f(m_criticalAll);
    }

    void setCriticalAll(ID _id)
    {
        m_criticalAll = _id;
        m_criticals.clear();
    }

private:
    std::map<T, ID> m_criticals;
    ID m_criticalAll = INVALID_ID;
};

}  // namespace executor
}  // namespace bcos
