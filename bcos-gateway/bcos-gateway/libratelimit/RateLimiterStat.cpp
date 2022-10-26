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

const std::string RateLimiterStat::TOTAL_INCOMING = " total    ";
const std::string RateLimiterStat::TOTAL_OUTGOING = " total    ";

double Stat::calcAvgRate(uint64_t _data, uint32_t _periodMS)
{
    auto avgRate = (double)_data * 8 * 1000 / 1024 / 1024 / _periodMS;
    return avgRate;
}

std::optional<std::string> Stat::toString(const std::string& _prefix, uint32_t _periodMS)
{
    if (lastDataSize.load() == 0)
    {
        return std::nullopt;
    }

    auto avgRate = calcAvgRate(lastDataSize.load(), _periodMS);

    std::stringstream ss;

    ss << " \t[" << _prefix << "] "
       << " \t"
       << " |total data: " << totalDataSize.load() << " |last data: " << lastDataSize.load()
       << " |total times: " << totalTimes.load() << " |last times: " << lastTimes.load()
       << " |total failed times: " << totalFailedTimes.load()
       << " |last failed times: " << lastFailedTimes.load() << " |avg rate(Mb/s): ";

    ss << std::fixed << std::setprecision(2) << avgRate;

    return ss.str();
}

void RateLimiterStat::start()
{
    if (m_running)
    {
        RATELIMIT_LOG(INFO) << LOG_BADGE("RateLimiterStat")
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
        GATEWAY_LOG(INFO) << LOG_DESC("\n [ratelimiter stat]") << LOG_DESC(io.first);
        GATEWAY_LOG(INFO) << LOG_DESC("\n [ratelimiter stat]") << LOG_DESC(io.second);
        rateLimiterStat->flushStat();
        statTimer->restart();
    });

    m_statTimer->start();

    RATELIMIT_LOG(INFO) << LOG_BADGE("RateLimiterStat") << LOG_DESC("ratelimiter stat start ok")
                        << LOG_KV("statInterval", statInterval);
}

void RateLimiterStat::stop()
{
    if (!m_running)
    {
        RATELIMIT_LOG(INFO) << LOG_BADGE("RateLimiterStat")
                            << LOG_DESC("ratelimiter stat has been stopped");
        return;
    }

    m_running = false;
    if (m_statTimer)
    {
        m_statTimer->stop();
    }

    RATELIMIT_LOG(INFO) << LOG_BADGE("RateLimiterStat") << LOG_DESC("ratelimiter stat stop end");
}


std::string RateLimiterStat::toGroupKey(const std::string& _groupID)
{
    return " group :  " + _groupID;
}

std::string RateLimiterStat::toModuleKey(uint16_t _moduleID)
{
    return " module : " + protocol::moduleIDToString((protocol::ModuleID)_moduleID);
}

std::string RateLimiterStat::toEndPointKey(const std::string& _ep)
{
    return " endpoint:  " + _ep;
}

void RateLimiterStat::updateInComing(const std::string& _endpoint, uint64_t _dataSize)
{
    std::string epKey = toEndPointKey(_endpoint);
    std::string totalKey = TOTAL_OUTGOING;

    // RATELIMIT_LOG(DEBUG) << LOG_BADGE("updateInComing") << LOG_KV("endpoint", _endpoint)
    //                     << LOG_KV("dataSize", _dataSize);

    std::lock_guard<std::mutex> lock(m_inLock);

    auto& totalInStat = m_inStat[totalKey];
    auto& epInStat = m_inStat[epKey];

    // update total incoming
    totalInStat.update(_dataSize);

    // update connection incoming
    epInStat.update(_dataSize);
}

void RateLimiterStat::updateOutGoing(const std::string& _endpoint, uint64_t _dataSize, bool suc)
{
    std::string epKey = toEndPointKey(_endpoint);
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
    const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize)
{
    if (_groupID.empty())
    {  // amop
        if (_moduleID != 0)
        {
            std::string moduleKey = toModuleKey(_moduleID);
            std::lock_guard<std::mutex> lock(m_inLock);

            auto& moduleInStat = m_inStat[moduleKey];
            moduleInStat.update(_dataSize);
        }

        return;
    }

    // RATELIMIT_LOG(DEBUG) << LOG_BADGE("updateInComing") << LOG_KV("_groupID", _groupID)
    //                     << LOG_KV("moduleID", _moduleID) << LOG_KV("dataSize", _dataSize);

    std::string groupKey = toGroupKey(_groupID);
    std::lock_guard<std::mutex> lock(m_inLock);

    auto& groupInStat = m_inStat[groupKey];
    groupInStat.update(_dataSize);
}

void RateLimiterStat::updateOutGoing(
    const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool suc)
{
    if (_groupID.empty())
    {
        if (_moduleID != 0)
        {
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
}

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
    std::string in = " <incoming bandwidth> :";
    {
        std::lock_guard<std::mutex> lock(m_inLock);
        for (auto& [k, s] : m_inStat)
        {
            in += "\t\n";

            auto opt = s.toString(k, _intervalMS);
            if (opt.has_value())
            {
                in += opt.value();
            }
        }
    }

    std::string out = " <outgoing bandwidth> :";
    {
        std::lock_guard<std::mutex> lock(m_outLock);
        for (auto& [k, s] : m_outStat)
        {
            out += "\t\n";

            auto opt = s.toString(k, _intervalMS);
            if (opt.has_value())
            {
                out += opt.value();
            }
        }
    }

    return {in, out};
}
