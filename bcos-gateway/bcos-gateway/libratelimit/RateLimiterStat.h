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

#include "bcos-framework/protocol/Protocol.h"
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
    uint64_t totalDataSize;
    uint64_t lastDataSize;

    uint64_t totalTimes;
    int64_t lastTimes;

    uint64_t totalFailedTimes;
    int64_t lastFailedTimes;

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

    std::optional<std::string> toString(const std::string& _prefix, uint32_t _periodMS) const;
};

class RateLimiterStat : public std::enable_shared_from_this<RateLimiterStat>
{
public:
    const static std::string TOTAL_INCOMING;
    const static std::string TOTAL_OUTGOING;

    using Ptr = std::shared_ptr<RateLimiterStat>;
    using ConstPtr = std::shared_ptr<const RateLimiterStat>;

    void start();
    void stop();

    // ---------------- statistics on inbound and outbound begin -------------------
    void updateInComing0(
        const std::string& _endpoint, uint16_t _pgkType, uint64_t _dataSize, bool _suc);
    void updateInComing(
        const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool _suc);

    void updateOutGoing(const std::string& _endpoint, uint64_t _dataSize, bool _suc);
    void updateOutGoing(
        const std::string& _groupID, uint16_t _moduleID, uint64_t _dataSize, bool _suc);
    // ---------------- statistics on inbound and outbound end -------------------


    static inline std::string toGroupKey(const std::string& _groupID)
    {
        // return " group :  " + _groupID;
        return _groupID;
    }

    static inline std::string toModuleKey(uint16_t _moduleID)
    {
        // return " module : " + protocol::moduleIDToString((protocol::ModuleID)_moduleID);
        return protocol::moduleIDToString((protocol::ModuleID)_moduleID);
    }

    static inline std::string toModuleKey(const std::string& _groupID, uint16_t _moduleID)
    {
        // return " group|module: " + _groupID + "|" +
        //        protocol::moduleIDToString((protocol::ModuleID)_moduleID);
        return _groupID + "|" + protocol::moduleIDToString((protocol::ModuleID)_moduleID);
    }

    static inline std::string toEndpointKey(const std::string& _ep)
    {
        // return " endpoint:  " + _ep;
        return _ep;
    }
    static inline std::string toEndpointPkgTypeKey(const std::string& _ep, uint16_t _pkgType)
    {
        // return " endpoint|pkgType:  " + _ep + "|" + std::to_string(_pkgType);
        return _ep + "|" + std::to_string(_pkgType);
    }

    void flushStat();

    std::pair<std::string, std::string> inAndOutStat(uint32_t _intervalMS);

    const auto& inStat() const { return m_inStat; }
    const auto& outStat() const { return m_outStat; }

    int32_t statInterval() const { return m_statInterval; }
    void setStatInterval(int32_t _statInterval) { m_statInterval = _statInterval; }

    bool enableConnectDebugInfo() const { return m_enableConnectDebugInfo; }
    void setEnableConnectDebugInfo(bool _enableConnectDebugInfo)
    {
        m_enableConnectDebugInfo = _enableConnectDebugInfo;
    }

    bool working() const { return m_running; }

private:
    bool m_running = false;

    // print more debug info
    bool m_enableConnectDebugInfo = true;

    std::mutex m_inLock;
    std::mutex m_outLock;

    // TODO: How to clean up the disconnected connections
    std::unordered_map<std::string, Stat> m_inStat;
    std::unordered_map<std::string, Stat> m_outStat;

    // ratelimiter stat report period, default 1 min
    constexpr static int32_t DEFAULT_STAT_INTERVAL_MS = 60000;
    int32_t m_statInterval{DEFAULT_STAT_INTERVAL_MS};
    // the timer that periodically report the stat
    std::shared_ptr<Timer> m_statTimer;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos