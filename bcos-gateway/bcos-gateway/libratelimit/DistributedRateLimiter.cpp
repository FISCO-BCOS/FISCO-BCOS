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
#include <exception>
#include <string>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

// lua script for redis distributed rate limit
const std::string DistributedRateLimiter::luaScript = R"(
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
    // TODO: This operation is not supported
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
    try
    {
        auto start = utcTime();

        auto keys = {m_rateLimitKey};
        std::vector<std::string> args = {
            std::to_string(m_maxPermits), std::to_string(_requiredPermits), "1"};

        auto result =
            m_redis->eval<int64_t>(luaScript, keys.begin(), keys.end(), args.begin(), args.end());

        auto end = utcTime();

        if (1)
        {
            GATEWAY_LOG(TRACE) << LOG_BADGE("DistributedRateLimiter::tryAcquire")
                               << LOG_KV("elapsedTime", (end - start))
                               << LOG_KV("maxPermits", m_maxPermits)
                               << LOG_KV("requiredPermits", _requiredPermits)
                               << LOG_KV("result", result);
        }

        return result >= 0;
    }
    catch (const std::exception& e)
    {
        // TODO: statistics failure Information
        GATEWAY_LOG(DEBUG) << LOG_BADGE("DistributedRateLimiter::tryAcquire")
                           << LOG_KV("rateLimitKey", m_rateLimitKey) << LOG_KV("error", e.what());

        // exception throw, allow this acquire
        return true;
    }
}

/**
 * @brief
 *
 * @return
 */
void DistributedRateLimiter::rollback(int64_t _requiredPermits)
{
    // TODO: This operation is not supported
    std::ignore = _requiredPermits;
}
