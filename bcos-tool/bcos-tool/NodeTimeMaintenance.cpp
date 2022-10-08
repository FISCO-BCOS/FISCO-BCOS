/**
 * @brief : maintain the node time
 * @file: NodeTimeMaintenance.cpp
 * @author: yujiechen
 * @date: 2020-06-12
 */

#include "NodeTimeMaintenance.h"

#include <bcos-sync/utilities/Common.h>
#include <bcos-utilities/Common.h>

using namespace bcos::tool;

void NodeTimeMaintenance::tryToUpdatePeerTimeInfo(
    bcos::crypto::PublicPtr nodeID, const std::int64_t time)
{
    std::int64_t localTime = utcTime();
    auto peerTimeOffset = time - localTime;

    {
        Guard l(x_mutex);
        // The time information of the same node is within m_minTimeOffset,
        // and the time information of the node is not updated
        if (m_node2TimeOffset.count(nodeID))
        {
            auto orgTimeOffset = m_node2TimeOffset[nodeID];
            if (std::abs(orgTimeOffset - peerTimeOffset) <= m_minTimeOffset)
                return;

            m_node2TimeOffset[nodeID] = peerTimeOffset;
        }
        else
        {
            // update time information
            m_node2TimeOffset.insert(std::make_pair(nodeID, peerTimeOffset));
        }
    }

    // check remote time
    if (std::abs(peerTimeOffset) > m_maxTimeOffset)
    {
        TIMESYNC_LOG(WARNING)
            << LOG_DESC("Invalid remote peer time:  too much difference from local time")
            << LOG_KV("peer", nodeID->shortHex()) << LOG_KV("peerTime", time)
            << LOG_KV("localTime", localTime) << LOG_KV("medianTimeOffset", m_medianTimeOffset);
    }
    updateTimeInfo();

    TIMESYNC_LOG(INFO) << LOG_DESC("updateTimeInfo: update median time offset")
                       << LOG_KV("updatedMedianTimeOffset", m_medianTimeOffset)
                       << LOG_KV("peer", nodeID->shortHex()) << LOG_KV("peerTime", time)
                       << LOG_KV("peerOffset", peerTimeOffset) << LOG_KV("utcTime", utcTime());
}

void NodeTimeMaintenance::updateTimeInfo()
{
    // get median time offset
    std::vector<std::int64_t> timeOffsetVec;
    {
        Guard l(x_mutex);
        for (auto const& it : m_node2TimeOffset)
            timeOffsetVec.emplace_back(it.second);
    }
    std::sort(timeOffsetVec.begin(), timeOffsetVec.end());

    auto medianIndex = timeOffsetVec.size() >> 1;
    std::int64_t medianTimeOffset{ 0 };
    if (timeOffsetVec.size() % 2 == 0)
    {
        medianTimeOffset =
            (std::int64_t)(timeOffsetVec[medianIndex] + timeOffsetVec[medianIndex - 1]) >> 1;
    }
    else
    {
        medianTimeOffset = timeOffsetVec[medianIndex];
    }
    if (std::abs(m_medianTimeOffset) >= m_maxTimeOffset)
    {
        checkLocalTimeAndWarning(timeOffsetVec);
    }
    m_medianTimeOffset = medianTimeOffset;
}

void NodeTimeMaintenance::checkLocalTimeAndWarning(const std::vector<std::int64_t>& timeOffsetVec)
{
    // If nobody has a time different than ours but within m_minTimeOffset of ours, give a warning
    for (auto rIter = timeOffsetVec.rbegin(); rIter != timeOffsetVec.rend(); ++rIter)
    {
        if (std::abs(*rIter) > m_minTimeOffset)
        {
            TIMESYNC_LOG(WARNING) << LOG_DESC(
                                         "Please check that your node's date and time are correct!")
                                  << LOG_KV("medianTimeOffset", m_medianTimeOffset)
                                  << LOG_KV("peersSize", timeOffsetVec.size());
        }
        else
            break;
    }
}

std::int64_t NodeTimeMaintenance::getAlignedTime() const
{
    return (utcTime() + m_medianTimeOffset);
}