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

#include <memory>
#include <tuple>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimit;

DistributedRateLimiter::DistributedRateLimiter(int64_t _maxQPS)
{
    std::ignore = _maxQPS;
}

/*
std::shared_ptr<sw::redis::Redis> DistributedRateLimiter::initRedis(
    const std::string& _redisIP, uint16_t& _redisPort)
{

     std::string redisIP = "127.0.0.1";
    uint16_t redisPort = 6379;
    m_redis = initRedis(redisIP, redisPort);

    RATELIMIT_LOG(INFO) << LOG_BADGE("[NEWOBJ][DistributedRateLimiter]")
                        << LOG_KV("redisIP", redisIP) << LOG_KV("redisPort", redisPort);


    sw::redis::ConnectionOptions connection_options;
    connection_options.host = _redisIP;    // Required.
    connection_options.port = _redisPort;  // Optional.
    // connection_options.password = "auth";  // Optional. No password by default.
    // connection_options.db = 1;  // Optional. Use the 0th database by default.

    // Optional. Timeout before we successfully send request to or receive response from redis.
    // By default, the timeout is 0ms, i.e. never timeout and block until we send or receive
    // successfully. NOTE: if any command is timed out, we throw a TimeoutError exception.
    connection_options.socket_timeout = std::chrono::milliseconds(2000);
    connection_options.connect_timeout = std::chrono::milliseconds(2000);

    sw::redis::ConnectionPoolOptions pool_options;
    // Pool size, i.e. max number of connections.
    pool_options.size = 100;

    // Connect to Redis server with a connection pool.
    auto redis = std::make_shared<sw::redis::Redis>(connection_options, pool_options);


    std::string key = "Redis Key";
    std::string value = "Hello, Redis.";
    redis->set(key, value);

    auto r = redis->get(key);
    if (r)
    {
    RATELIMIT_LOG(INFO) << LOG_BADGE("[NEWOBJ][DistributedRateLimiter]")
            << LOG_DESC("redis get") << LOG_KV("key", key)
            << LOG_KV("value", r.value());
    }
    else
    {
    RATELIMIT_LOG(INFO) << LOG_BADGE("[NEWOBJ][DistributedRateLimiter]")
            << LOG_DESC("redis get failed");
    }

    return redis;
}
*/

/**
 * @brief acquire permits
 *
 * @param _requiredPermits
 * @return void
 */
void DistributedRateLimiter::acquire(int64_t _requiredPermits)
{
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
    std::ignore = _requiredPermits;
    return true;
}

/**
 * @brief
 *
 * @return
 */
void DistributedRateLimiter::rollback(int64_t _requiredPermits)
{
    std::ignore = _requiredPermits;
    return;
}
