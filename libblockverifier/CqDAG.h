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
 * @brief : DAG(Directed Acyclic Graph) basic implement
 * @author: jimmyshi
 * @date: 2019-1-8
 */


#pragma once
#include "Common.h"
#include "DAG.h"
#include <libdevcore/Guards.h>
#include <libdevcore/concurrent_queue.h>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <thread>
#include <vector>

namespace dev
{
namespace blockverifier
{
struct CqVertex
{
    std::atomic<ID> inDegree;
    std::vector<ID> outEdge;
};

class CqDAG
{
    // Just algorithm, not thread safe
public:
    CqDAG(){};
    ~CqDAG();

    // Init DAG basic memory, should call before other function
    // _maxSize is max ID + 1
    void init(ID _maxSize);

    // Add edge between vertex
    void addEdge(ID _f, ID _t);

    // Generate DAG
    void generate();

    // Pop the top of DAG (thread safe), return INVALID_ID if queue is empty
    ID pop();

    // Wait until topLevel is not empty, return INVALID_ID if DAG reach the end
    ID waitPop();

    // Consume the top and add new top in top queue (thread safe)
    ID consume(ID _id);

    // Clear all data of this class (thread safe)
    void clear();

private:
    std::vector<std::shared_ptr<CqVertex>> m_vtxs;
    concurrent_queue<ID> m_topLevel;

    ID m_totalVtxs = 0;
    ID m_totalConsume = 0;

private:
    void printVtx(ID _id);
    mutable std::mutex x_topLevel;
    std::condition_variable cv_topLevel;
};

}  // namespace blockverifier
}  // namespace dev