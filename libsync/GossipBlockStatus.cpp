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
#include "GossipBlockStatus.h"

using namespace bcos;
using namespace bcos::sync;

// start gossip thread
void GossipBlockStatus::start()
{
    if (m_running)
    {
        GOSSIP_LOG(INFO) << LOG_DESC("GossipBlockStatus already started");
        return;
    }
    // start gossipBlockStatus worker
    startWorking();
    m_running = true;
    GOSSIP_LOG(INFO) << LOG_DESC("start GossipBlockStatus succ");
}

// stop the gossip thread
void GossipBlockStatus::stop()
{
    if (!m_running)
    {
        GOSSIP_LOG(INFO) << LOG_DESC("GossipBlockStatus already stopped");
        return;
    }
    GOSSIP_LOG(INFO) << LOG_DESC("stop GossipBlockStatus succ");
    m_running = false;
    doneWorking();
    if (isWorking())
    {
        stopWorking();
        terminate();
    }
}

void GossipBlockStatus::executeTask()
{
    assert(m_gossipBlockStatusHandler);
    m_gossipBlockStatusHandler(m_gossipPeersNumber);
}
