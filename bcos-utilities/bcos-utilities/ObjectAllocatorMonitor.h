/**
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
 * @file ObjectAllocatorMonitor.h
 * @author: octopuswang
 * @date 2023-01-30
 */
#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"
#include <memory>

namespace bcos
{

// template class declaration
template <typename... ARGS>
struct ObjectAllocStatToString
{
};

// template class specialization
template <>
struct ObjectAllocStatToString<>
{
    static std::string toString(uint32_t aliveThreshold = 0, const std::string& _delimiter = " ")
    {
        return "";
    }
};

template <typename TYPE, typename... ARGS>
struct ObjectAllocStatToString<TYPE, ARGS...> : public ObjectAllocStatToString<ARGS...>
{
    static std::string toString(uint32_t aliveThreshold = 0, const std::string& _delimiter = " ")
    {
        auto createdObjCount = TYPE::createdObjCount();
        auto destroyedObjCount = TYPE::destroyedObjCount();
        auto aliveObjCount = TYPE::aliveObjCount();

        if (aliveObjCount < aliveThreshold /** Exclude global object and single instance objects */)
        {
            return ObjectAllocStatToString<ARGS...>::toString(aliveThreshold, _delimiter);
        }

        return std::string("[ type: ") + boost::core::demangle(typeid(TYPE).name()) +
               " ,created: " + std::to_string(createdObjCount) +
               " ,destroyed: " + std::to_string(destroyedObjCount) +
               " ,alive: " + std::to_string(aliveObjCount) + " ]" + _delimiter +
               ObjectAllocStatToString<ARGS...>::toString(aliveThreshold, _delimiter);
    }
};

class ObjectAllocatorMonitor
{
public:
    using Ptr = std::shared_ptr<ObjectAllocatorMonitor>;
    using ConstPtr = std::shared_ptr<const ObjectAllocatorMonitor>;

    ObjectAllocatorMonitor() = default;
    ~ObjectAllocatorMonitor() { stopMonitor(); }

    ObjectAllocatorMonitor(const ObjectAllocatorMonitor&) = default;
    ObjectAllocatorMonitor(ObjectAllocatorMonitor&&) = default;
    ObjectAllocatorMonitor& operator=(const ObjectAllocatorMonitor&) = default;
    ObjectAllocatorMonitor& operator=(ObjectAllocatorMonitor&&) = default;

    template <typename... ARGS>
    void startMonitor(uint32_t aliveThreshold = 0, uint32_t _monitorPeriodMS = 60000)
    {
        if (m_run)
        {
            return;
        }
        m_run = true;

        constexpr uint32_t MIN_OBJ_ALLOC_MONITOR_PERIOD_MS = 60000;
        m_timer = std::make_shared<Timer>(
            std::max(_monitorPeriodMS, MIN_OBJ_ALLOC_MONITOR_PERIOD_MS), "objMonitor");

        auto weakPtrTimer = std::weak_ptr<Timer>(m_timer);
        m_timer->registerTimeoutHandler([aliveThreshold, weakPtrTimer]() {
            auto timer = weakPtrTimer.lock();
            if (!timer)
            {
                return;
            }

            auto objStatString = ObjectAllocStatToString<ARGS...>::toString(0);
            auto aliveObjStatString = ObjectAllocStatToString<ARGS...>::toString(aliveThreshold);

            BCOS_LOG(DEBUG) << LOG_BADGE("startMonitor") << LOG_BADGE("ObjectMonitor")
                            << LOG_KV("ObjectAllocStat", objStatString);
            BCOS_LOG(INFO) << LOG_BADGE("startMonitor") << LOG_BADGE("AliveObjectMonitor")
                           << LOG_KV("AliveObjectAllocStat", aliveObjStatString);
            timer->restart();
        });
        m_timer->start();
    }

    void stopMonitor()
    {
        if (!m_run)
        {
            return;
        }
        m_run = false;

        if (m_timer)
        {
            m_timer->stop();
        }
    }

private:
    bool m_run{false};
    std::shared_ptr<Timer> m_timer;
};

}  // namespace bcos
