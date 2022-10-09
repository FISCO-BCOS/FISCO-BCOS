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
 * @file GatewayRateLimiter.h
 * @author: octopus
 * @date 2022-09-30
 */

#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"
#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-gateway/libratelimit/RateLimiterManager.h>
#include <bcos-gateway/libratelimit/RateLimiterStat.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class GatewayRateLimiter
{
public:
    using Ptr = std::shared_ptr<GatewayRateLimiter>;
    using ConstPtr = std::shared_ptr<const GatewayRateLimiter>;
    using UniquePtr = std::unique_ptr<const GatewayRateLimiter>;

public:
    GatewayRateLimiter(ratelimiter::RateLimiterManager::Ptr _rateLimiterManager,
        ratelimiter::RateLimiterStat::Ptr _rateLimiterStat)
      : m_rateLimiterManager(_rateLimiterManager), m_rateLimiterStat(_rateLimiterStat)
    {}

    GatewayRateLimiter(GatewayRateLimiter&&) = delete;
    GatewayRateLimiter(const GatewayRateLimiter&) = delete;
    GatewayRateLimiter& operator=(const GatewayRateLimiter&) = delete;
    GatewayRateLimiter& operator=(GatewayRateLimiter&&) = delete;

    ~GatewayRateLimiter() { stop(); }

public:
    void start()
    {
        if (m_running)
        {
            RATELIMIT_LOG(INFO) << LOG_DESC("GatewayRateLimiter is running");
            return;
        }
        m_running = true;

        auto statReporterInterval = m_rateLimiterManager->rateLimiterConfig().statReporterInterval;

        m_rateLimiterStatTimer =
            std::make_shared<Timer>(statReporterInterval, "ratelimiter_reporter");
        auto rateLimiterStatTimer = m_rateLimiterStatTimer;
        auto rateLimiterStat = m_rateLimiterStat;

        m_rateLimiterStatTimer->registerTimeoutHandler(
            [rateLimiterStatTimer, statReporterInterval, rateLimiterStat]() {
                auto io = rateLimiterStat->inAndOutStat(statReporterInterval);
                GATEWAY_LOG(INFO) << LOG_DESC("\n [ratelimiter stat]") << LOG_DESC(io.first);
                GATEWAY_LOG(INFO) << LOG_DESC("\n [ratelimiter stat]") << LOG_DESC(io.second);
                rateLimiterStat->flushStat();
                rateLimiterStatTimer->restart();
            });

        RATELIMIT_LOG(INFO) << LOG_DESC("GatewayRateLimiter start ok")
                            << LOG_KV("statReporterInterval", statReporterInterval);
    }

    void stop()
    {
        if (!m_running)
        {
            RATELIMIT_LOG(INFO) << LOG_DESC("GatewayRateLimiter has been stopped");
            return;
        }

        m_running = false;
        if (m_rateLimiterStatTimer)
        {
            m_rateLimiterStatTimer->stop();
        }

        RATELIMIT_LOG(INFO) << LOG_DESC("GatewayRateLimiter stop end");
    }

public:
    std::pair<bool, std::string> checkOutGoing(const std::string& _endpoint,
        const std::string& _groupID, uint16_t _moduleID, uint64_t _msgLength);

    std::pair<bool, std::string> checkInComing(const std::string& _endpoint, uint64_t _msgLength);
    std::pair<bool, std::string> checkInComing(
        const std::string& _groupID, uint16_t _moduleID, uint64_t _msgLength);

private:
    bool m_running = false;

    ratelimiter::RateLimiterManager::Ptr m_rateLimiterManager;
    ratelimiter::RateLimiterStat::Ptr m_rateLimiterStat;
    // the timer that periodically report the stat
    std::shared_ptr<Timer> m_rateLimiterStatTimer;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos