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
 * @brief : Implementation of Rotation PBFT Engine
 * @file: RPBFTReqCache.h
 * @author: yujiechen
 * @date: 2020-01-14
 */
#pragma once
#include "Common.h"
#include <libconsensus/pbft/PartiallyPBFTReqCache.h>

#define RPBFTReqCache_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("RPBFTReqCache")

namespace dev
{
namespace consensus
{
class RPBFTReqCache : public PartiallyPBFTReqCache
{
public:
    using Ptr = std::shared_ptr<RPBFTReqCache>;
    RPBFTReqCache() : PartiallyPBFTReqCache()
    {
        m_rawPrepareStatusCache = std::make_shared<std::map<dev::h512, PBFTMsg::Ptr>>();
        m_requestedPrepareQueue = std::make_shared<QueueSet<dev::h256>>();
    }

    ~RPBFTReqCache() override {}
    // update the rawPrepareStatus for _peerNodeId when receive the rawPrepare status message
    virtual bool updateRawPrepareStatus(dev::h512 const& _peerNodeId,
        PBFTMsg::Ptr _rawPrepareStatus, VIEWTYPE const& _currentView,
        int64_t const& _currentNumber);
    // select nodes to request the missed rawPrepare
    virtual PBFTMsg::Ptr selectNodeToRequestRawPrepare(
        dev::h512& _selectedNode, int64_t const& _blockNumber);

    virtual void responseRawPrepare(
        std::shared_ptr<dev::bytes> _encodedRawPrepare, PBFTMsg::Ptr _rawPrepareRequestMsg);

    virtual void insertRequestedRawPrepare(dev::h256 const& _hash);
    bool isRequestedPrepare(dev::h256 const& _hash);

    void delCache(dev::eth::BlockHeader const& _highestBlockHeader) override;

private:
    bool findTheRequestedRawPrepare(PBFTMsg::Ptr _rawPrepareRequestMsg);
    // compare _cachedRawPrepareStatus and _receivedRawPrepareStatus
    // return true only when the _cachedRawPrepareStatus is older than _receivedRawPrepareStatus
    bool checkRawPrepareStatus(
        PBFTMsg::Ptr _cachedRawPrepareStatus, PBFTMsg::Ptr _receivedRawPrepareStatus);

    void clearInvalidRawPrepareStatusCache(dev::eth::BlockHeader const& _highestBlockHeader);

private:
    // store maps between nodeId and the latest rawPrepareStatus of the node
    std::shared_ptr<std::map<dev::h512, PBFTMsg::Ptr>> m_rawPrepareStatusCache;
    mutable SharedMutex x_rawPrepareStatusCache;

    PBFTMsg::Ptr m_latestRawPrepare;
    mutable SharedMutex x_latestRawPrepare;

    // record the requested rawPrepareReq
    std::shared_ptr<QueueSet<dev::h256>> m_requestedPrepareQueue;
    size_t const m_maxRequestedPrepareQueueSize = 1024;
    mutable SharedMutex x_requestedPrepareQueue;
};
}  // namespace consensus
}  // namespace dev