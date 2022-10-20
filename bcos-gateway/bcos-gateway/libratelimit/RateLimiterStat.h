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
 * @file RateLimiterStat.h
 * @author: octopus
 * @date 2022-06-30
 */
#pragma once

#include "bcos-gateway/Common.h"
#include "bcos-utilities/Timer.h"
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

//
struct Stat
{
    std::atomic<uint64_t> totalDataSize;
    std::atomic<uint64_t> lastDataSize;

    std::atomic<uint64_t> totalTimes;
    std::atomic<int64_t> lastTimes;

    std::atomic<uint64_t> totalFailedTimes;
    std::atomic<int64_t> lastFailedTimes;

public:
    void resetLast()
    {
        lastTimes = 0;
        lastDataSize = 0;
        lastFailedTimes = 0;
    }

    void update(uint64_t _dataSize)
    {
        totalTimes++;
        lastTimes++;

        totalDataSize += _dataSize;
        lastDataSize += _dataSize;
    }

    void updateFailed()
    {
        totalFailedTimes++;
        lastFailedTimes++;
    }

    double calcAvgRate(uint64_t _data, uint32_t _periodMS);

    std::optional<std::string> toString(const std::string& _prefix, uint32_t _periodMS);
};

class RateLimiterStat : public std::enable_shared_from_this<RateLimiterStat>
{
public:
    const static std::string TOTAL_INCOMING;
    const static std::string TOTAL_OUTGOING;

public:
    using Ptr = std::shared_ptr<RateLimiterStat>;
    using ConstPtr = std::shared_ptr<const RateLimiterStat>;

public:
    void start();
    void stop();

public:
    void updateInComing(const std::string& _endpoint, uint64_t _dataSize);
    void updateInComing(const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize);

    void updateOutGoing(const std::string& _endpoint, uint64_t _dataSize, bool suc);
    void updateOutGoing(
        const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool suc);

public:
    std::string toGroupKey(const std::string& _groupID);
    std::string toModuleKey(uint16_t _moduleID);
    std::string toEndPointKey(const std::string& _ep);

    void flushStat();

    std::pair<std::string, std::string> inAndOutStat(uint32_t _intervalMS);

public:
    const std::unordered_map<std::string, Stat>& inStat() { return m_inStat; }
    const std::unordered_map<std::string, Stat>& outStat() { return m_outStat; }

    int32_t statInterval() const { return m_statInterval; }
    void setStatInterval(int32_t _statInterval) { m_statInterval = _statInterval; }

private:
    bool m_running = false;

    // TODO: How to clean up the disconnected connections
    std::mutex m_inLock;
    std::mutex m_outLock;
    std::unordered_map<std::string, Stat> m_inStat;
    std::unordered_map<std::string, Stat> m_outStat;

    // report period, default 1 min
    int32_t m_statInterval = 60000;
    // the timer that periodically report the stat
    std::shared_ptr<Timer> m_statTimer;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos