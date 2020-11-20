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

namespace bcos
{
namespace consensus
{
class RPBFTReqCache : public PartiallyPBFTReqCache
{
public:
    using Ptr = std::shared_ptr<RPBFTReqCache>;
    RPBFTReqCache() : PartiallyPBFTReqCache()
    {
        m_requestedPrepareQueue = std::make_shared<QueueSet<bcos::h256>>();
        m_nodeIDToPrepareStatus = std::make_shared<std::map<bcos::h512, PBFTMsg::Ptr>>();
        m_prepareHashToNodeID =
            std::make_shared<std::map<bcos::h256, std::shared_ptr<std::set<bcos::h512>>>>();
    }

    ~RPBFTReqCache() override {}

    // check the _rawPrepareStatus received from the given _peerNodeId is valid or not
    virtual bool checkReceivedRawPrepareStatus(PBFTMsg::Ptr _rawPrepareStatus,
        VIEWTYPE const& _currentView, int64_t const& _currentNumber);

    // check the given rawPrepare request message has been sent or not
    virtual bool checkAndRequestRawPrepare(PBFTMsg::Ptr _pbftMsg);

    // response rawPrepareCache when receive the rawPrepare request
    virtual void responseRawPrepare(
        std::shared_ptr<bcos::bytes> _encodedRawPrepare, PBFTMsg::Ptr _rawPrepareRequestMsg);

    // hook to send rawPrepareStatus randomly after addRawPrepare into the cache
    void setRandomSendRawPrepareStatusCallback(std::function<void(PBFTMsg::Ptr)> const& _callback)
    {
        m_randomSendRawPrepareStatusCallback = _callback;
    }

    // cache the block hash of requested-raw-prepare
    virtual void insertRequestedRawPrepare(bcos::h256 const& _hash);
    // check the given raw-prepare-request has been sent or not
    bool isRequestedPrepare(bcos::h256 const& _hash);

    void setMaxRequestedPrepareQueueSize(size_t const& _maxRequestedPrepareQueueSize)
    {
        m_maxRequestedPrepareQueueSize = _maxRequestedPrepareQueueSize;
    }

    std::shared_ptr<QueueSet<bcos::h256>> requestedPrepareQueue()
    {
        return m_requestedPrepareQueue;
    }

    void updateRawPrepareStatusCache(
        bcos::h512 const& _nodeId, PBFTMsg::Ptr _receivedRawPrepareStatus);

    bcos::h512 selectNodeToRequestTxs(
        bcos::h512 const& _expectedNodeID, PBFTMsg::Ptr _receivedRawPrepareStatus);
    void setMaxRequestMissedTxsWaitTime(uint64_t const& _maxRequestMissedTxsWaitTime)
    {
        m_maxRequestMissedTxsWaitTime = _maxRequestMissedTxsWaitTime;
    }

private:
    bool findTheRequestedRawPrepare(PBFTMsg::Ptr _rawPrepareRequestMsg);
    // compare _cachedRawPrepareStatus and _receivedRawPrepareStatus
    // return true only when the _cachedRawPrepareStatus is older than _receivedRawPrepareStatus
    bool checkRawPrepareStatus(
        PBFTMsg::Ptr _cachedRawPrepareStatus, PBFTMsg::Ptr _receivedRawPrepareStatus);

    bcos::h512 selectRequiredNodeToRequestTxs(
        bcos::h512 const& _expectedNodeID, PBFTMsg::Ptr _receivedRawPrepareStatus);

private:
    // record the requested rawPrepareReq
    std::shared_ptr<QueueSet<bcos::h256>> m_requestedPrepareQueue;
    size_t m_maxRequestedPrepareQueueSize = 1024;
    mutable SharedMutex x_requestedPrepareQueue;

    // signal when update the rawPrepareStatus
    boost::condition_variable m_signalled;
    boost::mutex x_signalled;

    // maps between nodeID and prepare status
    std::shared_ptr<std::map<bcos::h512, PBFTMsg::Ptr>> m_nodeIDToPrepareStatus;
    // maps between block hash and nodeID set
    std::shared_ptr<std::map<bcos::h256, std::shared_ptr<std::set<bcos::h512>>>>
        m_prepareHashToNodeID;
    mutable SharedMutex x_nodeIDToPrepareStatus;

    uint64_t m_maxRequestMissedTxsWaitTime = 100;
};
}  // namespace consensus
}  // namespace bcos
