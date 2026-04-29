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

#include "bcos-utilities/Common.h"
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace bcos
{

class Timer;

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

    RateReporter(std::string _moduleName, uint64_t _intervalMS);

    ~RateReporter();

    RateReporter(const RateReporter&) = delete;
    RateReporter(RateReporter&&) = delete;
    RateReporter& operator=(const RateReporter&) = delete;
    RateReporter& operator=(RateReporter&&) = delete;

    void start();

    void stop();

    void report();

    void flush();

    void update(std::size_t _dataSize, bool _success);

private:
    std::string m_moduleName;
    uint32_t m_intervalMS;
    RateReporterStat m_rateReporterStat;
    std::shared_ptr<Timer> m_reportTimer;
};

class RateReporterFactory
{
public:
    static RateReporter::Ptr build(std::string _moduleName, uint64_t _intervalMS);
};

}  // namespace bcos