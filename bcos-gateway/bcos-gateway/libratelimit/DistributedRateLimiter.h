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
 * @file DistributedRateLimiter.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"
#include <bcos-gateway/libratelimit/RateLimiterInterface.h>
#include <bcos-utilities/Common.h>
#include <sw/redis++/redis++.h>
#include <memory>
#include <mutex>

namespace bcos
{
namespace gateway
{
namespace ratelimiter
{

class DistributedRateLimiter : public RateLimiterInterface,
                               public std::enable_shared_from_this<DistributedRateLimiter>
{
public:
    using Ptr = std::shared_ptr<DistributedRateLimiter>;
    using ConstPtr = std::shared_ptr<const DistributedRateLimiter>;
    using UniquePtr = std::unique_ptr<const DistributedRateLimiter>;

    const static std::string LUA_SCRIPT;
    const static int32_t DEFAULT_LOCAL_CACHE_PERCENT = 15;

public:
    DistributedRateLimiter(std::shared_ptr<sw::redis::Redis> _redis,
        const std::string& _rateLimiterKey, int64_t _maxPermits, int32_t _interval = 1,
        bool _enableLocalCache = true, int32_t _localCachePercent = DEFAULT_LOCAL_CACHE_PERCENT)
      : m_redis(_redis),
        m_rateLimiterKey(_rateLimiterKey),
        m_maxPermits(_maxPermits),
        m_interval(_interval),
        m_enableLocalCache(_enableLocalCache),
        m_localCachePercent(_localCachePercent)

    {
        GATEWAY_LOG(INFO) << LOG_BADGE("DistributedRateLimiter::NEWOBJ")
                          << LOG_DESC("construct distributed rate limiter")
                          << LOG_KV("rateLimiterKey", _rateLimiterKey)
                          << LOG_KV("interval", _interval) << LOG_KV("maxPermits", _maxPermits)
                          << LOG_KV("enableLocalCache", _enableLocalCache)
                          << LOG_KV("localCachePercent", _localCachePercent);

        if (m_enableLocalCache)
        {
            m_clearCacheTimer = std::make_shared<Timer>(_interval * 1000);
            m_clearCacheTimer->registerTimeoutHandler([this]() { refreshLocalCache(); });
            m_clearCacheTimer->start();
        }

        m_statTimer = std::make_shared<Timer>(60000);
        m_statTimer->registerTimeoutHandler([this]() { stat(); });
        m_statTimer->start();
    }

    DistributedRateLimiter(DistributedRateLimiter&&) = delete;
    DistributedRateLimiter(const DistributedRateLimiter&) = delete;
    DistributedRateLimiter& operator=(const DistributedRateLimiter&) = delete;
    DistributedRateLimiter& operator=(DistributedRateLimiter&&) = delete;

    ~DistributedRateLimiter() override
    {
        if (m_clearCacheTimer)
        {
            m_clearCacheTimer->stop();
        }

        if (m_statTimer)
        {
            m_statTimer->stop();
        }
    }

public:
    struct Stat
    {
        std::atomic<int64_t> totalRequestRedis = 0;
        std::atomic<int64_t> lastRequestRedis = 0;

        std::atomic<int64_t> totalRequestRedisFailed = 0;
        std::atomic<int64_t> lastRequestRedisFailed = 0;

        std::atomic<int64_t> totalRequestRedisExp = 0;
        std::atomic<int64_t> lastRequestRedisExp = 0;

        std::atomic<int64_t> totalRequestRedisMore1MS = 0;
        std::atomic<int64_t> lastRequestRedisMore1MS = 0;

        std::atomic<int64_t> lastRequestTotalCostMS = 0;

        void resetLast()
        {
            lastRequestRedis = 0;
            lastRequestRedisFailed = 0;
            lastRequestRedisExp = 0;
            lastRequestRedisMore1MS = 0;
            lastRequestTotalCostMS = 0;
        }

        void updateOk()
        {
            totalRequestRedis++;
            lastRequestRedis++;
        }

        void updateFailed()
        {
            totalRequestRedisFailed++;
            lastRequestRedisFailed++;
        }

        void updateExp()
        {
            totalRequestRedisExp++;
            lastRequestRedisExp++;
        }

        void updateMore1MS()
        {
            totalRequestRedisMore1MS++;
            lastRequestRedisMore1MS++;
        }
    };

public:
    /**
     * @brief acquire permits
     *
     * @param _requiredPermits
     * @return void
     */
    void acquire(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    bool tryAcquire(int64_t _requiredPermits) override;


    /**
     * @brief
     *
     * @return
     */
    void rollback(int64_t _requiredPermits) override;

    /**
     * @brief
     *
     * @param _requiredPermits
     * @param _withLock
     * @return true
     * @return false
     */
    bool tryAcquireLocalCache(int64_t _requiredPermits, bool _withLock = true);

    /**
     * @brief
     *
     * @param _requiredPermits
     * @return true
     * @return false
     */
    int64_t requestRedis(int64_t _requiredPermits);

    /**
     * @brief
     *
     */
    void refreshLocalCache();

    /**
     * @brief
     *
     */
    void stat();

public:
    int64_t maxPermits() const { return m_maxPermits; }
    int64_t interval() const { return m_interval; }
    bool enableLocalCache() const { return m_enableLocalCache; }
    int32_t localCachePercent() const { return m_localCachePercent; }
    std::string rateLimitKey() const { return m_rateLimiterKey; }
    std::shared_ptr<sw::redis::Redis> redis() const { return m_redis; }

private:
    // stat statistics
    Stat m_stat;

    // redis
    std::shared_ptr<sw::redis::Redis> m_redis;
    // key for distributed rate limit
    std::string m_rateLimiterKey;
    // max token acquire in m_interval time
    int64_t m_maxPermits;
    //
    int32_t m_interval = 1;

    // enable local cache for improve perf and reduce latency
    bool m_enableLocalCache = false;
    // lock for m_localCachePermits
    std::mutex x_localCache;
    // local cache percent of m_maxPermits
    int32_t m_localCachePercent = DEFAULT_LOCAL_CACHE_PERCENT;
    // local cache value
    int64_t m_localCachePermits = 0;
    //
    int64_t m_lastFailedPermit = 0;
    // clear local cache info periodically
    std::shared_ptr<bcos::Timer> m_clearCacheTimer = nullptr;
    // stat info periodically
    std::shared_ptr<bcos::Timer> m_statTimer = nullptr;
};

}  // namespace ratelimiter
}  // namespace gateway
}  // namespace bcos
