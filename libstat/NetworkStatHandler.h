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
#include <libchannelserver/ChannelMessage.h>
#include <libprotocol/CommonProtocolType.h>
#include <libutilities/Common.h>

namespace bcos
{
namespace stat
{
class NetworkStatHandler
{
public:
    using Ptr = std::shared_ptr<NetworkStatHandler>;
    using StatPtr = std::shared_ptr<std::map<int32_t, uint64_t>>;

    // _statisticName, eg. P2P, SDK
    NetworkStatHandler()
      : m_InMsgTypeToBytes(std::make_shared<std::map<int32_t, uint64_t>>()),
        m_OutMsgTypeToBytes(std::make_shared<std::map<int32_t, uint64_t>>())
    {}

    virtual ~NetworkStatHandler() {}

    void setConsensusMsgType(std::string const& _type)
    {
        int32_t consType = bcos::protocol::ProtocolID::PBFT;
        if (bcos::stringCmpIgnoreCase(_type, "raft") == 0)
        {
            consType = bcos::protocol::ProtocolID::Raft;
        }
        m_p2pMsgTypeToDesc = {{consType, "CONS"}, {bcos::protocol::ProtocolID::BlockSync, "SYNC"}};
    }

    void setGroupId(bcos::GROUP_ID const& _groupId) { m_groupId = _groupId; }

    virtual void updateIncomingTraffic(int32_t const& _msgType, uint64_t _msgSize);
    virtual void updateOutgoingTraffic(int32_t const& _msgType, uint64_t _msgSize);

    virtual void updateTraffic(StatPtr _statMap, int32_t const& _msgType, uint64_t _msgSize);

    uint64_t totalInMsgBytes() const { return m_totalInMsgBytes.load(); }
    uint64_t totalOutMsgBytes() const { return m_totalOutMsgBytes.load(); }

    virtual void printStatistics();
    virtual void resetStatistics();

private:
    void printStatistics(std::string const& _statName,
        std::map<int32_t, std::string> const& _inStatTypeToDesc,
        std::map<int32_t, std::string> const& _outStatTypeToDesc);

    void printStatisticLog(std::string const _statName,
        std::map<int32_t, std::string> const& _statTypeToDesc, StatPtr _statisticMap,
        std::string const& _suffix, std::stringstream& _oss);

protected:
    std::string const c_p2p_statisticName = "P2P";
    std::string const c_sdk_statisticName = "SDK";

    bcos::GROUP_ID m_groupId;
    // maps between message type and message description
    std::map<int32_t, std::string> m_p2pMsgTypeToDesc;

    std::map<int32_t, std::string> const c_sdkInMsgTypeToDesc = {
        {bcos::channel::ChannelMessageType::CHANNEL_RPC_REQUEST, "RPC"},
        {bcos::channel::ChannelMessageType::CLIENT_REGISTER_EVENT_LOG, "RegitsterEvent"},
        {bcos::channel::ChannelMessageType::CLIENT_UNREGISTER_EVENT_LOG, "UnregitsterEvent"}};

    std::map<int32_t, std::string> const c_sdkOutMsgTypeToDesc = {
        {bcos::channel::ChannelMessageType::CHANNEL_RPC_REQUEST, "RPC"},
        {bcos::channel::ChannelMessageType::TRANSACTION_NOTIFY, "Txs"},
        {bcos::channel::ChannelMessageType::EVENT_LOG_PUSH, "EventLog"}};

    // incoming traffic statistics: message type to the total
    std::shared_ptr<std::map<int32_t, uint64_t>> m_InMsgTypeToBytes;
    mutable SharedMutex x_InMsgTypeToBytes;
    std::atomic<uint64_t> m_totalInMsgBytes = {0};
    std::string const m_InMsgDescSuffix = "In";

    // outgoing traffic statistics
    std::shared_ptr<std::map<int32_t, uint64_t>> m_OutMsgTypeToBytes;
    mutable SharedMutex x_OutMsgTypeToBytes;
    std::atomic<uint64_t> m_totalOutMsgBytes = {0};
    std::string const m_OutMsgDescSuffix = "_Out";
};
}  // namespace stat
}  // namespace bcos
