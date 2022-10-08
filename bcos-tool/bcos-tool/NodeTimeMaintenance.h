/**
 * @brief : maintain the node time
 * @file: NodeTimeMaintenance.h
 * @author: yujiechen
 * @date: 2020-06-12
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/KeyInterface.h>

#define TIMESYNC_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TIMESYNC")

namespace bcos
{
namespace tool
{

class NodeTimeMaintenance
{
public:
    using Ptr = std::shared_ptr<NodeTimeMaintenance>;

    void tryToUpdatePeerTimeInfo(bcos::crypto::PublicPtr nodeId, const std::int64_t time);
    int64_t getAlignedTime() const;
    int64_t medianTimeOffset() const { return m_medianTimeOffset; }

private:
    void updateTimeInfo();
    void checkLocalTimeAndWarning(const std::vector<std::int64_t>& timeOffsetVec);

private:
    // maps between nodeID and the timeOffset
    mutable Mutex x_mutex;
    std::map<bcos::crypto::PublicPtr, std::int64_t, bcos::crypto::KeyCompare> m_node2TimeOffset;

    std::atomic_int64_t m_medianTimeOffset{ 0 };

    // 30min
    std::int64_t m_maxTimeOffset{ 30 * 60 * 1000 };
    // 3min
    std::int64_t m_minTimeOffset{ 3 * 60 * 1000 };
};
}  // namespace sync
}  // namespace bcos