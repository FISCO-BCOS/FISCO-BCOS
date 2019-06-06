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
 * @file: GroupPBFTEngine.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include "Common.h"
#include "GroupPBFTMsg.h"
#include "GroupPBFTReqCache.h"
#include <libconsensus/pbft/PBFTEngine.h>
namespace dev
{
namespace consensus
{
class GroupPBFTReqCache;
class GroupPBFTEngine : public PBFTEngine
{
public:
    GroupPBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> service,
        std::shared_ptr<dev::txpool::TxPoolInterface> txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> blockchain,
        std::shared_ptr<dev::sync::SyncInterface> sync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier,
        dev::PROTOCOL_ID const& protocolId, std::string const& baseDir, KeyPair const& keyPair,
        h512s const& sealerList = h512s())
      : PBFTEngine(service, txPool, blockchain, sync, blockVerifier, protocolId, baseDir, keyPair,
            sealerList)
    {
        m_groupBroadcastFilter = boost::bind(&GroupPBFTEngine::filterGroupNodeByNodeID, this, _1);
    }

    void setGroupSize(uint64_t const& groupSize)
    {
        m_configuredGroupSize = groupSize;
        GPBFTENGINE_LOG(INFO) << LOG_KV("configured groupSize", m_configuredGroupSize);
    }

    void setConsensusZoneSwitchBlockNumber(uint64_t const& zoneSwitchBlocks)
    {
        m_zoneSwitchBlocks = zoneSwitchBlocks;
    }

    void start() override;

    /// get the node index if the node is a sealer
    IDXTYPE nodeIdx() const override { return m_groupIdx; }

protected:
    /// below for grouping nodes into groups
    // reset config after commit a new block
    void resetConfig() override;

    /// below for shouldSeal
    // get current consensus zone
    virtual ZONETYPE getConsensusZone(int64_t const& blockNumber) const;
    // get leader if the current zone is the consensus zone
    std::pair<bool, IDXTYPE> getLeader() const override;
    bool isLeader();
    virtual bool locatedInConsensusZone(int64_t const& blockNumber) const;
    bool locatedInConsensusZone() const;

    // get node idx by nodeID
    ssize_t getIndexBySealer(dev::network::NodeID const& nodeId) override;
    IDXTYPE getNextLeader() const override;

    bool handlePrepareMsg(
        std::shared_ptr<PrepareReq> prepare_req, std::string const& endpoint = "self") override;

    // broadcast message among groups
    bool broadCastMsgAmongGroups(unsigned const& packetType, std::string const& key,
        bytesConstRef data, unsigned const& ttl = 0,
        std::unordered_set<dev::network::NodeID> const& filter =
            std::unordered_set<dev::network::NodeID>());
    // filter function when broad cast among groups
    virtual ssize_t filterGroupNodeByNodeID(dev::network::NodeID const& nodeId);


    IDXTYPE minValidNodes() const override { return m_zoneSize - m_FaultTolerance; }

    virtual ZONETYPE minValidGroups() const { return m_zoneNum - m_groupFaultTolerance; }

    virtual bool collectEnoughSignReq()
    {
        return collectEnoughReq(
            m_groupPBFTReqCache->signCache(), minValidNodes(), "collectSignReq", true);
    }
    virtual bool collectEnoughSuperSignReq(bool const& equalCondition = true)
    {
        return collectEnoughReq(m_groupPBFTReqCache->superSignCache(), minValidGroups(),
            "collectSuperSignReq", equalCondition);
    }
    virtual bool collectEnoughCommitReq(bool const& equalCondition = false)
    {
        bool ret = collectEnoughSuperSignReq(false);
        if (!ret)
        {
            return false;
        }
        return collectEnoughReq(m_groupPBFTReqCache->commitCache(), minValidNodes(),
            "collectCommitReq", equalCondition);
    }
    virtual bool collectEnoughSuperCommitReq()
    {
        bool ret = collectEnoughCommitReq(false);
        if (!ret)
        {
            return false;
        }
        return collectEnoughReq(m_groupPBFTReqCache->superCommitCache(), minValidGroups(),
            "collectSuperCommitReq", false);
    }


