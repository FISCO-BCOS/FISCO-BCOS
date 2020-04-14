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
 * @brief : Implement Channel network statistics
 * @file: RPCNetworkStatHandler.cpp
 * @author: yujiechen
 * @date: 2020-03-22
 */
#include "ChannelNetworkStatHandler.h"
using namespace dev::stat;
using namespace dev::channel;

void ChannelNetworkStatHandler::setFlushInterval(int64_t const& _flushInterval)
{
    m_flushInterval = _flushInterval;
}

void ChannelNetworkStatHandler::appendGroupP2PStatHandler(
    GROUP_ID const& _groupId, NetworkStatHandler::Ptr _handler)
{
    UpgradableGuard l(x_p2pStatHandlers);
    if (m_p2pStatHandlers->count(_groupId))
    {
        CHANNEL_STAT_LOG(WARNING) << LOG_DESC("appendGroupP2PStatHandler: already exists")
                                  << LOG_KV("groupId", std::to_string(_groupId));
        return;
    }
    UpgradeGuard ul(l);
    (*m_p2pStatHandlers)[_groupId] = _handler;
}

void ChannelNetworkStatHandler::removeGroupP2PStatHandler(GROUP_ID const& _groupId)
{
    UpgradableGuard l(x_p2pStatHandlers);
    if (m_p2pStatHandlers->count(_groupId))
    {
        UpgradeGuard ul(l);
        m_p2pStatHandlers->erase(_groupId);
        CHANNEL_STAT_LOG(WARNING) << LOG_DESC("removeGroupP2PStatHandler")
                                  << LOG_KV("groupId", std::to_string(_groupId));
    }
}

NetworkStatHandler::Ptr ChannelNetworkStatHandler::getP2PHandlerByGroupId(GROUP_ID const& _groupId)
{
    ReadGuard l(x_p2pStatHandlers);
    if (!m_p2pStatHandlers->count(_groupId))
    {
        return nullptr;
    }
    return (*m_p2pStatHandlers)[_groupId];
}

void ChannelNetworkStatHandler::updateGroupResponseTraffic(
    GROUP_ID const& _groupId, uint32_t const& _msgType, uint64_t const& _msgSize)
{
    auto p2pStatHandler = getP2PHandlerByGroupId(_groupId);
    if (!p2pStatHandler)
    {
        return;
    }
    p2pStatHandler->updateOutcomingTraffic(_msgType, _msgSize);
}

void ChannelNetworkStatHandler::updateIncomingTrafficForRPC(
    GROUP_ID _groupId, uint64_t const& _msgSize)
{
    auto p2pStatHandler = getP2PHandlerByGroupId(_groupId);
    if (!p2pStatHandler)
    {
        return;
    }
    p2pStatHandler->updateIncomingTraffic(ChannelMessageType::CHANNEL_RPC_REQUEST, _msgSize);
}

void ChannelNetworkStatHandler::updateOutcomingTrafficForRPC(
    GROUP_ID _groupId, uint64_t const& _msgSize)
{
    auto p2pStatHandler = getP2PHandlerByGroupId(_groupId);
    if (!p2pStatHandler)
    {
        return;
    }
    p2pStatHandler->updateOutcomingTraffic(ChannelMessageType::CHANNEL_RPC_REQUEST, _msgSize);
}

void ChannelNetworkStatHandler::flushLog()
{
    ReadGuard l(x_p2pStatHandlers);
    // print p2p statistics of each group
    for (auto p2pStatHandler : *m_p2pStatHandlers)
    {
        p2pStatHandler.second->printStatistics();
        // reset the statistic
        p2pStatHandler.second->resetStatistics();
    }
}

void ChannelNetworkStatHandler::startThread(
    ThreadPool::Ptr _thread, std::function<void()> const& _f, uint64_t const& _waitMs)
{
    std::weak_ptr<ChannelNetworkStatHandler> self(
        std::dynamic_pointer_cast<ChannelNetworkStatHandler>(shared_from_this()));
    m_running.store(true);
    _thread->enqueue([self, _f, _waitMs]() {
        while (true)
        {
            try
            {
                auto handler = self.lock();
                if (handler && handler->running())
                {
                    _f();
                    // wait m_flushInterval ms
                    std::this_thread::sleep_for(std::chrono::milliseconds(_waitMs));
                }
                else
                {
                    return;
                }
            }
            catch (std::exception const& _e)
            {
                CHANNEL_STAT_LOG(WARNING) << LOG_DESC("exceptioned when print network-statistic")
                                          << LOG_KV("errorInfo", boost::diagnostic_information(_e));
            }
        }
    });
}

void ChannelNetworkStatHandler::start()
{
    startThread(m_statLogFlushThread, boost::bind(&ChannelNetworkStatHandler::flushLog, this),
        m_flushInterval);
}

void ChannelNetworkStatHandler::startMonitorThread()
{
    startThread(m_networkMonitorThread,
        boost::bind(&ChannelNetworkStatHandler::calculateAverageNetworkBandwidth, this),
        m_refreshWindow);
}

void ChannelNetworkStatHandler::calculateAverageNetworkBandwidth()
{
    ReadGuard l(x_p2pStatHandlers);
    // print p2p statistics of each group
    for (auto p2pStatHandler : *m_p2pStatHandlers)
    {
        p2pStatHandler.second->networkMonitor()->flushNetworkStatistic();
    }
}
