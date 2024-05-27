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
 * @file RateCollector.h
 * @author: octopus
 * @date 2023-02-17
 */
#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/ObjectCounter.h"
#include "bcos-utilities/RefDataContainer.h"
#include "bcos-utilities/Timer.h"
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace bcos
{

struct RateCollectorStat
{
    std::atomic<uint64_t> totalDataSize{0};
    std::atomic<uint64_t> totalFailedDataSize{0};
    std::atomic<uint64_t> lastTotalDataSize{0};
    std::atomic<uint64_t> lastTotalFailedDataSize{0};
    std::atomic<uint64_t> totalCount{0};
    std::atomic<uint64_t> totalFailedCount{0};
    std::atomic<uint64_t> lastCount{0};
    std::atomic<uint64_t> lastFailedCount{0};
};

class RateCollector : bcos::ObjectCounter<RateCollector>
{
public:
    using Ptr = std::shared_ptr<RateCollector>;
    using ConstPtr = std::shared_ptr<const RateCollector>;

    RateCollector(std::string _moduleName, uint64_t _intervalMS)
      : m_moduleName(std::move(_moduleName)), m_intervalMS(_intervalMS)
    {
        m_reportTimer = std::make_shared<Timer>(_intervalMS, _moduleName);
        m_reportTimer->registerTimeoutHandler([this]() {
            report();
            flush();
            m_reportTimer->restart();
        });
    }

    ~RateCollector() { stop(); }

    RateCollector(const RateCollector&) = delete;
    RateCollector(RateCollector&&) = delete;
    RateCollector& operator=(const RateCollector&) = delete;
    RateCollector& operator=(RateCollector&&) = delete;

    void start()
    {
        if (m_reportTimer)
        {
            m_reportTimer->start();
        }
    }

    void stop()
    {
        if (m_reportTimer)
        {
            m_reportTimer->stop();
            m_reportTimer->destroy();
        }
    }

    static void enable();
    static void disable();
    bool isEnable();

    void report()
    {
        if (!isEnable())
        {
            return;
        }

        auto& stat = m_rateCollectorStat;
        BCOS_LOG(INFO) << LOG_BADGE("RateCollector")
                       << LOG_BADGE(m_moduleName)
                       //    << LOG_KV("totalCount", stat.totalCount)
                       //    << LOG_KV("totalFailedCount", stat.totalFailedCount)
                       //    << LOG_KV("totalDataSize", stat.totalDataSize)
                       //    << LOG_KV("totalFailedDataSize", stat.totalFailedDataSize)
                       << LOG_KV("lastCount", stat.lastCount)
                       << LOG_KV("lastTotalDataSize", stat.lastTotalDataSize)
                       << LOG_KV("lastFailedCount", stat.lastFailedCount)
                       << LOG_KV("lastTotalFailedDataSize", stat.lastTotalFailedDataSize)
                       << LOG_KV(
                              "lastRate(Mb/s)", calcAvgRate(stat.lastTotalDataSize, m_intervalMS))
                       << LOG_KV("lastQPS(request/s)", calcAvgQPS(stat.lastCount, m_intervalMS));
    }

    void flush()
    {
        m_rateCollectorStat.lastCount = 0;
        m_rateCollectorStat.lastFailedCount = 0;
        m_rateCollectorStat.lastTotalDataSize = 0;
        m_rateCollectorStat.lastTotalFailedDataSize = 0;
    }

    void update(std::size_t _dataSize, bool _success)
    {
        if (_success)
        {
            m_rateCollectorStat.totalCount++;
            m_rateCollectorStat.lastCount++;
            m_rateCollectorStat.totalDataSize += _dataSize;
            m_rateCollectorStat.lastTotalDataSize += _dataSize;
        }
        else
        {
            m_rateCollectorStat.totalFailedCount++;
            m_rateCollectorStat.lastFailedCount++;
            m_rateCollectorStat.totalFailedDataSize += _dataSize;
            m_rateCollectorStat.lastTotalFailedDataSize += _dataSize;
        }
    }

private:
    std::string m_moduleName;
    uint32_t m_intervalMS;
    RateCollectorStat m_rateCollectorStat;
    std::shared_ptr<Timer> m_reportTimer;
};

class RateCollectorFactory
{
public:
    static RateCollector::Ptr build(std::string _moduleName, uint64_t _intervalMS)
    {
        return std::make_shared<RateCollector>(_moduleName, _intervalMS);
    }
};

}  // namespace bcos