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
 * @file: NetworkStatistic.h
 * @author: yujiechen
 * @date: 2020-04-14
 */
#pragma once
#include <libdevcore/Common.h>

namespace dev
{
namespace stat
{
class NetworkStatistic
{
public:
    using Ptr = std::shared_ptr<NetworkStatistic>;
    NetworkStatistic() = default;
    virtual ~NetworkStatistic() {}

    virtual void flushNetworkStatistic();

    void updateInBytes(uint64_t const& _inBytesSize);
    void updateOutBytes(uint64_t const& _outBytesSize);

    uint64_t averageOutBandwidth() const { return m_averageOutBandwidth; }
    uint64_t averageInBandwidth() const { return m_averageInBandwidth; }

private:
    uint64_t m_refreshWindow;

    std::atomic<uint64_t> m_inBytesPerWindow;
    std::atomic<uint64_t> m_outBytesPerWindow;

    std::atomic<uint64_t> m_averageOutBandwidth;
    std::atomic<uint64_t> m_averageInBandwidth;
};
}  // namespace stat
}  // namespace dev
