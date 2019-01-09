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

#include "DAG.h"
#include "Common.h"

using namespace std;
using namespace dev;
using namespace dev::blockverifier;

DAG::~DAG()
{
    clear();
}

void DAG::init(ID _maxSize)
{
    clear();
    m_vtxs.resize(_maxSize);
}

void DAG::addEdge(ID _f, ID _t)
{
    if (_f >= m_vtxs.size() && _t >= m_vtxs.size())
        return;
    m_vtxs[_f].outEdge.emplace_back(_t);
    m_vtxs[_t].inDegree += 1;
    // PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("Add edge") << LOG_KV("from", _f)
    //                << LOG_KV("to", _t);
}

void DAG::generate()
{
    for (ID id = 0; id < m_vtxs.size(); id++)
    {
        if (m_vtxs[id].inDegree == 0)
            m_topLevel.push(id);
    }

    // PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("generate")
    //                << LOG_KV("queueSize", m_topLevel.size());
    // for (ID id = 0; id < m_vtxs.size(); id++)
    // printVtx(id);
}

bool DAG::isQueueEmpty()
{
    return m_topLevel.empty();
}

ID DAG::pop()
{
    ID top = m_topLevel.front();
    m_topLevel.pop();
    return top;
}

void DAG::consume(ID _id)
{
    for (ID id : m_vtxs[_id].outEdge)
    {
        Vertex& vtx = m_vtxs[id];
        vtx.inDegree -= 1;
        if (vtx.inDegree == 0)
            m_topLevel.push(id);
    }
    // PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("consumed")
    //                << LOG_KV("queueSize", m_topLevel.size());
    // for (ID id = 0; id < m_vtxs.size(); id++)
    // printVtx(id);
}

void DAG::clear()
{
    m_vtxs.clear();
    m_topLevel = queue<ID>();
}

void DAG::printVtx(ID _id)
{
    for (ID id : m_vtxs[_id].outEdge)
    {
        PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("VertexEdge") << LOG_KV("ID", _id)
                        << LOG_KV("inDegree", m_vtxs[_id].inDegree) << LOG_KV("edge", id);
    }
}