    void commonLogWhenCollectReq(size_t const& cacheSize, std::string const& desc)
    {
        GPBFTENGINE_LOG(INFO) << LOG_DESC(desc)
                              << LOG_KV("hash",
                                     m_groupPBFTReqCache->prepareCache()->block_hash.abridged())
                              << LOG_KV("collectedSize", cacheSize)
                              << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                              << LOG_KV("idx", m_idx);
    }


    template <typename T>
    bool collectEnoughReq(
        T const& cache, IDXTYPE const& minSize, std::string const& desc, bool const& equalCondition)
    {
        auto cacheSize = m_groupPBFTReqCache->getSizeFromCache(
            m_groupPBFTReqCache->prepareCache()->block_hash, cache);
        if (equalCondition && (cacheSize == (size_t)(minSize - 1)))
        {
            commonLogWhenCollectReq(cacheSize, desc);
            return true;
        }
        else if (!equalCondition && (cacheSize >= (size_t)(minSize - 1)))
        {
            commonLogWhenCollectReq(cacheSize, desc);
            return true;
        }
        return false;
    }

    // handle superSign message
    CheckResult isValidSuperSignReq(std::shared_ptr<SuperSignReq> superSignReq,
        ZONETYPE const& zoneId, std::ostringstream& oss) const
    {
        return isValidSuperReq(m_groupPBFTReqCache->superSignCache(), superSignReq, zoneId, oss);
    }

    virtual bool handleSuperSignReq(
        std::shared_ptr<SuperSignReq> superSignReq, PBFTMsgPacket const& pbftMsg);

    virtual bool broadcastSuperSignMsg()
    {
        // generate the superSignReq
        std::shared_ptr<SuperSignReq> req = std::make_shared<SuperSignReq>(
            m_groupPBFTReqCache->prepareCache(), VIEWTYPE(m_globalView), IDXTYPE(m_idx));
        // cache superSignReq
        m_groupPBFTReqCache->addSuperSignReq(req, m_zoneId);
        GPBFTENGINE_LOG(INFO) << LOG_DESC("checkAndCommit, broadcast and cache SuperSignReq")
                              << LOG_KV("height", req->height)
                              << LOG_KV("hash", req->block_hash.abridged())
                              << LOG_KV("globalView", m_globalView) << LOG_KV("idx", m_idx)
                              << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                              << LOG_KV("nodeId", m_keyPair.pub().abridged());
        return broadcastMsgToOtherGroups<SuperSignReq>(req);
    }

    template <typename T>
    void commonLogWhenHandleMsg(std::ostringstream& oss, std::string const& desc,
        ZONETYPE const& zoneId, std::shared_ptr<T> superReq, PBFTMsgPacket const& pbftMsg)
    {
        oss << LOG_DESC(desc) << LOG_KV("number", superReq->height)
            << LOG_KV("curBlkNum", m_highestBlock.number())
            << LOG_KV("hash", superReq->block_hash.abridged()) << LOG_KV("genIdx", superReq->idx)
            << LOG_KV("genZone", zoneId) << LOG_KV("curGlobalView", m_globalView)
            << LOG_KV("reqGloablView", superReq->view) << LOG_KV("fromIdx", pbftMsg.node_idx)
            << LOG_KV("fromIp", pbftMsg.endpoint) << LOG_KV("zone", m_zoneId)
            << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
    }

    virtual void checkAndBackupForSuperSignEnough();

    // handle superCommit message
    virtual bool handleSuperCommitReq(
        std::shared_ptr<SuperCommitReq> superCommitReq, PBFTMsgPacket const& pbftMsg);

