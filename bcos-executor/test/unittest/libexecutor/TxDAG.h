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
#include <bcos-executor/src/CallParameters.h>
#include <bcos-executor/src/Common.h>
#include <bcos-executor/src/dag/DAG.h>
#include "bcos-executor/src/dag/TxDAGInterface.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/executor/TransactionExecutor.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/Transaction.h"
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace bcos
{
namespace executor
{
class TransactionExecutive;

class TxDAG : public virtual TxDAGInterface
{
public:
    TxDAG() : m_dag() {}
    virtual ~TxDAG() {}

    // Generate DAG according with given transactions
    void init(
        critical::CriticalFieldsInterface::Ptr _txsCriticals, ExecuteTxFunc const& _f) override;

    void run(unsigned int threadNum) override;

    // Called by thread
    // Has the DAG reach the end?
    // process-exit related:
    // if the m_stop is true(may be the storage has exceptioned), return true
    // directly
    bool hasFinished() { return (m_exeCnt >= m_totalParaTxs) || (m_stop.load()); }

    // Called by thread
    // Execute a unit in DAG
    // This function can be parallel
    int executeUnit();

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

}  // namespace executor
}  // namespace bcos
