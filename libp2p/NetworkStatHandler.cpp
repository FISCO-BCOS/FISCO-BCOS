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
#include "NetworkStatHandler.h"

using namespace dev;
using namespace dev::stat;
// update incoming traffic
void NetworkStatHandler::updateIncomingTraffic(int32_t const& _msgType, uint64_t _msgSize)
{
    m_totalInMsgBytes += _msgSize;
    WriteGuard l(x_InMsgTypeToBytes);
    if (m_InMsgTypeToBytes.count(_msgType))
    {
        m_InMsgTypeToBytes[_msgType] += _msgSize;
        return;
    }
    m_InMsgTypeToBytes[_msgType] = _msgSize;
}

// update outcoming traffic
void NetworkStatHandler::updateOutcomingTraffic(int32_t const& _msgType, uint64_t _msgSize)
{
    m_totalOutMsgBytes += _msgSize;
    WriteGuard l(x_OutMsgTypeToBytes);
    if (m_OutMsgTypeToBytes.count(_msgType))
    {
        m_OutMsgTypeToBytes[_msgType] += _msgSize;
        return;
    }
    m_OutMsgTypeToBytes[_msgType] = _msgSize;
}

void NetworkStatHandler::printStatistics() {}