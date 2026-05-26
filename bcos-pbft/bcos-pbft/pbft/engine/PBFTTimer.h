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
namespace bcos::consensus
{
class PBFTTimer : public Timer
{
public:
    using Ptr = std::shared_ptr<PBFTTimer>;
    explicit PBFTTimer(int64_t _timeout, std::string const& _name = "pbftTimer")
      : Timer(_timeout, _name)
    {
        updateAdjustedTimeout();
    }

    ~PBFTTimer() override = default;

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

    void reset(int64_t _timeout) override
    {
        setTimeout(_timeout);
        updateAdjustedTimeout();
    }

protected:
    // ensure that this period of time increases exponentially until some requested operation
    // executes
    void updateAdjustedTimeout()
    {
        auto changeCycle = std::min(m_changeCycle.load(), c_maxChangeCycle);
        int64_t timeout = this->timeout() * std::pow(m_base, changeCycle);
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
    std::atomic<int64_t> m_adjustedTimeout = 0;
    std::atomic<uint64_t> m_changeCycle = 0;
    // FIB-135: bound the exponential back-off so that view-change timeout stays
    // within a reasonable multiple of the base consensus_timeout.
    // With base=1.5 and c_maxChangeCycle=3: max multiplier = 1.5^3 = 3.375x.
    // Previously c_maxChangeCycle=10 allowed 1.5^10 ≈ 57.7x, meaning a 3s base
    // timeout grew to ~173 seconds, causing prolonged stalls every time a slow
    // or isolated node was elected rPBFT leader.
    constexpr static double m_base = 1.5;
    uint64_t c_maxChangeCycle = 3;
};
}  // namespace bcos::consensus