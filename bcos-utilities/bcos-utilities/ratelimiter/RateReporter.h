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
 * @file RateReporter.h
 * @author: octopus
 * @date 2023-02-17
 */
#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/RefDataContainer.h"
#include "bcos-utilities/Timer.h"
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace bcos
{

inline double calcAvgRate(uint64_t _data, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto avgRate = (double)_data * 8 * 1000 / 1024 / 1024 / _intervalMS;
        return avgRate;
    }
    return 0;
}

inline uint32_t calcAvgQPS(uint64_t _requestCount, uint32_t _intervalMS)
{
    if (_intervalMS > 0)
    {
        auto qps = _requestCount * 1000 / _intervalMS;
        return qps;
    }
    return 0;
}

struct RateReporterStat
{
    std::atomic<int64_t> totalDataSize{0};
    std::atomic<int64_t> totalFailedDataSize{0};
    std::atomic<int64_t> lastTotalDataSize{0};
    std::atomic<int64_t> lastTotalFailedDataSize{0};
    std::atomic<int64_t> totalCount{0};
    std::atomic<int64_t> totalFailedCount{0};
    std::atomic<int64_t> lastCount{0};
    std::atomic<int64_t> lastFailedCount{0};
};

class RateReporter
{
public:
    using Ptr = std::shared_ptr<RateReporter>;
    using ConstPtr = std::shared_ptr<const RateReporter>;

    RateReporter(std::string _moduleName, uint64_t _intervalMS)
      : m_moduleName(std::move(_moduleName)), m_intervalMS(_intervalMS)
    {
        m_reportTimer = std::make_shared<Timer>(_intervalMS, _moduleName);
        m_reportTimer->registerTimeoutHandler([this]() {
            report();
            flush();
            m_reportTimer->restart();
        });
    }

    ~RateReporter() { stop(); }

    RateReporter(const RateReporter&) = delete;
    RateReporter(RateReporter&&) = delete;
    RateReporter& operator=(const RateReporter&) = delete;
    RateReporter& operator=(RateReporter&&) = delete;

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
        }
    }

    void report()
    {
        auto& stat = m_rateReporterStat;
        BCOS_LOG(INFO) << LOG_BADGE("RateReporter")
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
        m_rateReporterStat.lastCount = 0;
        m_rateReporterStat.lastFailedCount = 0;
        m_rateReporterStat.lastTotalDataSize = 0;
        m_rateReporterStat.lastTotalFailedDataSize = 0;
    }

    void update(std::size_t _dataSize, bool _success)
    {
        if (_success)
        {
            m_rateReporterStat.totalCount++;
            m_rateReporterStat.lastCount++;
            m_rateReporterStat.totalDataSize += _dataSize;
            m_rateReporterStat.lastTotalDataSize += _dataSize;
        }
        else
        {
            m_rateReporterStat.totalFailedCount++;
            m_rateReporterStat.lastFailedCount++;
            m_rateReporterStat.totalFailedDataSize += _dataSize;
            m_rateReporterStat.lastTotalFailedDataSize += _dataSize;
        }
    }

private:
    std::string m_moduleName;
    uint32_t m_intervalMS;
    RateReporterStat m_rateReporterStat;
    std::shared_ptr<Timer> m_reportTimer;
};

class RateReporterFactory
{
public:
    static RateReporter::Ptr build(std::string _moduleName, uint64_t _intervalMS)
    {
        return std::make_shared<RateReporter>(_moduleName, _intervalMS);
    }
};

}  // namespace bcos