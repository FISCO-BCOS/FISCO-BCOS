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
 * @file: RPBFTReqCache.cpp
 * @author: yujiechen
 * @date: 2020-01-14
 */

#include "RPBFTReqCache.h"

using namespace dev::consensus;

bool RPBFTReqCache::checkReceivedRawPrepareStatus(dev::h512 const& _peerNodeId,
    PBFTMsg::Ptr _rawPrepareStatus, VIEWTYPE const& _currentView, int64_t const& _currentNumber)
{
    std::ostringstream outputInfo;
    outputInfo << LOG_KV("_peer", _peerNodeId.abridged()) << LOG_KV("currentView", _currentView)
               << LOG_KV("currentNumber", _currentNumber)
               << LOG_KV("reqHeight", _rawPrepareStatus->height)
               << LOG_KV("reqView", _rawPrepareStatus->view)
               << LOG_KV("reqHash", _rawPrepareStatus->block_hash.abridged())
               << LOG_KV("reqIdx", _rawPrepareStatus->idx);
    if (_rawPrepareStatus->height <= _currentNumber)
    {
        RPBFTReqCache_LOG(DEBUG) << LOG_DESC(
                                        "checkTheReceivedRawPrepareStatus: expired "
                                        "rawPrepareStatus for lower block number")
                                 << outputInfo.str();
        return false;
    }
    // the local rawPrepareCache already the newest
    {
        ReadGuard l(x_rawPrepareCache);
        if (!checkRawPrepareStatus(m_rawPrepareCache, _rawPrepareStatus))
        {
            RPBFTReqCache_LOG(DEBUG) << LOG_DESC(
                                            "checkTheReceivedRawPrepareStatus: expired "
                                            "rawPrepareStatus for older than rawPrepareCache")
                                     << outputInfo.str();
            return false;
        }
    }
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("checkReceivedRawPrepareStatus: valid rawPrepareStatus")
                             << outputInfo.str();
    return true;
}

bool RPBFTReqCache::checkRawPrepareStatus(
    PBFTMsg::Ptr _cachedRawPrepareStatus, PBFTMsg::Ptr _receivedRawPrepareStatus)
{
    if (!_cachedRawPrepareStatus)
    {
        return true;
    }
    // the received rawPrepareStatus is invalid for lower block height
    if (_cachedRawPrepareStatus->height > _receivedRawPrepareStatus->height)
    {
        return false;
    }
    // the received rawPrepareStatus is invalid for lower view
    if (_cachedRawPrepareStatus->height == _receivedRawPrepareStatus->height &&
        _cachedRawPrepareStatus->view >= _receivedRawPrepareStatus->view)
    {
        return false;
    }
    return true;
}

void RPBFTReqCache::insertRequestedRawPrepare(dev::h256 const& _hash)
{
    WriteGuard requestedPrepareHashLock(x_requestedPrepareQueue);
    if (m_requestedPrepareQueue->size() >= m_maxRequestedPrepareQueueSize)
    {
        m_requestedPrepareQueue->pop();
    }
    m_requestedPrepareQueue->push(_hash);
}

bool RPBFTReqCache::isRequestedPrepare(dev::h256 const& _hash)
{
    ReadGuard l(x_requestedPrepareQueue);
    // the rawPrepareReq has already been requested
    if (m_requestedPrepareQueue->count(_hash))
    {
        return true;
    }
    return false;
}

bool RPBFTReqCache::checkAndRequestRawPrepare(PBFTMsg::Ptr _pbftMsg)
{
    std::ostringstream outputInfo;
    outputInfo << LOG_KV("reqHeight", _pbftMsg->height)
               << LOG_KV("reqHash", _pbftMsg->block_hash.abridged())
               << LOG_KV("reqView", _pbftMsg->view) << LOG_KV("reqIdx", _pbftMsg->idx);
    if (isRequestedPrepare(_pbftMsg->block_hash))
    {
        RPBFTReqCache_LOG(DEBUG) << LOG_DESC("checkAndRequestRawPrepare: Duplicated")
                                 << outputInfo.str();
        return false;
    }
    insertRequestedRawPrepare(_pbftMsg->block_hash);
    return true;
}

// response the requested-rawPrepare
void RPBFTReqCache::responseRawPrepare(
    std::shared_ptr<dev::bytes> _encodedRawPrepare, PBFTMsg::Ptr _rawPrepareRequestMsg)
{
    std::ostringstream outputInfo;
    outputInfo << LOG_KV("reqHeight", _rawPrepareRequestMsg->height)
               << LOG_KV("reqHash", _rawPrepareRequestMsg->block_hash.abridged())
               << LOG_KV("reqView", _rawPrepareRequestMsg->view)
               << LOG_KV("reqIdx", _rawPrepareRequestMsg->idx);
    // doesn't find the requested-raw-prepare-req
    if (!findTheRequestedRawPrepare(_rawPrepareRequestMsg))
    {
        RPBFTReqCache_LOG(WARNING)
            << LOG_DESC("responseRawPrepare exceptioned for requested rawPrepareRequest not found")
            << outputInfo.str();
        BOOST_THROW_EXCEPTION(RequestedRawPrepareNotFound()
                              << errinfo_comment("requested rawPrepareRequest not found"));
    }
    // find the requested raw-prepare-req
    m_rawPrepareCache->encode(*_encodedRawPrepare);
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("responseRawPrepare") << outputInfo.str();
}

bool RPBFTReqCache::findTheRequestedRawPrepare(PBFTMsg::Ptr _rawPrepareRequestMsg)
{
    if (m_rawPrepareCache->block_hash != _rawPrepareRequestMsg->block_hash ||
        m_rawPrepareCache->height != _rawPrepareRequestMsg->height ||
        m_rawPrepareCache->idx != _rawPrepareRequestMsg->idx ||
        m_rawPrepareCache->view != _rawPrepareRequestMsg->view)
    {
        return false;
    }
    return true;
}
