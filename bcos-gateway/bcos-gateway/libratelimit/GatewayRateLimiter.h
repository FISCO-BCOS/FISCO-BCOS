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
#include <bcos-utilities/ratelimiter/RateLimiterInterface.h>
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

    GatewayRateLimiter(ratelimiter::RateLimiterManager::Ptr& _rateLimiterManager,
        ratelimiter::RateLimiterStat::Ptr& _rateLimiterStat)
      : m_rateLimiterManager(_rateLimiterManager), m_rateLimiterStat(_rateLimiterStat)
    {}

    GatewayRateLimiter(GatewayRateLimiter&&) = delete;
    GatewayRateLimiter(const GatewayRateLimiter&) = delete;
    GatewayRateLimiter& operator=(const GatewayRateLimiter&) = delete;
    GatewayRateLimiter& operator=(GatewayRateLimiter&&) = delete;

    ~GatewayRateLimiter() { stop(); }

    void start()
    {
        if (m_running)
        {
            GATEWAY_LOG(INFO) << LOG_BADGE("GatewayRateLimiter")
                                << LOG_DESC("gateway ratelimiter is running");
            return;
        }
        m_running = true;
        auto enableOutRateLimit = m_rateLimiterManager->rateLimiterConfig().enableOutRateLimit();
        auto enableInRateLimit = m_rateLimiterManager->rateLimiterConfig().enableInRateLimit();
        bool bStartStat = false;
        if ((enableOutRateLimit || enableInRateLimit) && m_rateLimiterStat)
        {
            m_rateLimiterStat->start();
            bStartStat = true;
        }

        GATEWAY_LOG(INFO) << LOG_BADGE("GatewayRateLimiter")
                            << LOG_DESC("gateway ratelimiter start end")
                            << LOG_KV("bStartStat", bStartStat)
                            << LOG_KV("enableOutRateLimit", enableOutRateLimit)
                            << LOG_KV("enableInRateLimit", enableInRateLimit);
    }

    void stop()
    {
        if (!m_running)
        {
            GATEWAY_LOG(INFO) << LOG_BADGE("GatewayRateLimiter")
                                << LOG_DESC("gateway ratelimiter has been stopped");
            return;
        }

        m_running = false;
        if (m_rateLimiterStat)
        {
            m_rateLimiterStat->stop();
        }

        GATEWAY_LOG(INFO) << LOG_BADGE("GatewayRateLimiter")
                            << LOG_DESC("gateway ratelimiter stop end");
    }

public:
    std::optional<std::string> checkOutGoing(const std::string& _endpoint, uint16_t _pkgType,
        const std::string& _groupID, uint16_t _moduleID, int64_t _msgLength);

    std::optional<std::string> checkInComing(
        const std::string& _endpoint, uint16_t _packageType, int64_t _msgLength, bool /*unused*/);
    std::optional<std::string> checkInComing(
        const std::string& _groupID, uint16_t _moduleID, int64_t _msgLength);

private:
    bool m_running = false;

    ratelimiter::RateLimiterManager::Ptr m_rateLimiterManager;
    ratelimiter::RateLimiterStat::Ptr m_rateLimiterStat;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos