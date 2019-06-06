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
 * @brief : implementation of Grouped-PBFT consensus engine
 * @file: GroupPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2019-5-28
 */
#include "GroupPBFTEngine.h"
#include "GroupPBFTMsg.h"
#include "GroupPBFTReqCache.h"
#include <libethcore/Exceptions.h>
using namespace dev::eth;

namespace dev
{
namespace consensus
{
void GroupPBFTEngine::start()
{
    PBFTEngine::start();
    m_groupPBFTReqCache = std::dynamic_pointer_cast<GroupPBFTReqCache>(m_reqCache);
    GPBFTENGINE_LOG(INFO) << "[Start GroupPBFTEngine...]";
}

/// resetConfig for group PBFTEngine
void GroupPBFTEngine::resetConfig()
{
    updateMaxBlockTransactions();
    m_idx = MAXIDX;
    updateConsensusNodeList();
    {
        ReadGuard l(m_sealerListMutex);
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            if (m_sealerList[i] == m_keyPair.pub())
            {
                m_accountType = NodeAccountType::SealerAccount;
                m_idx = i;
                break;
            }
        }
        m_nodeNum = m_sealerList.size();
    }
    if (m_nodeNum < 1)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC(
            "Must set at least one pbft sealer, current number of sealers is zero");
        BOOST_THROW_EXCEPTION(
            EmptySealers() << errinfo_comment("Must set at least one pbft sealer!"));
    }
    m_cfgErr = (m_idx == MAXIDX);
    if (m_cfgErr)
    {
        return;
    }
    // obtain zone id of this node
    m_zoneId = m_idx / m_configuredGroupSize;
    // calculate current zone num
    m_zoneNum = m_nodeNum + (m_configuredGroupSize - 1) / m_configuredGroupSize;
    // get zone size
    if (m_nodeNum - m_idx < m_configuredGroupSize)
    {
        m_zoneSize = m_nodeNum - m_idx;
    }
    else
    {
        m_zoneSize = int64_t(m_configuredGroupSize);
    }
    // calculate the idx of the node in a given group
    auto origin_groupIdx = int64_t(m_groupIdx);
    m_groupIdx = m_idx % m_zoneSize;
    m_FaultTolerance = (m_zoneSize - 1) / 3;
    m_groupFaultTolerance = (m_zoneNum - 1) / 3;

    // update the node list of other groups that can receive the network packet of this node if the
    // idx has been updated
    if (origin_groupIdx != m_groupIdx)
    {
        WriteGuard l(x_broadCastListAmongGroups);
        ReadGuard l_sealers(m_sealerListMutex);
        m_broadCastListAmongGroups.clear();
        int64_t i = 0;
        for (i = 0; i < m_zoneNum; i++)
        {
            auto nodeIndex = i * m_configuredGroupSize + m_groupIdx;
            if (i == m_zoneId)
            {
                continue;
            }
            if ((uint64_t)(nodeIndex) >= m_sealerList.size())
            {
                break;
            }
            m_broadCastListAmongGroups.insert(m_sealerList[nodeIndex]);
        }
    }
    GPBFTENGINE_LOG(INFO) << LOG_DESC("reset configure") << LOG_KV("totalNode", m_nodeNum)
                          << LOG_KV("globalIdx", m_idx) << LOG_KV("zoneNum", m_zoneNum)
                          << LOG_KV("zoneId", m_zoneId) << LOG_KV("zoneSize", m_zoneSize)
                          << LOG_KV("groupIdx", m_groupIdx)
                          << LOG_KV("faultTolerance", m_FaultTolerance)
                          << LOG_KV("groupFaultTolerance", m_groupFaultTolerance)
                          << LOG_KV("gIdx", m_groupIdx) << LOG_KV("idx", m_idx);
}

/// get consensus zone:
/// consensus_zone = (globalView + currentBlockNumber % m_zoneSwitchBlocks) % zone_size
ZONETYPE GroupPBFTEngine::getConsensusZone(int64_t const& blockNumber) const
{
    /// get consensus zone failed
    if (m_cfgErr || m_highestBlock.sealer() == Invalid256)
    {
        return MAXIDX;
    }
    return (m_globalView + blockNumber % m_zoneSwitchBlocks) % m_zoneNum;
}

// determine the given node located in the consensus zone or not
bool GroupPBFTEngine::locatedInConsensusZone(int64_t const& blockNumber) const
{
    if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256)
    {
        return false;
    }
    // get the current consensus zone
    ZONETYPE consZone = getConsensusZone(blockNumber);
    // get consensus zone failed or the node is not in the consens zone
    if (consZone == MAXIDX || consZone != m_zoneId)
    {
        return false;
    }
    return true;
}

