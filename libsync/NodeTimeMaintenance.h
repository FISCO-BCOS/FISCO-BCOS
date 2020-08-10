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
 * @brief : maintain the node time
 * @file: NodeTimeMaintenance.h
 * @author: yujiechen
 * @date: 2020-06-12
 */
#pragma once
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>

namespace dev
{
namespace sync
{
#define TIME_LOG(level) LOG(level) << LOG_BADGE("NodeTimeMaintenance")

class SyncStatusPacket;

class NodeTimeMaintenance
{
public:
    using Ptr = std::shared_ptr<NodeTimeMaintenance>;
    NodeTimeMaintenance();
    virtual ~NodeTimeMaintenance() {}

    virtual void tryToUpdatePeerTimeInfo(std::shared_ptr<SyncStatusPacket> _peerStatus);
    virtual int64_t getAlignedTime();
    int64_t medianTimeOffset() { return m_medianTimeOffset.load(); }

private:
    void updateTimeInfo();
    void checkLocalTimeAndWarning();

private:
    // maps between nodeID and the timeOffset
    std::shared_ptr<std::map<dev::h512, int64_t> > m_node2TimeOffset;
    std::atomic<int64_t> m_medianTimeOffset = {0};

    // 30min
    int64_t m_maxTimeOffset = 30 * 60 * 1000;
    // 3min
    int64_t m_minTimeOffset = 3 * 60 * 1000;

    mutable Mutex x_mutex;
};
}  // namespace sync
}  // namespace dev