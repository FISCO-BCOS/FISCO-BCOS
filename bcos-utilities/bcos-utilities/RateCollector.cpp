/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file RateCollector.cpp
 * @author: Jimmy Shi
 * @date 2023/3/8
 */

#include "RateCollector.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"

using namespace bcos;

bool c_enableStatCollector = false;

RateCollector::RateCollector(
    boost::asio::io_context& _ioService, std::string _moduleName, uint64_t _intervalMS)
  : m_moduleName(std::move(_moduleName)), m_intervalMS(_intervalMS)
{
    m_reportTimer = std::make_shared<Timer>(_ioService, _intervalMS, m_moduleName);
    m_reportTimer->registerTimeoutHandler([this]() {
        report();
        flush();
        m_reportTimer->restart();
    });
}

RateCollector::~RateCollector()
{
    stop();
}

void RateCollector::start()
{
    if (m_reportTimer)
    {
        m_reportTimer->start();
    }
}

void RateCollector::stop()
{
    if (m_reportTimer)
    {
        m_reportTimer->stop();
        m_reportTimer->destroy();
    }
}


void RateCollector::enable()
{
    c_enableStatCollector = true;
}

void RateCollector::disable()
{
    c_enableStatCollector = false;
}

bool RateCollector::isEnable()
{
    return c_enableStatCollector;
}

void RateCollector::report()
{
    if (!isEnable())
    {
        return;
    }

    auto& stat = m_rateCollectorStat;
    BCOS_LOG(INFO) << LOG_BADGE("RateCollector")
                   << LOG_BADGE(m_moduleName)
                   << LOG_KV("lastCount", stat.lastCount)
                   << LOG_KV("lastTotalDataSize", stat.lastTotalDataSize)
                   << LOG_KV("lastFailedCount", stat.lastFailedCount)
                   << LOG_KV("lastTotalFailedDataSize", stat.lastTotalFailedDataSize)
                   << LOG_KV("lastRate(Mb/s)", calcAvgRate(stat.lastTotalDataSize, m_intervalMS))
                   << LOG_KV("lastQPS(request/s)", calcAvgQPS(stat.lastCount, m_intervalMS));
}

void RateCollector::flush()
{
    m_rateCollectorStat.lastCount = 0;
    m_rateCollectorStat.lastFailedCount = 0;
    m_rateCollectorStat.lastTotalDataSize = 0;
    m_rateCollectorStat.lastTotalFailedDataSize = 0;
}

void RateCollector::update(std::size_t _dataSize, bool _success)
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