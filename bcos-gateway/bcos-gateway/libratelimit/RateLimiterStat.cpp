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
 * @file RateLimiterStat.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/libratelimit/RateLimiterStat.h>
#include <boost/lexical_cast.hpp>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

const std::string RateLimiterStat::TOTAL_INCOMING = "total  ";
const std::string RateLimiterStat::TOTAL_OUTGOING = "total  ";

std::optional<std::string> Stat::toString(const std::string& _prefix, uint32_t _periodMS) const
{
    // Notice: no data interaction, ignore, no display
    if ((lastTimes == 0) && (lastFailedTimes == 0))
    {
        return std::nullopt;
    }

    auto avgRate = calcAvgRate(lastDataSize, _periodMS);
    auto avgQPS = calcAvgQPS(lastTimes, _periodMS);

    std::stringstream ss;

    ss << " \t[" << _prefix << "] "
       << " |total data: " << totalDataSize << " |last data: " << lastDataSize
       << " |total times: " << totalTimes << " |last times: " << lastTimes
       << " |total failed times: " << totalFailedTimes << " |last failed times: " << lastFailedTimes
       << " |avg rate(Mb/s): ";

    ss << std::fixed << std::setprecision(2) << avgRate;
    ss << " |avg qps(p/s): " << avgQPS;

    return ss.str();
}

void RateLimiterStat::start()
{
    if (m_running)
    {
        GATEWAY_LOG(INFO) << LOG_BADGE("RateLimiterStat")
                            << LOG_DESC("ratelimiter stat is running");
        return;
    }
    m_running = true;

    m_statTimer = std::make_shared<Timer>(m_statInterval, "ratelimiter_reporter");

    auto statInterval = m_statInterval;
    auto statTimer = m_statTimer;
    auto rateLimiterStatWeakPtr = std::weak_ptr<RateLimiterStat>(shared_from_this());
    m_statTimer->registerTimeoutHandler([statTimer, rateLimiterStatWeakPtr, statInterval]() {
        auto rateLimiterStat = rateLimiterStatWeakPtr.lock();
        if (!rateLimiterStat)
        {
            return;
        }

        auto io = rateLimiterStat->inAndOutStat(statInterval);
        GATEWAY_LOG(INFO) << LOG_DESC(" [ratelimiter stat] ") << LOG_DESC(io.first);
        GATEWAY_LOG(INFO) << LOG_DESC(" [ratelimiter stat] ") << LOG_DESC(io.second);
        rateLimiterStat->flushStat();
        statTimer->restart();
    });

    m_statTimer->start();

    GATEWAY_LOG(INFO) << LOG_BADGE("RateLimiterStat") << LOG_DESC("ratelimiter stat start ok")
                        << LOG_KV("statInterval", statInterval)
                        << LOG_KV("enableConnectDebugInfo", m_enableConnectDebugInfo);
}

void RateLimiterStat::stop()
{
    if (!m_running)
    {
        GATEWAY_LOG(INFO) << LOG_BADGE("RateLimiterStat")
                            << LOG_DESC("ratelimiter stat has been stopped");
        return;
    }

    m_running = false;
    if (m_statTimer)
    {
        m_statTimer->stop();
    }

    GATEWAY_LOG(INFO) << LOG_BADGE("RateLimiterStat") << LOG_DESC("ratelimiter stat stop end");
}

// ---------------- statistics on inbound and outbound begin -------------------
void RateLimiterStat::updateInComing0(
    const std::string& _endpoint, uint16_t _pgkType, uint64_t _dataSize, bool _suc)
{
    if (!working())
    {
        return;
    }

    std::string epKey = toEndpointKey(_endpoint);
    std::string totalKey = TOTAL_OUTGOING;

    // GATEWAY_LOG(DEBUG) << LOG_BADGE("updateInComing") << LOG_KV("endpoint", _endpoint)
    //                     << LOG_KV("dataSize", _dataSize);

    {
        std::lock_guard<std::mutex> lock(m_inLock);

        auto& totalInStat = m_inStat[totalKey];
        auto& epInStat = m_inStat[epKey];
        if (_suc)
        {
            // update total incoming
            totalInStat.update(_dataSize);
            // update connection incoming
            epInStat.update(_dataSize);
        }
        else
        {
            // update total incoming
            totalInStat.updateFailed();
            // update connection incoming
            epInStat.updateFailed();
        }
    }

    if (m_enableConnectDebugInfo)
    {
        std::string epPkgKey = toEndpointPkgTypeKey(_endpoint, _pgkType);

        std::lock_guard<std::mutex> lock(m_inLock);
        auto& epPkgInStat = m_inStat[epPkgKey];

        if (_suc)
        {
            epPkgInStat.update(_dataSize);
        }
        else

        {
            epPkgInStat.updateFailed();
        }
    }
}

