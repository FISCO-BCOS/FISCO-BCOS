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
 *
 * @brief block level context
 * @file ExecuteOutputs.h
 * @author: jimmyshi
 * @date: 2022-04-02
 */

#pragma once

#include "bcos-executor/src/Common.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include <tbb/concurrent_unordered_map.h>


namespace bcos
{
namespace executor
{
class ExecuteOutputs
{
public:
    using Ptr = std::shared_ptr<ExecuteOutputs>;

    void add(protocol::ExecutionMessage::UniquePtr result)
    {
        auto contextID = result->contextID();
        m_contextID2Result.emplace(contextID, std::move(result));
    }
    std::vector<protocol::ExecutionMessage::UniquePtr> dumpAndClear()
    {
        auto results = std::vector<protocol::ExecutionMessage::UniquePtr>();

        std::set<int64_t, std::less<>> contextIDs;

        for (auto it = m_contextID2Result.begin(); it != m_contextID2Result.end(); it++)
        {
            // we assume that context id is in sequence increasing
            contextIDs.insert(it->first);
        }

        for (auto contextID : contextIDs)
        {
            results.push_back(std::move(m_contextID2Result.at(contextID)));
        }

        m_contextID2Result.clear();
        return results;
    }

    void clear() { m_contextID2Result.clear(); }

private:
    tbb::concurrent_unordered_map<int64_t, protocol::ExecutionMessage::UniquePtr>
        m_contextID2Result;
};
}  // namespace executor
}  // namespace bcos