    CheckResult isValidSuperCommitReq(std::shared_ptr<SuperCommitReq> superCommitReq,
        ZONETYPE const& zoneId, std::ostringstream& oss) const
    {
        return isValidSuperReq(
            m_groupPBFTReqCache->superCommitCache(), superCommitReq, zoneId, oss);
    }

    void checkSuperReqAndCommitBlock();
    virtual void printWhenCollectEnoughSuperReq(std::string const& desc, size_t superReqSize);

    virtual bool broadcastSuperCommitMsg()
    {
        // generate SuperCommitReq
        std::shared_ptr<SuperCommitReq> req = std::make_shared<SuperCommitReq>(
            m_groupPBFTReqCache->prepareCache(), VIEWTYPE(m_globalView), IDXTYPE(m_idx));
        // cache SuperCommitReq
        m_groupPBFTReqCache->addSuperCommitReq(req, m_zoneId);
        GPBFTENGINE_LOG(INFO) << LOG_DESC("checkAndSave, broadcast and cache SuperCommitReq")
                              << LOG_KV("height", req->height)
                              << LOG_KV("hash", req->block_hash.abridged())
                              << LOG_KV("globalView", m_globalView) << LOG_KV("idx", m_idx)
                              << LOG_KV("groupIdx", m_groupIdx) << LOG_KV("zoneId", m_zoneId)
                              << LOG_KV("nodeId", m_keyPair.pub().abridged());
        return broadcastMsgToOtherGroups<SuperCommitReq>(req);
    }


    void checkAndCommit() override;
    void checkAndSave() override;
    std::shared_ptr<PBFTMsg> handleMsg(std::string& key, PBFTMsgPacket const& pbftMsg) override;

private:
    template <class T, class S>
    CheckResult isValidSuperReq(S const& cache, std::shared_ptr<T> superReq, ZONETYPE const& zoneId,
        std::ostringstream& oss) const
    {
        if (m_groupPBFTReqCache->cacheExists(cache, superReq->block_hash, zoneId))
        {
            GPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("Invalid superReq: already cached!") << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        if (hasConsensused(superReq))
        {
            GPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("Invalid superReq: already consensued!") << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        return checkBasic(superReq, oss, false);
    }


    template <typename T>
    bool broadcastMsgToOtherGroups(std::shared_ptr<T> req, unsigned const& ttl = 2)
    {
        bytes encodedReq;
        req->encode(encodedReq);
        return broadCastMsgAmongGroups(
            GroupPBFTPacketType::SuperSignReqPacket, req->uniqueKey(), ref(encodedReq), ttl);
    }

    ZONETYPE getZoneByNodeIndex(IDXTYPE const& nodeIdx)
    {
        return (nodeIdx / m_configuredGroupSize);
    }

protected:
    // the zone that include this node
    std::atomic<ZONETYPE> m_zoneId = {0};
    // global view
    std::atomic<VIEWTYPE> m_globalView = {0};
    // configured group size
    std::atomic<int64_t> m_configuredGroupSize = {0};
    /// group idx
    std::atomic<int64_t> m_groupIdx = {0};
    /// real zone Size
    std::atomic<int64_t> m_zoneSize = {0};
    /// group num
    std::atomic<int64_t> m_zoneNum = {0};
    /// maxmum fault-tolerance inner a given group
    std::atomic<int64_t> m_FaultTolerance = {0};
    /// maxmum fault-tolerance among groups
    std::atomic<int64_t> m_groupFaultTolerance = {0};
    std::atomic<int64_t> m_zoneSwitchBlocks = {0};

    mutable SharedMutex x_broadCastListAmongGroups;
    std::set<h512> m_broadCastListAmongGroups;

    // functions
    std::function<ssize_t(dev::network::NodeID const&)> m_groupBroadcastFilter = nullptr;

    // pointers
    std::shared_ptr<GroupPBFTReqCache> m_groupPBFTReqCache = nullptr;
};
}  // namespace consensus
}  // namespace dev