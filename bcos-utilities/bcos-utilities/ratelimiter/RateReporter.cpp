/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file RateReporter.cpp
 */

#include "bcos-utilities/ratelimiter/RateReporter.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"

using namespace bcos;

RateReporter::RateReporter(std::string _moduleName, uint64_t _intervalMS)
  : m_moduleName(std::move(_moduleName)), m_intervalMS(_intervalMS)
{
    m_reportTimer = std::make_shared<Timer>(_intervalMS, m_moduleName);
    m_reportTimer->registerTimeoutHandler([this]() {
        report();
        flush();
        m_reportTimer->restart();
    });
}

RateReporter::~RateReporter()
{
    stop();
}

void RateReporter::start()
{
    if (m_reportTimer)
    {
        m_reportTimer->start();
    }
}

void RateReporter::stop()
{
    if (m_reportTimer)
    {
        m_reportTimer->stop();
    }
}

void RateReporter::report()
{
    auto& stat = m_rateReporterStat;
    BCOS_LOG(INFO) << LOG_BADGE("RateReporter")
                   << LOG_BADGE(m_moduleName)
                   << LOG_KV("lastCount", stat.lastCount)
                   << LOG_KV("lastTotalDataSize", stat.lastTotalDataSize)
                   << LOG_KV("lastFailedCount", stat.lastFailedCount)
                   << LOG_KV("lastTotalFailedDataSize", stat.lastTotalFailedDataSize)
                   << LOG_KV("lastRate(Mb/s)", calcAvgRate(stat.lastTotalDataSize, m_intervalMS))
                   << LOG_KV("lastQPS(request/s)", calcAvgQPS(stat.lastCount, m_intervalMS));
}

void RateReporter::flush()
{
    m_rateReporterStat.lastCount = 0;
    m_rateReporterStat.lastFailedCount = 0;
    m_rateReporterStat.lastTotalDataSize = 0;
    m_rateReporterStat.lastTotalFailedDataSize = 0;
}

void RateReporter::update(std::size_t _dataSize, bool _success)
{
    auto const dataSize = static_cast<int64_t>(_dataSize);
    if (_success)
    {
        m_rateReporterStat.totalCount++;
        m_rateReporterStat.lastCount++;
        m_rateReporterStat.totalDataSize += dataSize;
        m_rateReporterStat.lastTotalDataSize += dataSize;
    }
    else
    {
        m_rateReporterStat.totalFailedCount++;
        m_rateReporterStat.lastFailedCount++;
        m_rateReporterStat.totalFailedDataSize += dataSize;
        m_rateReporterStat.lastTotalFailedDataSize += dataSize;
    }
}

RateReporter::Ptr RateReporterFactory::build(std::string _moduleName, uint64_t _intervalMS)
{
    return std::make_shared<RateReporter>(std::move(_moduleName), _intervalMS);
}
