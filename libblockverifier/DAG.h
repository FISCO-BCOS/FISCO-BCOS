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
#include <libdevcore/Guards.h>
#include <cstdint>
#include <queue>
#include <vector>

namespace dev
{
namespace blockverifier
{
using ID = uint32_t;
using IDs = std::vector<ID>;
static const ID INVALID_ID = (ID(0) - 1);

struct Vertex
{
    ID inDegree;
    std::vector<ID> outEdge;
    mutable dev::SharedMutex vtxLock;
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

    // Pop the top of DAG (thread safe), return INVALID_ID if queue is empty
    ID pop();

    // Consume the top and add new top in top queue (thread safe)
    void consume(ID _id);

    // Clear all data of this class (thread safe)
    void clear();

private:
    std::vector<std::shared_ptr<Vertex>> m_vtxs;
    std::queue<ID> m_topLevel;

private:
    void printVtx(ID _id);
    mutable dev::SharedMutex x_topLevel;
};

}  // namespace blockverifier
}  // namespace dev