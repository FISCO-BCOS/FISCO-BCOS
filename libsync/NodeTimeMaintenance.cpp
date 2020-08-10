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
 * @file: NodeTimeMaintenance.cpp
 * @author: yujiechen
 * @date: 2020-06-12
 */

#include "NodeTimeMaintenance.h"
#include <libsync/SyncMsgPacket.h>

using namespace dev::sync;

NodeTimeMaintenance::NodeTimeMaintenance()
  : m_node2TimeOffset(std::make_shared<std::map<dev::h512, int64_t>>())
{}

void NodeTimeMaintenance::tryToUpdatePeerTimeInfo(SyncStatusPacket::Ptr _peerStatus)
{
    auto peer = _peerStatus->nodeId;
    int64_t localTime = utcTime();
    auto peerTimeOffset = _peerStatus->alignedTime - localTime;

    Guard l(x_mutex);
    // The time information of the same node is within m_minTimeOffset,
    // and the time information of the node is not updated
    if (m_node2TimeOffset->count(peer))
    {
        auto orgTimeOffset = (*m_node2TimeOffset)[peer];
        if (std::abs(orgTimeOffset - peerTimeOffset) <= m_minTimeOffset)
        {
            return;
        }
        (*m_node2TimeOffset)[peer] = peerTimeOffset;
    }
    else
    {
        // update time information
        m_node2TimeOffset->insert(std::make_pair(peer, peerTimeOffset));
    }

    // check remote time
    if (std::abs(peerTimeOffset) > m_maxTimeOffset)
    {
        TIME_LOG(WARNING) << LOG_DESC(
                                 "Invalid remote peer time:  too much difference from local time")
                          << LOG_KV("peer", peer.abridged())
                          << LOG_KV("peerTime", _peerStatus->alignedTime)
                          << LOG_KV("localTime", localTime)
                          << LOG_KV("medianTimeOffset", m_medianTimeOffset.load());
    }
    updateTimeInfo();

    TIME_LOG(INFO) << LOG_DESC("updateTimeInfo: update median time offset")
                   << LOG_KV("updatedMedianTimeOffset", m_medianTimeOffset)
                   << LOG_KV("peer", peer.abridged())
                   << LOG_KV("peerTime", _peerStatus->alignedTime)
                   << LOG_KV("peerOffset", peerTimeOffset) << LOG_KV("utcTime", utcTime());
}

void NodeTimeMaintenance::updateTimeInfo()
{
    // get median time offset
    std::vector<int64_t> timeOffsetVec;
    for (auto const& it : *m_node2TimeOffset)
    {
        timeOffsetVec.push_back(it.second);
    }
    std::sort(timeOffsetVec.begin(), timeOffsetVec.end());
    int64_t medianIndex = timeOffsetVec.size() / 2;
    int64_t medianTimeOffset;
    if (timeOffsetVec.size() % 2 == 0)
    {
        medianTimeOffset =
            (int64_t)(timeOffsetVec[medianIndex] + timeOffsetVec[medianIndex - 1]) / 2;
    }
    else
    {
        medianTimeOffset = timeOffsetVec[medianIndex];
    }
    if (std::abs(m_medianTimeOffset.load()) >= m_maxTimeOffset)
    {
        checkLocalTimeAndWarning();
    }
    m_medianTimeOffset.store(medianTimeOffset);
}

void NodeTimeMaintenance::checkLocalTimeAndWarning()
{
    // If nobody has a time different than ours but within m_minTimeOffset of ours, give a warning
    for (auto const& it : *m_node2TimeOffset)
    {
        if (std::abs(it.second) <= m_minTimeOffset)
        {
            return;
        }
    }
    TIME_LOG(WARNING) << LOG_DESC("Please check that your node's date and time are correct!")
                      << LOG_KV("medianTimeOffset", m_medianTimeOffset.load())
                      << LOG_KV("peersSize", m_node2TimeOffset->size());
}

int64_t NodeTimeMaintenance::getAlignedTime()
{
    return (utcTime() + m_medianTimeOffset.load());
}