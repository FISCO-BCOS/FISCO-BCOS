/**
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
 * @brief implementation for PBFTTimer
 * @file PBFTTimer.h
 * @author: yujiechen
 * @date 2021-04-26
 */
#pragma once
#include <bcos-utilities/Timer.h>
namespace bcos
{
namespace consensus
{
class PBFTTimer : public Timer
{
public:
    using Ptr = std::shared_ptr<PBFTTimer>;
    explicit PBFTTimer(uint64_t _timeout) : Timer(_timeout) { updateAdjustedTimeout(); }

    ~PBFTTimer() override {}

    void updateChangeCycle(uint64_t _changeCycle)
    {
        m_changeCycle.store(std::min(_changeCycle, c_maxChangeCycle));
        updateAdjustedTimeout();
    }
    void incChangeCycle(uint64_t _increasedValue)
    {
        updateChangeCycle(m_changeCycle.load() + _increasedValue);
    }
    void resetChangeCycle() { updateChangeCycle(0); }
    uint64_t changeCycle() const { return m_changeCycle; }

    void reset(uint64_t _timeout) override
    {
        m_timeout = _timeout;
        updateAdjustedTimeout();
    }

protected:
    // ensure that this period of time increases exponentially until some requested operation
    // executes
    void updateAdjustedTimeout()
    {
        auto changeCycle = std::min(m_changeCycle.load(), c_maxChangeCycle);
        uint64_t timeout = m_timeout.load() * std::pow(m_base, changeCycle);
        if (timeout == m_adjustedTimeout)
        {
            return;
        }
        m_adjustedTimeout.store(timeout);
        if (running())
        {
            restart();
        }
    }
    uint64_t adjustTimeout() override { return m_adjustedTimeout; }

private:
    std::atomic<uint64_t> m_adjustedTimeout = {0};
    std::atomic<uint64_t> m_changeCycle = {0};
    double const m_base = 1.5;
    uint64_t c_maxChangeCycle = 10;
};
}  // namespace consensus
}  // namespace bcos