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

#include "DAG.h"
using namespace std;
using namespace bcos;
using namespace bcos::executor;

DAG::~DAG()
{
    clear();
}

void DAG::init(ID _maxSize)
{
    clear();
    for (ID i = 0; i < _maxSize; ++i)
        m_vtxs.emplace_back(make_shared<Vertex>());
    m_totalVtxs = _maxSize;
    m_totalConsume = 0;
}

void DAG::addEdge(ID _f, ID _t)
{
    if (_f >= m_vtxs.size() && _t >= m_vtxs.size())
        return;
    m_vtxs[_f]->outEdge.emplace_back(_t);
    m_vtxs[_t]->inDegree += 1;
    // PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("Add edge") << LOG_KV("from", _f)
    //                << LOG_KV("to", _t);
}

void DAG::generate()
{
    for (ID id = 0; id < m_vtxs.size(); ++id)
    {
        if (m_vtxs[id]->inDegree == 0)
            m_topLevel.push(id);
    }

    // PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("generate")
    //                << LOG_KV("queueSize", m_topLevel.size());
    // for (ID id = 0; id < m_vtxs.size(); id++)
    // printVtx(id);
}

ID DAG::waitPop(bool _needWait)
{
    // Note: concurrent_queue of TBB can't be used with boost::conditional_variable
    //       the try_pop will already be false
    std::unique_lock<std::mutex> ul(x_topLevel);
    ID top = INVALID_ID;
    cv_topLevel.wait_for(ul, std::chrono::milliseconds(10), [&]() {
        auto has = m_topLevel.try_pop(top);
        if (has)
        {
            return true;
        }
        else if (m_totalConsume >= m_totalVtxs)
        {
            return true;
        }
        else if (!_needWait)
        {
            return true;
        }
        // process-exit related:
        // if the m_stop is true (may be the storage has exceptioned)
        // return true, will pop INVALID_ID
        else if (m_stop.load())
        {
            return true;
        }

        return false;
    });
    return top;
}

ID DAG::consume(ID _id)
{
    ID producedNum = 0;
    ID nextId = INVALID_ID;
    ID lastDegree = INVALID_ID;
    for (ID id : m_vtxs[_id]->outEdge)
    {
        auto vtx = m_vtxs[id];
        {
            lastDegree = vtx->inDegree.fetch_sub(1);
        }
        if (lastDegree == 1)
        {
            ++producedNum;
            if (producedNum == 1)
            {
                nextId = id;
            }
            else
            {
                m_topLevel.push(id);
                cv_topLevel.notify_one();
            }
        }
    }

    if (m_totalConsume.fetch_add(1) + 1 == m_totalVtxs)
    {
        cv_topLevel.notify_all();
    }
    // PARA_LOG(TRACE) << LOG_BADGE("TbbCqDAG") << LOG_DESC("consumed")
    //                << LOG_KV("queueSize", m_topLevel.size());
    // for (ID id = 0; id < m_vtxs.size(); id++)
    // printVtx(id);
    return nextId;
}

void DAG::clear()
{
    m_vtxs = std::vector<std::shared_ptr<Vertex>>();
    // XXXX m_topLevel.clear();
}

void DAG::printVtx(ID _id)
{
    for (ID id : m_vtxs[_id]->outEdge)
    {
        PARA_LOG(TRACE) << LOG_BADGE("DAG") << LOG_DESC("VertexEdge") << LOG_KV("ID", _id)
                        << LOG_KV("inDegree", m_vtxs[_id]->inDegree) << LOG_KV("edge", id);
    }
}