/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : implementation of PBFT consensus
 * @file: TimeManager.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include <libutilities/Common.h>
namespace bcos
{
namespace consensus
{
struct TimeManager
{
    /// the max block generation time
    unsigned m_emptyBlockGenTime = 1000;

    /// last execution finish time, only one will be used at last
    /// the finish time of executing tx by leader
    uint64_t m_viewTimeout = 3 * m_emptyBlockGenTime;
    uint64_t m_consensusTimeout = 3 * m_emptyBlockGenTime;
    unsigned m_changeCycle = 0;
    // the recently time collected enough signature packages
    uint64_t m_lastSignTime = 0;
    // the recently time reached consensus
    uint64_t m_lastConsensusTime;
    // the recently time receive the rawPrepare and ready to execute the block
    uint64_t m_lastAddRawPrepareTime = 0;
    // the recently time executed the block
    uint64_t m_lastExecTime = 0;

    /// the minimum block generation time(default is 500ms)
    unsigned m_minBlockGenTime = 500;

    /// time point of last signature collection
    std::chrono::steady_clock::time_point m_lastGarbageCollection;
    const unsigned kMaxChangeCycle = 20;
    const unsigned CollectInterval = 60;

    void resetConsensusTimeout(unsigned const& _consensusTimeout)
    {
        m_consensusTimeout = _consensusTimeout;
    }

    inline void initTimerManager(unsigned view_timeout)
    {
        m_lastAddRawPrepareTime = utcSteadyTime();
        m_lastConsensusTime = utcSteadyTime();
        m_lastExecTime = utcSteadyTime();
        m_lastSignTime = 0;
        m_viewTimeout = view_timeout;
        m_changeCycle = 0;
        m_lastGarbageCollection = std::chrono::steady_clock::now();
    }

    inline void changeView()
    {
        m_lastConsensusTime = 0;
        m_lastSignTime = 0;
    }

    inline void updateChangeCycle()
    {
        m_changeCycle = std::min(m_changeCycle + 1, (unsigned)kMaxChangeCycle);
    }

    virtual bool isTimeout(int64_t _now = utcSteadyTime())
    {
        auto maxConsensusTime = std::max(m_lastConsensusTime, m_lastSignTime);
        auto last = maxConsensusTime;
        // collect PBFT related message packets and set the timeout to 3s
        auto viewTimeOut = m_viewTimeout;
        // the case that received the rawPrepare, but not collect enough sign requests
        if (m_lastAddRawPrepareTime > maxConsensusTime &&
            (m_lastConsensusTime != 0 || m_lastSignTime != 0))
        {
            // process block execution, set the timeout to consensus_timeout
            if (m_lastAddRawPrepareTime > m_lastExecTime)
            {
                viewTimeOut = m_consensusTimeout;
                last = m_lastAddRawPrepareTime;
            }
            // the block has been executed, while not collect enough sign requests
            else
            {
                last = m_lastExecTime;
            }
        }
        auto interval = (uint64_t)(viewTimeOut * std::pow(1.5, m_changeCycle));
        return (_now - last >= interval);
    }
};
}  // namespace consensus
}  // namespace bcos
