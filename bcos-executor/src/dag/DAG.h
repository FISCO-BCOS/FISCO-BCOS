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
 * @brief : DAG(Directed Acyclic Graph) basic implement
 * @author: jimmyshi
 * @date: 2019-1-8
 */


#pragma once
#include "../Common.h"
#include "bcos-framework/libutilities/Common.h"
#include <tbb/concurrent_queue.h>
#include <condition_variable>
#include <cstdint>
#include <queue>
#include <thread>
#include <vector>

namespace bcos
{
namespace executor
{
using ID = uint32_t;
using IDs = std::vector<ID>;
static const ID INVALID_ID = (ID(0) - 1);

struct Vertex
{
    std::atomic<ID> inDegree;
    std::vector<ID> outEdge;
};

class DAG
{
    // Just algorithm, not thread safe
public:
    DAG(){};
    ~DAG();

    // Init DAG basic memory, should call before other function
    // _maxSize is max ID + 1
    void init(ID _maxSize);

    // Add edge between vertex
    void addEdge(ID _f, ID _t);

    // Generate DAG
    void generate();

    // Wait until topLevel is not empty, return INVALID_ID if DAG reach the end
    ID waitPop(bool _needWait = true);

    // Consume the top and add new top in top queue (thread safe)
    ID consume(ID _id);

    // Clear all data of this class (thread safe)
    void clear();

private:
    std::vector<std::shared_ptr<Vertex>> m_vtxs;
    tbb::concurrent_queue<ID> m_topLevel;

    ID m_totalVtxs = 0;
    std::atomic<ID> m_totalConsume;

private:
    void printVtx(ID _id);
    mutable std::mutex x_topLevel;
    std::condition_variable cv_topLevel;
    std::atomic_bool m_stop = {false};

};

}  // namespace executor
}  // namespace bcos