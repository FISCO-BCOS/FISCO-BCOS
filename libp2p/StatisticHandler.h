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
 * @file: StatisticHandler.h
 * @author: yujiechen
 * @date: 2019-10-15
 * */
#pragma once
#include <libdevcore/Guards.h>
#include <libethcore/Protocol.h>
#include <libp2p/P2PMessage.h>

#define STATISTIC_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("STATISTIC")
namespace dev
{
namespace p2p
{
class StatisticHandler
{
public:
    using Ptr = std::shared_ptr<StatisticHandler>;
    StatisticHandler() = default;
    virtual ~StatisticHandler() {}

    /// statistics for Service
    // update m_serviceInPacketBytes
    virtual void updateServiceInPackets(std::shared_ptr<P2PMessage> _message);
    // update m_serviceOutPacketBytes
    virtual void updateServiceOutPackets(std::shared_ptr<P2PMessage> _message);

    /// statistics for sync
    virtual void updateSendedBlockInfo(size_t const& _blockBytes, size_t const& _blockCount = 1);
    virtual void updateSendedTxsInfo(size_t const& _txsCount, size_t const& _txsBytes);
    virtual void updateSendedTxsInfo(size_t const& _txsBytes);
    virtual void updateDownloadedBlockInfo(
        size_t const& _blockBytes, size_t const& _blockCount = 1);
    virtual void updateDownloadedTxsCount(size_t const& _txsCount);
    virtual void updateDownloadedTxsBytes(size_t const& _txsBytes);

    /// statistics for consensus
    virtual void updateConsInPacketsInfo(uint8_t const& _packetType, uint64_t const& _packetBytes);
    virtual void updateConsOutPacketsInfo(
        uint8_t const& _packetType, uint64_t _sessionSize, uint64_t const& _packetBytes);

    // print the network statistic
    virtual void printStatistics();

private:
    void printSerivceStatisticInfo();
    void printSyncStatisticInfo();
    void printConsStatisticInfo();

private:
    /// statistics for Serivce
    // stat network-flow by register into the Service module
    std::map<dev::PROTOCOL_ID, uint64_t> m_serviceInPacketBytes;
    mutable SharedMutex x_serviceInPacketBytes;
    uint64_t m_serviceTotalInBytes = 0;

    std::map<dev::PROTOCOL_ID, uint64_t> m_serviceOutPacketBytes;
    mutable SharedMutex x_serviceOutPacketBytes;
    uint64_t m_serviceTotalOutBytes = 0;

    ///  statistics for sync
    // stat block sync
    std::atomic<uint64_t> m_sendedBlockCount = {0};
    std::atomic<uint64_t> m_sendedBlockBytes = {0};
    std::atomic<uint64_t> m_downloadBlockCount = {0};
    std::atomic<uint64_t> m_downloadBlockBytes = {0};

    // stat transaction sync
    std::atomic<uint64_t> m_sendedTxsCount = {0};
    std::atomic<uint64_t> m_sendedTxsBytes = {0};
    std::atomic<uint64_t> m_downloadTxsCount = {0};
    std::atomic<uint64_t> m_downloadTxsBytes = {0};

    /// statistics for consensus
    // map between packet_id and the <packetCount, packetBytesSize>
    std::map<uint8_t, std::pair<uint64_t, uint64_t>> m_consInPacketsInfo;
    mutable SharedMutex x_consInPacketsInfo;
    uint64_t m_totalConsInPacketBytes = 0;

    std::map<uint8_t, std::pair<uint64_t, uint64_t>> m_consOutPacketsInfo;
    mutable SharedMutex x_consOutPacketsInfo;
    uint64_t m_totalConsOutPacketBytes = 0;
};
}  // namespace p2p
}  // namespace dev
