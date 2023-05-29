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
 * @file TimeoutController.cpp
 * @author: chuwen
 * @date 2023-05-15
 */

#include "TimeoutController.h"
#include <bcos-framework/protocol/GlobalConfig.h>

using namespace bcos;
using namespace bcos::gateway;

// default packet timeout
constexpr std::uint32_t DEFAULT_PACKET_TIMEOUT{10000};
// max packet timeout
constexpr std::uint32_t MAX_PACKET_TIMEOUT{30000};
// Count the number of packet timeouts every 5 seconds
constexpr std::int16_t TIMEOUT_RECACULATE_SLICE{5000};

// If there are more than or equal to 10 timeout packets within TIMEOUT_RECACULATE_SLICE, increase the timeout
constexpr std::int16_t TIMEOUT_ADJUST_THRESHOLD{10};
// The timeout is increased by 5 seconds each time
constexpr std::int16_t TIMEOUT_INCREASE_UNIT{5000};
// The timeout is decreased by 500 milliseconds each time
constexpr std::int16_t TIMEOUT_DECREASE_UNIT{500};

TimeoutController::TimeoutController() : Worker("TimeoutController", 1000) {}

void TimeoutController::start()
{
    bcos::Guard guard(x_running);

    if(true == m_running)
    {
        return;
    }

    startWorking();
    m_running = true;
}

void TimeoutController::stop()
{
    bcos::Guard guard(x_running);

    if(false == m_running)
    {
        return;
    }

    finishWorker();
    if(isWorking())
    {
        // stop the worker thread
        stopWorking();
        terminate();
    }

    m_running = false;
}

void TimeoutController::executeWorker()
{
    std::uint64_t endTimestamp = utcSteadyTime();
    std::uint64_t startTimestamp = endTimestamp - TIMEOUT_RECACULATE_SLICE;

    bcos::Guard pointGuard(x_nodeTimeoutPoints);
    for (auto p2pIDIter = m_nodeTimeoutPoints.begin(); p2pIDIter != m_nodeTimeoutPoints.end();
           ++p2pIDIter)
    {
        auto p2pID = p2pIDIter->first;
        for (auto moduleIter = p2pIDIter->second.begin(); moduleIter != p2pIDIter->second.end();
               ++moduleIter)
        {
            std::uint16_t count{0};

            // Collect the number of timeouts within 5 seconds
            while(false == moduleIter->second.empty())
            {
                auto timestamp = moduleIter->second.front();
                if(timestamp >= endTimestamp)
                {
                    break;
                }
                if(timestamp >= startTimestamp)
                {
                    ++count;
                }
                moduleIter->second.pop_front();
            }

            bcos::Guard guard(x_nodePacketTimeout);
            if(count > TIMEOUT_ADJUST_THRESHOLD)
            {
                // The number of timeouts is greater than the threshold
                m_nodePacketTimeout[p2pID][moduleIter->first] += TIMEOUT_INCREASE_UNIT;
                if(m_nodePacketTimeout[p2pID][moduleIter->first] > MAX_PACKET_TIMEOUT)
                {
                    m_nodePacketTimeout[p2pID][moduleIter->first] = MAX_PACKET_TIMEOUT;
                }
            }
            else
            {
                m_nodePacketTimeout[p2pID][moduleIter->first] -= TIMEOUT_DECREASE_UNIT;
                if(m_nodePacketTimeout[p2pID][moduleIter->first] < DEFAULT_PACKET_TIMEOUT)
                {
                    m_nodePacketTimeout[p2pID][moduleIter->first] = DEFAULT_PACKET_TIMEOUT;
                }
            }

            TIMEOUT_CONTROLLER_LOG(DEBUG)
                    << LOG_KV("p2pID", p2pID) << LOG_KV("moduleID", moduleIter->first)
                    << LOG_KV("timeout", m_nodePacketTimeout[p2pID][moduleIter->first]);
        }
    }
}

void TimeoutController::addTimeoutPoint(
      const std::string& _p2pID, const int _moduleID, const std::uint64_t _timestamp)
{
    {
        bcos::Guard guard(x_nodePacketTimeout);
        if(m_nodePacketTimeout[_p2pID].end() == m_nodePacketTimeout[_p2pID].find(_moduleID))
        {
            m_nodePacketTimeout[_p2pID][_moduleID] = DEFAULT_PACKET_TIMEOUT;
        }
    }

    {
        bcos::Guard guard(x_nodeTimeoutPoints);
        m_nodeTimeoutPoints[_p2pID][_moduleID].emplace_back(_timestamp);
    }
}

std::uint32_t TimeoutController::getTimeout(const std::string &_p2pID, const int _moduleID)
{
    bcos::Guard guard(x_nodePacketTimeout);

    auto iter = m_nodePacketTimeout[_p2pID].find(_moduleID);
    if(m_nodePacketTimeout[_p2pID].end() == iter)
    {
        return DEFAULT_PACKET_TIMEOUT;
    }

    return iter->second;
}

void TimeoutController::removeP2pID(const std::string &_p2pID)
{
    {
        bcos::Guard guard(x_nodePacketTimeout);
        auto iter = m_nodePacketTimeout.find(_p2pID);
        if(m_nodePacketTimeout.end() != iter)
        {
            m_nodePacketTimeout.erase(iter);
        }
    }

    {
        bcos::Guard guard(x_nodeTimeoutPoints);
        auto iter = m_nodeTimeoutPoints.find(_p2pID);
        if(m_nodeTimeoutPoints.end() != iter)
        {
            m_nodeTimeoutPoints.erase(iter);
        }
    }

    TIMEOUT_CONTROLLER_LOG(INFO) << LOG_KV("node offline, p2pID", _p2pID);
}