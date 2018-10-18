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
#include <libdevcore/Common.h>
namespace dev
{
namespace consensus
{
struct TimeManager
{
    /// last execution finish time
    uint64_t m_lastExecFinishTime;
    uint64_t m_lastExecBlockFiniTime;
    unsigned m_viewTimeout;
    unsigned m_changeCycle = 0;
    uint64_t m_lastSignTime = 0;
    uint64_t m_lastConsensusTime;
    unsigned m_intervalBlockTime = 1000;
    /// time point of last signature collection
    std::chrono::system_clock::time_point m_lastGarbageCollection;
    static const unsigned kMaxChangeCycle = 20;
    static const unsigned CollectInterval = 60;
    float m_execTimePerTx;
    uint64_t m_leftTime;

    inline void initTimerManager(unsigned view_timeout)
    {
        m_lastExecFinishTime = m_lastExecBlockFiniTime = m_lastConsensusTime = utcTime();
        m_viewTimeout = view_timeout;
        m_changeCycle = 0;
        m_lastGarbageCollection = std::chrono::system_clock::now();
    }

    inline void changeView()
    {
        m_lastConsensusTime = 0;
        m_lastSignTime = 0;
        m_changeCycle = 0;
    }

    inline void updateChangeCycle()
    {
        m_changeCycle = std::min(m_changeCycle + 1, (unsigned)kMaxChangeCycle);
    }

    inline bool isTimeout()
    {
        auto now = utcTime();
        auto last = std::max(m_lastConsensusTime, m_lastSignTime);
        auto interval = (uint64_t)(m_viewTimeout * std::pow(1.5, m_changeCycle));
        return (now - last >= interval);
    }

    inline void updateTimeAfterHandleBlock(size_t const& txNum, uint64_t const& startExecTime)
    {
        m_lastExecFinishTime = utcTime();
        if (txNum != 0)
        {
            float execTime_per_tx = (float)(m_lastExecFinishTime - startExecTime);
            m_execTimePerTx = (m_execTimePerTx + execTime_per_tx) / 2;
            if (m_execTimePerTx >= m_intervalBlockTime)
                m_execTimePerTx = m_intervalBlockTime;
        }
    }

    inline uint64_t calculateMaxPackTxNum(uint64_t defaultMaxTxNum, u256 const& view)
    {
        auto last_exec_finish_time = std::min(m_lastExecFinishTime, m_lastExecBlockFiniTime);
        unsigned passed_time = 0;
        if (view != u256(0))
            passed_time = utcTime() - m_lastConsensusTime;
        else
        {
            if (m_lastConsensusTime - last_exec_finish_time >= m_intervalBlockTime)
                passed_time = utcTime() - m_lastConsensusTime;
            else
                passed_time = utcTime() - last_exec_finish_time;
        }
        auto left_time = 0;
        if (passed_time < m_intervalBlockTime)
            left_time = m_intervalBlockTime - passed_time;
        uint64_t max_tx_num = defaultMaxTxNum;
        if (m_execTimePerTx != 0)
        {
            max_tx_num = left_time / m_execTimePerTx;
            if (left_time > 0 && left_time < m_execTimePerTx)
                max_tx_num = 1;
            if (max_tx_num > defaultMaxTxNum)
                max_tx_num = defaultMaxTxNum;
        }
        return max_tx_num;
    }
};
}  // namespace consensus
}  // namespace dev
