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


#pragma once
#include "./TxDAGInterface.h"
#include "tbb/flow_graph.h"
#include <vector>


namespace bcos
{
namespace executor
{

class TxDAG2 : public virtual TxDAGInterface
{
    using Task = tbb::flow::continue_node<tbb::flow::continue_msg>;

public:
    TxDAG2() : m_startTask(m_dag) {}

    virtual ~TxDAG2() {}

    void init(
        critical::CriticalFieldsInterface::Ptr _txsCriticals, ExecuteTxFunc const& _f) override;

    void run(unsigned int threadNum) override;

private:
    ExecuteTxFunc f_executeTx;
    tbb::flow::graph m_dag;
    tbb::flow::broadcast_node<tbb::flow::continue_msg> m_startTask;
    std::vector<Task> m_tasks = std::vector<Task>();
    size_t m_totalParaTxs;
};

}  // namespace executor
}  // namespace bcos
