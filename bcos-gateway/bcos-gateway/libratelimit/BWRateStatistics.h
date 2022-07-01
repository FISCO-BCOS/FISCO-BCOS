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
 * @file BWRateStatistics.h
 * @author: octopus
 * @date 2022-06-30
 */
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

//
struct Statistics
{
    std::atomic<int64_t> totalDataSize;
    std::atomic<int64_t> lastDataSize;

    std::atomic<int64_t> totalTryDataSize;
    std::atomic<int64_t> lastTryDataSize;

    std::atomic<int64_t> totalCount;
    std::atomic<int64_t> lastCount;

    std::atomic<int64_t> requireSuc;
    std::atomic<int64_t> requireFailed;

public:
    void reset()
    {
        lastCount = 0;
        lastDataSize = 0;
    }

    void update(int64_t _dataSize)
    {
        totalCount++;
        lastCount++;

        totalDataSize += _dataSize;
        lastDataSize += _dataSize;
    }

    std::string toString(const std::string& _prefix);
};

class BWRateStatistics
{
public:
    const static std::string TOTAL_INCOMING;
    const static std::string TOTAL_OUTGOING;

public:
    using Ptr = std::shared_ptr<BWRateStatistics>;
    using ConstPtr = std::shared_ptr<const BWRateStatistics>;

public:
    void updateInGoing(const std::string& _endPoint, int64_t _dataSize);
    void updateOutGoing(const std::string& _endPoint, int64_t _dataSize);

    void updateInGoing(const std::string& _groupID, uint16_t _moduleID, int64_t _dataSize);
    void updateOutGoing(const std::string& _groupID, uint16_t _moduleID, int64_t _dataSize);

    // TODO: statistics about bandwidth try require failures
    void updateOutGoing(const std::string& _groupID, uint16_t _moduleID, int64_t _dataSize, bool);

public:
    void flushData();
    std::string toString();

public:
    const std::unordered_map<std::string, Statistics>& inStat() { return m_inStat; }
    const std::unordered_map<std::string, Statistics>& outStat() { return m_outStat; }

private:
    std::mutex m_inLock;
    std::mutex m_outLock;
    std::unordered_map<std::string, Statistics> m_inStat;
    std::unordered_map<std::string, Statistics> m_outStat;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos