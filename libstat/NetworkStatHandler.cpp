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
    updateTraffic(m_InMsgTypeToBytes, _msgType, _msgSize);
}

// update outgoing traffic
void NetworkStatHandler::updateOutgoingTraffic(int32_t const& _msgType, uint64_t _msgSize)
{
    m_totalOutMsgBytes += _msgSize;
    WriteGuard l(x_OutMsgTypeToBytes);
    updateTraffic(m_OutMsgTypeToBytes, _msgType, _msgSize);
}

void NetworkStatHandler::updateTraffic(StatPtr _statMap, int32_t const& _msgType, uint64_t _msgSize)
{
    if (_statMap->count(_msgType))
    {
        (*_statMap)[_msgType] += _msgSize;
        return;
    }
    (*_statMap)[_msgType] = _msgSize;
}

void NetworkStatHandler::printStatistics()
{
    printStatistics(c_p2p_statisticName, m_p2pMsgTypeToDesc, m_p2pMsgTypeToDesc);
    printStatistics(c_sdk_statisticName, c_sdkInMsgTypeToDesc, c_sdkOutMsgTypeToDesc);
    // print the totalBytes
    STAT_LOG(INFO) << LOG_TYPE("Total") << LOG_KV("g", m_groupId)
                   << LOG_KV("Total_In", m_totalInMsgBytes)
                   << LOG_KV("Total_Out", m_totalOutMsgBytes);
}

void NetworkStatHandler::printStatistics(std::string const& _statName,
    std::map<int32_t, std::string> const& _inStatTypeToDesc,
    std::map<int32_t, std::string> const& _outStatTypeToDesc)
{
    std::stringstream statDescOss;
    statDescOss << LOG_TYPE(_statName);
    statDescOss << LOG_KV("g", m_groupId);
    {
        ReadGuard inLock(x_InMsgTypeToBytes);
        // print the incoming traffic statistics
        printStatisticLog(
            _statName, _inStatTypeToDesc, m_InMsgTypeToBytes, m_InMsgDescSuffix, statDescOss);
    }
    {
        ReadGuard outLock(x_OutMsgTypeToBytes);
        printStatisticLog(
            _statName, _outStatTypeToDesc, m_OutMsgTypeToBytes, m_OutMsgDescSuffix, statDescOss);
    }
    STAT_LOG(INFO) << statDescOss.str();
}

void NetworkStatHandler::printStatisticLog(std::string const _statName,
    std::map<int32_t, std::string> const& _statTypeToDesc, StatPtr _statisticMap,
    std::string const& _suffix, std::stringstream& _oss)
{
    auto totalBytes = 0;
    for (auto const& it : _statTypeToDesc)
    {
        std::string desc = _statName + "_" + it.second + _suffix;
        if (_statisticMap->count(it.first))
        {
            _oss << LOG_KV(desc, (*_statisticMap)[it.first]);
            totalBytes += (*_statisticMap)[it.first];
        }
        else
        {
            _oss << LOG_KV(desc, 0);
        }
    }
    std::string totalBytesDesc = _statName + "_total" + _suffix;
    _oss << LOG_KV(totalBytesDesc, totalBytes);
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