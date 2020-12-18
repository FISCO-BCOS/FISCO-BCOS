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
 * @brief : block status gossip implementation
 * @author: yujiechen
 * @date: 2019-10-08
 */
#pragma once
#include <libprotocol/Protocol.h>
#include <libutilities/Worker.h>

#define GOSSIP_LOG(_OBV) LOG(_OBV) << LOG_BADGE("GossipBlockStatus")

namespace bcos
{
namespace sync
{
class GossipBlockStatus : public Worker
{
public:
    using Ptr = std::shared_ptr<GossipBlockStatus>;
    GossipBlockStatus(bcos::PROTOCOL_ID const& _protocolId, int64_t const& _gossipInterval,
        int64_t const& _gossipPeersNumber)
      : Worker("gossip-" + std::to_string(bcos::protocol::getGroupAndProtocol(_protocolId).first),
            _gossipInterval),
        m_gossipPeersNumber(_gossipPeersNumber)
    {}
    void registerGossipHandler(std::function<void(int64_t const&)> const& _gossipBlockStatusHandler)
    {
        m_gossipBlockStatusHandler = _gossipBlockStatusHandler;
    }
    // start gossip thread
    virtual void start();
    // stop the gossip thread
    virtual void stop();

    // gossip block status
    void executeTask() override;

private:
    // send block status to random neighbors every m_gossipInterval
    std::atomic_bool m_running = {false};
    std::int64_t m_gossipPeersNumber = 3;
    std::function<void(int64_t const&)> m_gossipBlockStatusHandler = nullptr;
};
}  // namespace sync
}  // namespace bcos
