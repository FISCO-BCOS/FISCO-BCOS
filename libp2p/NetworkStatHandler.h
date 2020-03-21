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
 * @file: NetworkStatHandler.h
 * @author: yujiechen
 * @date: 2020-03-20
 */
#pragma once
#include <libdevcore/Guards.h>
#include <libdevcore/Log.h>
#include <libethcore/Protocol.h>

#define GROUP_STAT_LOG(LEVEL) STAT_LOG(LEVEL) << LOG_BADGE(m_groupIdStr)

namespace dev
{
namespace stat
{
class NetworkStatHandler
{
public:
    using Ptr = std::shared_ptr<NetworkStatHandler>;
    using StatConstPtr = std::shared_ptr<std::map<int32_t, uint64_t> const>;
    using StatPtr = std::shared_ptr<std::map<int32_t, uint64_t>>;

    // _statisticName, eg. P2P, SDK
    NetworkStatHandler(std::string const& _statisticName)
      : m_statisticName(_statisticName),
        m_InMsgTypeToBytes(std::make_shared<std::map<int32_t, uint64_t>>()),
        m_OutMsgTypeToBytes(std::make_shared<std::map<int32_t, uint64_t>>())
    {}

    virtual ~NetworkStatHandler() {}

    void setGroupId(dev::GROUP_ID const& _groupId)
    {
        m_groupId = _groupId;
        m_groupIdStr = "g:" + std::to_string(m_groupId);
    }

    void setMsgTypeToDesc(std::map<int32_t, std::string> const& _msgTypeToDesc)
    {
        m_msgTypeToDesc = _msgTypeToDesc;
    }

    virtual void updateIncomingTraffic(int32_t const& _msgType, uint64_t _msgSize);
    virtual void updateOutcomingTraffic(int32_t const& _msgType, uint64_t _msgSize);

    uint64_t totalInMsgBytes() const { return m_totalInMsgBytes.load(); }
    uint64_t totalOutMsgBytes() const { return m_totalOutMsgBytes.load(); }

    virtual void printStatistics();
    virtual void resetStatistics();

protected:
    virtual void printStatisticLog(StatConstPtr _statisticMap, uint64_t const& _totalBytes,
        SharedMutex& _mutex, std::string const& _suffix);

private:
    std::string m_statisticName;

    dev::GROUP_ID m_groupId;
    std::string m_groupIdStr;
    // maps between message type and message description
    std::map<int32_t, std::string> m_msgTypeToDesc;

    // incoming traffic statistics: message type to the total
    std::shared_ptr<std::map<int32_t, uint64_t>> m_InMsgTypeToBytes;
    mutable SharedMutex x_InMsgTypeToBytes;
    std::atomic<uint64_t> m_totalInMsgBytes;
    std::string const m_InMsgDescSuffix = "In";

    // outcoming traffic statistics
    std::shared_ptr<std::map<int32_t, uint64_t>> m_OutMsgTypeToBytes;
    mutable SharedMutex x_OutMsgTypeToBytes;
    std::atomic<uint64_t> m_totalOutMsgBytes;
    std::string const m_OutMsgDescSuffix = "_Out";
};
}  // namespace stat
}  // namespace dev