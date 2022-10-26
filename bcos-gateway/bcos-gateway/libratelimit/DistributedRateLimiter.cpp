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
 * @file DistributedRateLimiter.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/DistributedRateLimiter.h>
#include <bcos-utilities/Common.h>
#include <mutex>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

// lua script for redis distributed rate limit
const std::string DistributedRateLimiter::LUA_SCRIPT = R"(
        -- 限流key
        local key = KEYS[1]
        -- 限流大小, 初始token数目
        local initialToken = tonumber(ARGV[1])
        -- 申请token数目
        local requestToken = tonumber(ARGV[2])
        -- 限流间隔，单位秒
        local interval = tonumber(ARGV[3])
        
        -- key不存在, 未初始化或过期, 初始化
        local r = redis.call('get', key)
        if not r then
            -- 设置限流值
            redis.call('set', key, ARGV[1])
            -- 设置过期时间
            redis.call("EXPIRE", key, ARGV[3])
        end
        
        -- 剩余token
        local curentToken = tonumber(redis.call('get', key) or '0')
        
        -- 是否超出限流
        if curentToken < requestToken then
            -- 返回(拒绝)
            return -1
        else
            -- 更新 curentToken
            redis.call("DECRBY", key, requestToken)
            -- 返回(放行)
            return requestToken
        end
        )";

/**
 * @brief acquire permits
 *
 * @param _requiredPermits
 * @return void
 */
void DistributedRateLimiter::acquire(int64_t _requiredPermits)
{
    // Note: This operation is not supported
    std::ignore = _requiredPermits;
}

/**
 * @brief
 *
 * @param _requiredPermits
 * @return true
 * @return false
 */
bool DistributedRateLimiter::tryAcquire(int64_t _requiredPermits)
{
    // try local cache acquire first
    if (m_enableLocalCache && tryAcquireLocalCache(_requiredPermits, true))
    {
        return true;
    }

    if (!m_enableLocalCache)
    {
        // local cache not enable,  request redis directly
        return requestRedis(_requiredPermits) >= 0;
    }

    std::lock_guard<std::mutex> lock(x_localCache);
    // another thread update local cache again
    if (tryAcquireLocalCache(_requiredPermits, false))
    {
        return true;
    }

    // the request acquire bigger than _requiredPermits has been failed
    if (m_lastFailedPermit > 0 && _requiredPermits >= m_lastFailedPermit)
    {
        return false;
    }

    // request redis to update local cache
    int64_t permits = m_maxPermits / 100 * m_localCachePermits;
    if (requestRedis(permits > _requiredPermits ? permits : _requiredPermits) >= 0)
    {
        // update local cache
        m_localCachePermits += (permits - _requiredPermits);
        return true;
    }

    // update failed info
    m_lastFailedPermit = permits;
    if (permits == _requiredPermits)
    {
        return false;
    }

    auto result = requestRedis(_requiredPermits);
    if (result < 0)
    {
        m_lastFailedPermit = _requiredPermits;
    }

    return result >= 0;
}

/**
 * @brief
 *
 * @return
 */
void DistributedRateLimiter::rollback(int64_t _requiredPermits)
{
    // Note: This operation is not supported
    std::ignore = _requiredPermits;
}

/**
 * @brief
 *
 * @param _requiredPermits
 * @param _withLock
 * @return true
 * @return false
 */
bool DistributedRateLimiter::tryAcquireLocalCache(int64_t _requiredPermits, bool _withLock)
{
    // enable local cache and local cache is not enough
    if (m_localCachePermits < _requiredPermits)
    {
        return false;
    }

    bool result = false;
    if (_withLock)
    {
        x_localCache.lock();
    }

    if (m_localCachePermits >= _requiredPermits)
    {
        m_localCachePermits -= _requiredPermits;
        result = true;
    }

    if (_withLock)
    {
        x_localCache.unlock();
    }

    return result;
}

/**
 * @brief
 *
 * @param _requiredPermits
 * @return true
 * @return false
 */
int64_t DistributedRateLimiter::requestRedis(int64_t _requiredPermits)
{
    try
    {
        auto start = utcTimeUs();

        auto keys = {m_rateLimiterKey};
        std::vector<std::string> args = {std::to_string(m_maxPermits),
            std::to_string(_requiredPermits), std::to_string(m_interval)};

        auto result = m_redis->eval<long long>(
            LUA_SCRIPT, keys.begin(), keys.end(), args.begin(), args.end());

        auto end = utcTimeUs();

        // update stat
        result >= 0 ? m_stat.updateOk() : m_stat.updateFailed();

        if ((end - start) > 1000)
        {
            m_stat.updateMore1MS();
        }

        m_stat.lastRequestTotalCostMS += (end - start);

        return result;
    }
    catch (const std::exception& e)
    {
        m_stat.updateExp();
        // TODO: statistics failure information
        GATEWAY_LOG(DEBUG) << LOG_BADGE("DistributedRateLimiter") << LOG_DESC("requestRedis")
                           << LOG_KV("rateLimitKey", m_rateLimiterKey)
                           << LOG_KV("enableLocalCache", m_enableLocalCache)
                           << LOG_KV("error", e.what());

        // exception throw, allow this acquire
        return _requiredPermits;
    }
}

/**
 * @brief
 *
 */
void DistributedRateLimiter::stat()
{
    GATEWAY_LOG(DEBUG) << LOG_BADGE("DistributedRateLimiter") << LOG_BADGE("stat")
                       << LOG_BADGE(m_rateLimiterKey) << LOG_KV("totalC", m_stat.totalRequestRedis)
                       << LOG_KV("totalExpC", m_stat.totalRequestRedisExp)
                       << LOG_KV("totalFailedC", m_stat.totalRequestRedisFailed)
                       << LOG_KV("totalMore1MSC", m_stat.totalRequestRedisMore1MS)
                       << LOG_KV("lastC", m_stat.lastRequestRedis)
                       << LOG_KV("lastExpC", m_stat.lastRequestRedisExp)
                       << LOG_KV("lastFailedC", m_stat.lastRequestRedisFailed)
                       << LOG_KV("lastMore1MSC", m_stat.lastRequestRedisMore1MS)
                       << LOG_KV("lastTotalCostMS", m_stat.lastRequestTotalCostMS);

    m_stat.resetLast();
    m_statTimer->restart();
}

void DistributedRateLimiter::refreshLocalCache()
{
    {
        std::lock_guard<std::mutex> lock(x_localCache);
        m_localCachePermits = 0;
        m_localCachePercent = 0;
        m_lastFailedPermit = 0;
    }

    m_clearCacheTimer->restart();
}
