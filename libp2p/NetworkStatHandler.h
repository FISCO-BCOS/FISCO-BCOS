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
#include <libdevcore/ThreadPool.h>

namespace dev
{
namespace stat
{
class NetworkStatHandler
{
public:
    using Ptr = std::shared_ptr<NetworkStatHandler>;
    NetworkStatHandler() = default;
    virtual ~NetworkStatHandler() {}

    virtual void updateIncomingTraffic(int32_t const& _msgType, uint64_t _msgSize);
    virtual void updateOutcomingTraffic(int32_t const& _msgType, uint64_t _msgSize);

    uint64_t totalInMsgBytes() const { return m_totalInMsgBytes.load(); }
    uint64_t totalOutMsgBytes() const { return m_totalOutMsgBytes.load(); }

    virtual void printStatistics();

private:
    // incoming traffic statistics: message type to the total
    std::map<int32_t, uint64_t> m_InMsgTypeToBytes;
    mutable SharedMutex x_InMsgTypeToBytes;
    std::atomic<uint64_t> m_totalInMsgBytes;

    // outcoming traffic statistics
    std::map<int32_t, uint64_t> m_OutMsgTypeToBytes;
    mutable SharedMutex x_OutMsgTypeToBytes;
    std::atomic<uint64_t> m_totalOutMsgBytes;
};
}  // namespace stat
}  // namespace dev