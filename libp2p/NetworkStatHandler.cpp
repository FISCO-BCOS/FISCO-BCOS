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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implement network statistics
 * @file: NetworkStatHandler.cpp
 * @author: yujiechen
 * @date: 2020-03-20
 */
#include "NetworkStatHandler.h"

using namespace dev;
using namespace dev::stat;
// update incoming traffic
void NetworkStatHandler::updateIncomingTraffic(int32_t const& _msgType, uint64_t _msgSize)
{
    m_totalInMsgBytes += _msgSize;
    WriteGuard l(x_InMsgTypeToBytes);
    if (m_InMsgTypeToBytes->count(_msgType))
    {
        (*m_InMsgTypeToBytes)[_msgType] += _msgSize;
        return;
    }
    (*m_InMsgTypeToBytes)[_msgType] = _msgSize;
}

// update outcoming traffic
void NetworkStatHandler::updateOutcomingTraffic(int32_t const& _msgType, uint64_t _msgSize)
{
    m_totalOutMsgBytes += _msgSize;
    WriteGuard l(x_OutMsgTypeToBytes);
    if (m_OutMsgTypeToBytes->count(_msgType))
    {
        (*m_OutMsgTypeToBytes)[_msgType] += _msgSize;
        return;
    }
    (*m_OutMsgTypeToBytes)[_msgType] = _msgSize;
}

void NetworkStatHandler::printStatistics()
{
    // print the incoming traffic statistics
    printStatisticLog(m_InMsgTypeToBytes, m_totalInMsgBytes, x_InMsgTypeToBytes, m_InMsgDescSuffix);
    printStatisticLog(
        m_OutMsgTypeToBytes, m_totalOutMsgBytes, x_OutMsgTypeToBytes, m_OutMsgDescSuffix);
}

void NetworkStatHandler::printStatisticLog(StatConstPtr _statisticMap, uint64_t const& _totalBytes,
    SharedMutex& _mutex, std::string const& _suffix)
{
    ReadGuard l(_mutex);
    std::stringstream statDescOss;
    std::string totalStatDesc = m_statisticName + _suffix;
    statDescOss << LOG_KV(totalStatDesc, _totalBytes);
    for (auto const& it : *_statisticMap)
    {
        if (m_msgTypeToDesc.count(it.first))
        {
            std::string desc = m_msgTypeToDesc[it.first] + _suffix;
            statDescOss << LOG_KV(desc, it.second);
        }
    }
    GROUP_STAT_LOG(INFO) << statDescOss.str();
}

void NetworkStatHandler::resetStatistics()
{
    m_totalInMsgBytes.store(0);
    m_totalOutMsgBytes.store(0);
    {
        WriteGuard l(x_InMsgTypeToBytes);
        m_InMsgTypeToBytes->clear();
    }
    {
        WriteGuard l(x_OutMsgTypeToBytes);
        m_OutMsgTypeToBytes->clear();
    }
}