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
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : statistic network flow
 * @file: StatisticHandler.cpp
 * @author: yujiechen
 * @date: 2019-10-15
 * */
#include "StatisticHandler.h"
using namespace dev;
using namespace dev::eth;
using namespace dev::p2p;

// update the total-network-in packet size
void StatisticHandler::updateServiceInPackets(std::shared_ptr<P2PMessage> _message)
{
    WriteGuard l(x_serviceInPacketBytes);
    auto groupAndProtocol = getGroupAndProtocol(_message->protocolID());
    m_serviceTotalInBytes += _message->deliveredLength();
    if (!m_serviceInPacketBytes.count(groupAndProtocol.second))
    {
        m_serviceInPacketBytes[groupAndProtocol.second] = _message->deliveredLength();
        return;
    }
    m_serviceInPacketBytes[groupAndProtocol.second] += _message->deliveredLength();
}

// update the total-network-out packet size
void StatisticHandler::updateServiceOutPackets(std::shared_ptr<P2PMessage> _message)
{
    WriteGuard l(x_serviceOutPacketBytes);
    auto groupAndProtocol = getGroupAndProtocol(_message->protocolID());
    m_serviceTotalOutBytes += _message->length();
    if (!m_serviceOutPacketBytes.count(groupAndProtocol.second))
    {
        m_serviceOutPacketBytes[groupAndProtocol.second] = _message->length();
        return;
    }
    m_serviceOutPacketBytes[groupAndProtocol.second] += _message->length();
}

// update sended block info
void StatisticHandler::updateSendedBlockInfo(size_t const& _blockBytes, size_t const& _blockCount)
{
    m_sendedBlockCount += _blockCount;
    m_sendedBlockBytes += _blockBytes;
}

// update downloaded block info
void StatisticHandler::updateDownloadedBlockInfo(
    size_t const& _blockBytes, size_t const& _blockCount)
{
    m_downloadBlockCount += _blockCount;
    m_downloadBlockBytes += _blockBytes;
}

void StatisticHandler::updateSendedTxsInfo(size_t const& _txsCount, size_t const& _txsBytes)
{
    m_sendedTxsCount += _txsCount;
    m_sendedTxsBytes += _txsBytes;
}

void StatisticHandler::updateSendedTxsInfo(size_t const& _txsBytes)
{
    m_sendedTxsBytes += _txsBytes;
}

void StatisticHandler::updateDownloadedTxsCount(size_t const& _txsCount)
{
    m_downloadTxsCount += _txsCount;
}

void StatisticHandler::updateDownloadedTxsBytes(size_t const& _txsBytes)
{
    m_downloadTxsBytes += _txsBytes;
}

void StatisticHandler::updateConsInPacketsInfo(
    uint8_t const& _packetType, uint64_t const& _packetBytes)
{
    WriteGuard l(x_consInPacketsInfo);
    m_totalConsInPacketBytes += _packetBytes;
    if (!m_consInPacketsInfo.count(_packetType))
    {
        m_consInPacketsInfo[_packetType] = std::make_pair(1, _packetBytes);
        return;
    }
    m_consInPacketsInfo[_packetType].first++;
    m_consInPacketsInfo[_packetType].second += _packetBytes;
}

void StatisticHandler::updateConsOutPacketsInfo(
    uint8_t const& _packetType, uint64_t _sessionSize, uint64_t const& _packetBytes)
{
    WriteGuard l(x_consOutPacketsInfo);
    auto packetBytes = _sessionSize * _packetBytes;
    m_totalConsOutPacketBytes += packetBytes;
    if (!m_consOutPacketsInfo.count(_packetType))
    {
        m_consOutPacketsInfo[_packetType] = std::make_pair(_sessionSize, packetBytes);
        return;
    }
    m_consOutPacketsInfo[_packetType].first += _sessionSize;
    m_consOutPacketsInfo[_packetType].second += packetBytes;
}

void StatisticHandler::printSerivceStatisticInfo()
{
    // print network-in information
    {
        ReadGuard l(x_serviceInPacketBytes);
        for (auto const& it : m_serviceInPacketBytes)
        {
            STATISTIC_LOG(INFO) << LOG_DESC("Service: network-in")
                                << LOG_KV("protocolID", std::to_string(it.first))
                                << LOG_KV("inBytes", it.second);
        }
        STATISTIC_LOG(INFO) << LOG_DESC("Service: network-in")
                            << LOG_KV("totalInBytes", m_serviceTotalInBytes);
    }
    // print network-out information
    {
        ReadGuard l(x_serviceOutPacketBytes);
        for (auto const& it : m_serviceOutPacketBytes)
        {
            STATISTIC_LOG(INFO) << LOG_DESC("Service: network-out")
                                << LOG_KV("protocolID", std::to_string(it.first))
                                << LOG_KV("outBytes", it.second);
        }
        STATISTIC_LOG(INFO) << LOG_DESC("Service: network-out")
                            << LOG_KV("totalOutBytes", m_serviceTotalOutBytes);
    }
}

void StatisticHandler::printSyncStatisticInfo()
{
    STATISTIC_LOG(INFO) << LOG_DESC("Sync: statistic-info")
                        << LOG_KV("sendedBlockCount", m_sendedBlockCount)
                        << LOG_KV("sendedBlockBytes", m_sendedBlockBytes)
                        << LOG_KV("sendedTxsCount", m_sendedTxsCount)
                        << LOG_KV("sendedTxsBytes", m_sendedTxsBytes)
                        << LOG_KV("downloadBlockCount", m_downloadBlockCount)
                        << LOG_KV("downloadBlockBytes", m_downloadBlockBytes)
                        << LOG_KV("downloadTxsCount", m_downloadTxsCount)
                        << LOG_KV("downloadTxsBytes", m_downloadTxsBytes);
}

void StatisticHandler::printConsStatisticInfo()
{
    // print consensus-network-in information
    {
        ReadGuard l(x_consInPacketsInfo);
        for (auto const& it : m_consInPacketsInfo)
        {
            STATISTIC_LOG(INFO) << LOG_DESC("Consensus: network-in")
                                << LOG_KV("packetType", std::to_string(it.first))
                                << LOG_KV("packetCount", it.second.first)
                                << LOG_KV("packetSize", it.second.second);
        }
        STATISTIC_LOG(INFO) << LOG_DESC("Consensus: network-in")
                            << LOG_KV("totalInPacketBytes", m_totalConsInPacketBytes);
    }
    // print consensus-network-out information
    {
        ReadGuard l(x_consOutPacketsInfo);
        for (auto const& it : m_consOutPacketsInfo)
        {
            STATISTIC_LOG(INFO) << LOG_DESC("Consensus: network-out")
                                << LOG_KV("packetType", std::to_string(it.first))
                                << LOG_KV("packetCount", it.second.first)
                                << LOG_KV("packetSize", it.second.second);
        }
        STATISTIC_LOG(INFO) << LOG_DESC("Consensus: network-out")
                            << LOG_KV("totalOutPacketBytes", m_totalConsOutPacketBytes);
    }
}

void StatisticHandler::printStatistics()
{
    printSerivceStatisticInfo();
    printSyncStatisticInfo();
    printConsStatisticInfo();
}