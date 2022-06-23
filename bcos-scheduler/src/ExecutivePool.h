/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief The pool for executive states
 * @file ExecutivePool.h
 * @author: jimmyshi
 * @date: 2022-05-05
 */


#pragma once
#include "Common.h"
#include "Executive.h"

#define TBB_PREVIEW_CONCURRENT_ORDERED_CONTAINERS 1
#include <tbb/concurrent_set.h>
#include <tbb/concurrent_unordered_map.h>
#include <string>

namespace bcos::scheduler
{

class ExecutivePool
{
public:
    using ExecutiveStateHandler = std::function<bool(
        ContextID contextID, ExecutiveState::Ptr executiveState)>;  // return needContinue
    using SetPtr = std::shared_ptr<tbb::concurrent_set<ContextID>>;
    enum MessageHint : int8_t
    {
        ALL = 0,
        NEED_PREPARE = 1,
        LOCKED = 2,
        NEED_SEND = 3,
        NEED_SCHEDULE_OUT = 4,
        END = 5
    };

    void add(ContextID contextID, ExecutiveState::Ptr executiveState);
    ExecutiveState::Ptr get(ContextID contextID);
    void markAs(ContextID contextID, MessageHint type);
    void refresh();
    bool empty() { return m_pendingExecutives.empty() && m_hasLocked->empty(); }
    bool empty(MessageHint type);
    void forEach(MessageHint type, ExecutiveStateHandler handler, bool needClear = false);
    void forEachAndClear(MessageHint type, ExecutiveStateHandler handler);

    static std::string toString(MessageHint type)
    {
        switch (type)
        {
        case NEED_PREPARE:
        {
            return "NEED_PREPARE";
        }
        case LOCKED:
        {
            return "LOCKED";
        }
        case NEED_SEND:
        {
            return "NEED_SEND";
        }
        case NEED_SCHEDULE_OUT:
        {
            return "NEED_SCHEDULE_OUT";
        }
        case END:
        {
            return "END";
        }
        case ALL:
        {
            return "ALL";
        }
        }
        return "Unknown";
    }

private:
    tbb::concurrent_unordered_map<ContextID, ExecutiveState::Ptr> m_pendingExecutives;

    SetPtr m_needPrepare = std::make_shared<tbb::concurrent_set<ContextID>>();
    SetPtr m_hasLocked = std::make_shared<tbb::concurrent_set<ContextID>>();
    SetPtr m_needScheduleOut = std::make_shared<tbb::concurrent_set<ContextID>>();
    SetPtr m_needSend = std::make_shared<tbb::concurrent_set<ContextID>>();
    SetPtr m_needRemove = std::make_shared<tbb::concurrent_set<ContextID>>();
};
}  // namespace bcos::scheduler