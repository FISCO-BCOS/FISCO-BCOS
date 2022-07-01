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
 * @file BWRateStatistics.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include <bcos-gateway/libratelimit/BWRateStatistics.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

const std::string BWRateStatistics::TOTAL_INCOMING = "total incoming";
const std::string BWRateStatistics::TOTAL_OUTGOING = "total outgoing";

std::string Statistics::toString(const std::string& _prefix)
{
    // TODO: calc bandwidth rate
    return "\t[ " + _prefix + ": |total data: " + std::to_string(totalDataSize.load()) +
           " |last  data: " + std::to_string(lastDataSize.load()) +
           " |total count:" + std::to_string(totalCount.load()) +
           " |last  count:" + std::to_string(lastCount.load()) + " ]";
}

void BWRateStatistics::updateInGoing(const std::string& _endPoint, int64_t _dataSize)
{
    std::string endPointKey = "ip: " + _endPoint;
    std::string totalKey = TOTAL_OUTGOING;

    std::lock_guard<std::mutex> l(m_inLock);

    auto& totalInStat = m_inStat[totalKey];
    auto& epInStat = m_inStat[endPointKey];

    // update total incoming
    totalInStat.update(_dataSize);

    // update connection incoming
    epInStat.update(_dataSize);
}

void BWRateStatistics::updateOutGoing(const std::string& _endPoint, int64_t _dataSize)
{
    std::string endPointKey = "ip: " + _endPoint;
    std::string totalKey = TOTAL_OUTGOING;

    std::lock_guard<std::mutex> l(m_outLock);

    auto& totalOutStat = m_outStat[totalKey];
    auto& epOutStat = m_outStat[endPointKey];

    // update total outgoing
    totalOutStat.update(_dataSize);

    // update connection outgoing
    epOutStat.update(_dataSize);
}

void BWRateStatistics::updateInGoing(
    const std::string& _groupID, uint16_t _moduleID, int64_t _dataSize)
{
    if (!_groupID.empty())
    {
        std::string groupKey = "group: " + _groupID;
        std::lock_guard<std::mutex> l(m_inLock);

        auto& groupInStat = m_inStat[groupKey];
        groupInStat.update(_dataSize);
    }

    if (_moduleID != 0)
    {
        std::string moduleKey = "module: " + std::to_string(_moduleID);
        std::lock_guard<std::mutex> l(m_inLock);

        auto& moduleInStat = m_inStat[moduleKey];
        moduleInStat.update(_dataSize);
    }
}

void BWRateStatistics::updateOutGoing(
    const std::string& _groupID, uint16_t _moduleID, int64_t _dataSize)
{
    if (!_groupID.empty())
    {
        std::string groupKey = "group: " + _groupID;
        std::lock_guard<std::mutex> l(m_outLock);

        auto& groupOutStat = m_outStat[groupKey];
        groupOutStat.update(_dataSize);
    }

    if (_moduleID != 0)
    {
        std::string moduleKey = "module: " + std::to_string(_moduleID);
        std::lock_guard<std::mutex> l(m_outLock);

        auto& moduleOutStat = m_outStat[moduleKey];
        moduleOutStat.update(_dataSize);
    }
}

void BWRateStatistics::flushData()
{
    {
        std::lock_guard<std::mutex> l(m_inLock);
        for (auto& [k, s] : m_inStat)
        {
            s.reset();
        }
    }

    {
        std::lock_guard<std::mutex> l(m_outLock);
        for (auto& [k, s] : m_outStat)
        {
            s.reset();
        }
    }
}

std::string BWRateStatistics::toString()
{
    std::string result;

    {
        std::lock_guard<std::mutex> l(m_inLock);
        for (auto& [k, s] : m_inStat)
        {
            result += s.toString(k);
        }
    }

    {
        std::lock_guard<std::mutex> l(m_outLock);
        for (auto& [k, s] : m_outStat)
        {
            result += s.toString(k);
        }
    }

    return result;
}