/// get current leader
std::pair<bool, IDXTYPE> GroupPBFTEngine::getLeader() const
{
    // the node is not in the consensus group
    if (!locatedInConsensusZone(m_highestBlock.number()))
    {
        return std::make_pair(false, MAXIDX);
    }
    return std::make_pair(true, (m_view + m_highestBlock.number()) % m_zoneSize);
}

/// get next leader
IDXTYPE GroupPBFTEngine::getNextLeader() const
{
    auto expectedBlockNumber = m_highestBlock.number() + 1;
    /// the next leader is not located in this Zone
    if (!locatedInConsensusZone(expectedBlockNumber))
    {
        return MAXIDX;
    }
    return (m_view + expectedBlockNumber) % m_zoneSize;
}

/// get nodeIdx according to nodeID
/// override the function to make sure the node only broadcast message to the nodes that belongs to
/// m_zoneId
ssize_t GroupPBFTEngine::getIndexBySealer(dev::network::NodeID const& nodeId)
{
    ssize_t nodeIndex = PBFTEngine::getIndexBySealer(nodeId);
    /// observer or not in the zone
    if (nodeIndex == -1)
    {
        return -1;
    }
    ssize_t nodeIdxInGroup = nodeIndex / m_configuredGroupSize;
    if (nodeIdxInGroup != m_zoneId)
    {
        return -1;
    }
    return nodeIdxInGroup;
}


bool GroupPBFTEngine::broadCastMsgAmongGroups(unsigned const& packetType, std::string const& key,
    bytesConstRef data, unsigned const& ttl, std::unordered_set<dev::network::NodeID> const& filter)
{
    return PBFTEngine::broadcastMsg(packetType, key, data, filter, ttl, m_groupBroadcastFilter);
}

ssize_t GroupPBFTEngine::filterGroupNodeByNodeID(dev::network::NodeID const& nodeId)
{
    // should broadcast to the node
    if (m_broadCastListAmongGroups.count(nodeId))
    {
        return 1;
    }
    // shouldn't broadcast to the node
    return -1;
}

bool GroupPBFTEngine::isLeader()
{
    auto leader = getLeader();
    if (leader.first && leader.second == m_groupIdx)
    {
        return true;
    }
    return false;
}

bool GroupPBFTEngine::locatedInConsensusZone() const
{
    return locatedInConsensusZone(m_highestBlock.number());
}


bool GroupPBFTEngine::handlePrepareMsg(
    std::shared_ptr<PrepareReq> prepare_req, std::string const& endpoint)
{
    // nodes of the consensus zone broadcast encoded Prepare packet to nodes located in other groups
    if (locatedInConsensusZone())
    {
        bytes prepare_data;
        prepare_req->encode(prepare_data);
        broadCastMsgAmongGroups(
            GroupPBFTPacketType::PrepareReqPacket, prepare_req->uniqueKey(), ref(prepare_data));
    }
    return PBFTEngine::handlePrepareMsg(prepare_req, endpoint);
}

/**
 * @brief
 *
 * @param superSignReq
 * @param endpoint
 * @return true
 * @return false
 */