void RateLimiterStat::updateOutGoing(const std::string& _endpoint, uint64_t _dataSize, bool suc)
{
    if (!working())
    {
        return;
    }

    std::string epKey = toEndpointKey(_endpoint);
    std::string totalKey = TOTAL_OUTGOING;

    std::lock_guard<std::mutex> lock(m_outLock);
    auto& totalOutStat = m_outStat[totalKey];
    auto& epOutStat = m_outStat[epKey];

    if (suc)
    {
        // update total outgoing
        totalOutStat.update(_dataSize);

        // update connection outgoing
        epOutStat.update(_dataSize);
    }
    else
    {
        totalOutStat.updateFailed();

        epOutStat.updateFailed();
    }
}

void RateLimiterStat::updateInComing(
    const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool _suc)
{
    if (!working())
    {
        return;
    }

    if (_groupID.empty() && (_moduleID != 0))
    {  // amop
        std::string moduleKey = toModuleKey(_moduleID);
        std::lock_guard<std::mutex> lock(m_inLock);

        auto& moduleInStat = m_inStat[moduleKey];

        if (_suc)
        {
            moduleInStat.update(_dataSize);
        }
        else
        {
            moduleInStat.updateFailed();
        }

        return;
    }

    std::string groupKey = toGroupKey(_groupID);

    std::lock_guard<std::mutex> lock(m_inLock);
    auto& groupInStat = m_inStat[groupKey];
    groupInStat.update(_dataSize);

    if (m_enableConnectDebugInfo && (_moduleID != 0))
    {
        std::string key = toModuleKey(_groupID, _moduleID);

        auto& inStat = m_inStat[key];

        if (_suc)
        {
            inStat.update(_dataSize);
        }
        else
        {
            inStat.updateFailed();
        }
    }
}

void RateLimiterStat::updateOutGoing(
    const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool suc)
{
    if (!working())
    {
        return;
    }

    if (_groupID.empty())
    {
        if (_moduleID != 0)
        {  // AMOP
            std::string moduleKey = toModuleKey(_moduleID);
            std::lock_guard<std::mutex> lock(m_outLock);

            auto& moduleOutStat = m_outStat[moduleKey];
            moduleOutStat.update(_dataSize);

            if (suc)
            {
                // update total outgoing
                moduleOutStat.update(_dataSize);
            }
            else
            {
                moduleOutStat.updateFailed();
            }
        }
        else
        {
            // Notice: p2p basic message, do nothing
        }

        return;
    }

    std::string groupKey = toGroupKey(_groupID);
    std::lock_guard<std::mutex> lock(m_outLock);

    auto& groupOutStat = m_outStat[groupKey];
    if (suc)
    {
        // update total outgoing
        groupOutStat.update(_dataSize);
    }
    else
    {
        groupOutStat.updateFailed();
    }

    if (m_enableConnectDebugInfo && (_moduleID != 0))
    {
        std::string key = toModuleKey(_groupID, _moduleID);

        auto& outStat = m_outStat[key];
        if (suc)
        {
            // update total outgoing
            outStat.update(_dataSize);
        }
        else
        {
            outStat.updateFailed();
        }
    }
}
// ---------------- statistics on inbound and outbound end -------------------

void RateLimiterStat::flushStat()
{
    {
        std::lock_guard<std::mutex> lock(m_inLock);
        for (auto& [k, s] : m_inStat)
        {
            s.resetLast();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_outLock);
        for (auto& [k, s] : m_outStat)
        {
            s.resetLast();
        }
    }
}

std::pair<std::string, std::string> RateLimiterStat::inAndOutStat(uint32_t _intervalMS)
{
    std::unordered_map<std::string, Stat> inStat;
    std::unordered_map<std::string, Stat> outStat;

    {
        std::lock_guard<std::mutex> lock(m_inLock);
        inStat = m_inStat;
    }

    {
        std::lock_guard<std::mutex> lock(m_outLock);
        outStat = m_outStat;
    }

    std::map<std::string, Stat> inStatMap{inStat.begin(), inStat.end()};
    std::map<std::string, Stat> outStatMap{outStat.begin(), outStat.end()};

    std::string in = " <incoming>:";
    std::string out = " <outgoing>:";
    {
        for (auto& [k, s] : inStatMap)
        {
            auto opt = s.toString(k, _intervalMS);
            if (opt.has_value())
            {
                in += "\t\n";
                in += opt.value();
            }
        }
    }

    {
        for (auto& [k, s] : outStatMap)
        {
            auto opt = s.toString(k, _intervalMS);
            if (opt.has_value())
            {
                out += "\t\n";
                out += opt.value();
            }
        }
    }

    return {in, out};
}
