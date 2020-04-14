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
 * @file: NetworkStatistic.cpp
 * @author: yujiechen
 * @date: 2020-04-14
 */
#include "NetworkStatistic.h"

using namespace dev::stat;

void NetworkStatistic::flushNetworkStatistic()
{
    m_averageOutBandwidth = m_outBytesPerWindow * 1000 / m_refreshWindow;
    m_averageInBandwidth = m_inBytesPerWindow * 1000 / m_refreshWindow;
    m_outBytesPerWindow = 0;
    m_inBytesPerWindow = 0;
}

void NetworkStatistic::updateInBytes(uint64_t const& _inBytesSize)
{
    m_inBytesPerWindow += _inBytesSize;
}

void NetworkStatistic::updateOutBytes(uint64_t const& _outBytesSize)
{
    m_outBytesPerWindow += _outBytesSize;
}