bool GroupPBFTEngine::handleSuperSignReq(
    std::shared_ptr<SuperSignReq> superSignReq, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(superSignReq, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    auto zoneId = getZoneByNodeIndex(superSignReq->idx);
    commonLogWhenHandleMsg(oss, "handleSuperSignReq", zoneId, superSignReq, pbftMsg);
    // check superSignReq
    CheckResult ret = isValidSuperSignReq(superSignReq, zoneId, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    // add future superSignReq
    if ((ret == CheckResult::FUTURE) &&
        (m_groupPBFTReqCache->getSuperSignCacheSize(superSignReq->block_hash) <=
            (size_t)(minValidGroups() - 1)))
    {
        m_groupPBFTReqCache->addSuperSignReq(superSignReq, zoneId);
        return true;
    }
    m_groupPBFTReqCache->addSuperSignReq(superSignReq, zoneId);
    checkAndBackupForSuperSignEnough();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("handleSuperSignMsg succ") << LOG_KV("INFO", oss.str());
    return true;
}


void GroupPBFTEngine::printWhenCollectEnoughSuperReq(std::string const& desc, size_t superReqSize)
{
    GPBFTENGINE_LOG(INFO) << LOG_DESC(desc)
                          << LOG_KV(
                                 "hash", m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
                          << LOG_KV("height", m_groupPBFTReqCache->prepareCache()->height)
                          << LOG_KV("SuperReqSize", superReqSize) << LOG_KV("zone", m_zoneId)
                          << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

/// collect superSignReq and broadcastCommitReq if collected enough superSignReq
void GroupPBFTEngine::checkAndBackupForSuperSignEnough()
{
    auto superSignSize =
        m_groupPBFTReqCache->getSuperSignCacheSize(m_groupPBFTReqCache->prepareCache()->block_hash);
    // broadcast commit message
    if (superSignSize == (size_t)(minValidGroups() - 1))
    {
        printWhenCollectEnoughSuperReq(
            "checkAndBackupForSuperSignEnough: collect enough superSignReq, backup prepare request "
            "to PBFT backup DB",
            superSignSize);

        // backup signReq
        m_groupPBFTReqCache->updateCommittedPrepare();
        backupMsg(c_backupKeyCommitted, m_groupPBFTReqCache->committedPrepareCache());
        // broadcast commit message
        if (!broadcastCommitReq(*(m_groupPBFTReqCache->prepareCache())))
        {
            GPBFTENGINE_LOG(WARNING)
                << LOG_DESC("checkAndBackupForSuperSignEnough: broadcastCommitReq failed");
        }
    }
}

/**
 * @brief: called by handleSignMsg:
 *         1. check the collected signReq inner the group are enough or not
 *         2. broadcast superSignReq if the collected signReq are enough
 */
void GroupPBFTEngine::checkAndCommit()
{
    if (!collectEnoughSignReq())
    {
        return;
    }
    broadcastSuperSignMsg();
}

/**
 * @brief : called by handleCommitMsg
 *          1. check the collected commitReq inside of the group are enough or not
 *          2. broadcast superCommitReq if the collected commitReq are enough
 */
void GroupPBFTEngine::checkAndSave()
{
    if (!collectEnoughCommitReq())
    {
        return;
    }
    broadcastSuperCommitMsg();
}

bool GroupPBFTEngine::handleSuperCommitReq(
    std::shared_ptr<SuperCommitReq> superCommitReq, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(superCommitReq, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    auto zoneId = getZoneByNodeIndex(superCommitReq->idx);
    std::ostringstream oss;
    commonLogWhenHandleMsg(oss, "handleSuperCommitReq", zoneId, superCommitReq, pbftMsg);
    CheckResult ret = isValidSuperCommitReq(superCommitReq, zoneId, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    m_groupPBFTReqCache->addSuperCommitReq(superCommitReq, zoneId);
    if (ret == CheckResult::FUTURE)
    {
        return true;
    }
    checkSuperReqAndCommitBlock();
    GPBFTENGINE_LOG(INFO) << LOG_DESC("handleSuperCommitMsg succ") << LOG_KV("INFO", oss.str());
    return true;
}

void GroupPBFTEngine::checkSuperReqAndCommitBlock()
{
    if (!collectEnoughSuperCommitReq())
    {
        return;
    }
    auto superCommitSize = m_groupPBFTReqCache->getSizeFromCache(
        m_groupPBFTReqCache->prepareCache()->block_hash, m_groupPBFTReqCache->superCommitCache());
    PBFTEngine::checkAndCommitBlock(superCommitSize);
}

std::shared_ptr<PBFTMsg> GroupPBFTEngine::handleMsg(std::string& key, PBFTMsgPacket const& pbftMsg)
{
    bool succ = false;
    std::shared_ptr<PBFTMsg> pbftPacket = nullptr;
    switch (pbftMsg.packet_id)
    {
    case GroupPBFTPacketType::SuperSignReqPacket:
    {
        std::shared_ptr<SuperSignReq> superSignReq = std::make_shared<SuperSignReq>();
        succ = handleSuperSignReq(superSignReq, pbftMsg);
        key = superSignReq->uniqueKey();
        pbftPacket = superSignReq;
    }
    case GroupPBFTPacketType::SuperCommitReqPacket:
    {
        std::shared_ptr<SuperCommitReq> superCommitReq = std::make_shared<SuperCommitReq>();
        succ = handleSuperCommitReq(superCommitReq, pbftMsg);
        key = superCommitReq->uniqueKey();
        pbftPacket = superCommitReq;
    }
    default:
    {
        return PBFTEngine::handleMsg(key, pbftMsg);
    }
    }
    if (!succ)
    {
        return nullptr;
    }
    return pbftPacket;
}

}  // namespace consensus
}  // namespace dev