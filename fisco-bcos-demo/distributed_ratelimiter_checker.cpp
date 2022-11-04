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
 * @file distributed_ratlimiter_checker.cpp
 * @author: octopuswang
 * @date 2022-10-12
 */
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/libratelimit/DistributedRateLimiter.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

void usage()
{
    std::cerr << "Usage: ./distributed-ratelimiter-checker {redis host} {redis port} {pool size} "
                 "{client count} {rate} "
                 "{limit token}"
                 "{interval}\n"
              << std::endl;
    std::cerr << "eg:" << std::endl;
    std::cerr << "\t./distributed-ratelimiter-checker 127.0.0.1 6379 16 16 limittoken 1 2\n "
              << std::endl;
    exit(0);
}

struct StatData
{
    std::atomic<int64_t> totalFailedC{0};
    std::atomic<int64_t> totalSucC{0};

    std::atomic<int64_t> totalFailedData{0};
    std::atomic<int64_t> totalSucData{0};

    std::atomic<int64_t> lastFailedC{0};
    std::atomic<int64_t> lastSucC{0};

    std::atomic<int64_t> lastFailedData{0};
    std::atomic<int64_t> lastSucData{0};

    void resetLast()
    {
        lastFailedC = 0;
        lastSucC = 0;
        lastFailedData = 0;
        lastSucData = 0;
    }

    void update(int64_t _data, bool _suc)
    {
        if (_suc)
        {
            totalSucC++;
            lastSucC++;
            totalSucData += _data;
            lastSucData += _data;
        }
        else
        {
            totalFailedC++;
            lastFailedC++;
            totalFailedData += _data;
            lastFailedData += _data;
        }
    }
};

// TODO: add latency check

int main(int argc, char** argv)
{
    if (argc < 8)
    {
        usage();
    }

    std::string redisIP = argv[1];
    uint16_t redisPort = atoi(argv[2]);
    int32_t redisPoolSize = std::stol(argv[3]);
    uint32_t clientCount = std::stoul(argv[4]);
    std::string limitToken = argv[5];
    uint32_t rate = std::stoul(argv[6]) * 1024 * 1024 * 8;
    uint32_t rateInterval = std::stoul(argv[7]);

    std::cout << " [distributed-ratelimiter-checker] parameters:" << redisIP << std::endl;
    std::cout << "\t### redisIP:" << redisIP << std::endl;
    std::cout << "\t### redisPort:" << redisPort << std::endl;
    std::cout << "\t### redisPoolSize:" << redisPoolSize << std::endl;
    std::cout << "\t### clientCount:" << clientCount << std::endl;
    std::cout << "\t### limitToken:" << limitToken << std::endl;
    std::cout << "\t### rate:" << rate << std::endl;
    std::cout << "\t### interval:" << rateInterval << std::endl;

    auto factory = std::make_shared<GatewayFactory>("", "");

    GatewayConfig::RedisConfig redisConfig;
    redisConfig.host = redisIP;
    redisConfig.port = redisPort;
    redisConfig.connectionPoolSize = redisPoolSize;
    redisConfig.timeout = 1000;

    // init all redis instance as client
    std::vector<std::thread> threads(clientCount);
    //
    bool allThreadsInitSuc = false;

    // stat statistics
    StatData stateData;

    // init random
    srand(time(NULL));

    for (uint32_t i = 0; i < clientCount; i++)
    {
        auto redis = factory->initRedis(redisConfig);
        auto rateLimiter =
            std::make_shared<DistributedRateLimiter>(redis, limitToken, rate, rateInterval);
        threads.emplace_back([rateLimiter, rate, rateInterval, &allThreadsInitSuc, &stateData]() {
            while (true)
            {
                if (!allThreadsInitSuc)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                auto start = utcTimeUs();

                // tryAcquire (0, 1/10 * rate)] once time
                auto acquire = random() % (rate / 10);
                acquire = (acquire > 0 ? acquire : 1);

                bool result = rateLimiter->tryAcquire(acquire);

                // auto end = utcTimeUs();
                // if (end - start >= 500)
                // {
                //     std::cerr << " [distributed ratelimiter][timeout]"
                //               << LOG_KV("tid", std::this_thread::get_id())
                //               << LOG_KV("acquire", acquire) << LOG_KV("result", result)
                //               << LOG_KV("elapsedUS", (end - start)) << std::endl;
                // }

                // state suc
                stateData.update(acquire, result);

                // sleep ms
                auto sleepMS = random() % (2 * rateInterval * 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    };

    // stat thread
    threads.emplace_back([&allThreadsInitSuc, rate, rateInterval, &stateData]() {
        while (true)
        {
            if (!allThreadsInitSuc)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // reset last stat
            stateData.resetLast();

            std::this_thread::sleep_for(std::chrono::seconds(rateInterval));

            // stat report
            std::cerr << " [distributed ratelimiter][stat]" << LOG_DESC(" stat -> ")
                      << LOG_KV("interval", rateInterval)
                      << LOG_KV("totalSucC", stateData.totalSucC)
                      << LOG_KV("totalFailedC", stateData.totalFailedC)
                      << LOG_KV("totalSucData", stateData.totalSucData)
                      << LOG_KV("totalFailedData", stateData.totalFailedData)
                      << LOG_KV("lastSucC", stateData.lastSucC)
                      << LOG_KV("lastFailedC", stateData.lastFailedC)
                      << LOG_KV("lastSucData", stateData.lastSucData)
                      << LOG_KV("lastFailedData", stateData.lastFailedData) << LOG_KV("rate", rate)
                      << LOG_KV(" STATUS ", stateData.lastSucData <= (rate + int64_t(0.05 * rate)) ?
                                                "OK" :
                                                "OverFlow")
                      << std::endl;
        }
    });

    // init completely
    allThreadsInitSuc = true;

    // join all thread
    std::for_each(threads.begin(), threads.end(), [](std::thread& _t) mutable {
        if (_t.joinable())
        {
            _t.join();
        }
    });

    return 0;
}