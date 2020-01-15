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

bool RPBFTReqCache::updateRawPrepareStatus(dev::h512 const& _peerNodeId,
    PBFTMsg::Ptr _rawPrepareStatus, VIEWTYPE const& _currentView, int64_t const& _currentNumber)
{
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("prepare to updateRawPrepareStatus")
                             << LOG_KV("_peer", _peerNodeId.abridged())
                             << LOG_KV("currentView", _currentView)
                             << LOG_KV("currentNumber", _currentNumber)
                             << LOG_KV("reqHeight", _rawPrepareStatus->height)
                             << LOG_KV("reqView", _rawPrepareStatus->view)
                             << LOG_KV("reqHash", _rawPrepareStatus->block_hash.abridged())
                             << LOG_KV("reqIdx", _rawPrepareStatus->idx);
    if (_rawPrepareStatus->height <= _currentNumber || _rawPrepareStatus->view < _currentView)
    {
        return false;
    }
    // the local rawPrepareCache already the newest
    {
        ReadGuard l(x_rawPrepareCache);
        if (!checkRawPrepareStatus(m_rawPrepareCache, _rawPrepareStatus))
        {
            return false;
        }
    }
    {
        UpgradableGuard l(x_latestRawPrepare);
        // update the latestRawPrepare
        if (checkRawPrepareStatus(m_latestRawPrepare, _rawPrepareStatus))
        {
            UpgradeGuard ul(l);
            m_latestRawPrepare = _rawPrepareStatus;
        }
    }
    // hasn't cached the rawPrepareStatus, update directly
    UpgradableGuard l(x_rawPrepareStatusCache);
    if (!m_rawPrepareStatusCache->count(_peerNodeId))
    {
        (*m_rawPrepareStatusCache)[_peerNodeId] = _rawPrepareStatus;
        return true;
    }
    // already cached rawPrepareStatus from the given peer, check and update the
    // rawPrepareStatus
    auto cachedRawPrepareStatus = (*m_rawPrepareStatusCache)[_peerNodeId];

    if (!checkRawPrepareStatus(cachedRawPrepareStatus, _rawPrepareStatus))
    {
        return false;
    }
    UpgradeGuard ul(l);
    // update the cachedRawPrepareStatus
    (*m_rawPrepareStatusCache)[_peerNodeId] = _rawPrepareStatus;
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

PBFTMsg::Ptr RPBFTReqCache::selectNodeToRequestRawPrepare(
    dev::h512& _selectedNode, int64_t const& _expectedNumber)
{
    dev::h256 latestRawPrepareHash;
    {
        ReadGuard latestRawPrepareLock(x_latestRawPrepare);
        if (m_latestRawPrepare->height < _expectedNumber)
        {
            return nullptr;
        }
        latestRawPrepareHash = m_latestRawPrepare->block_hash;
    }
    if (isRequestedPrepare(latestRawPrepareHash))
    {
        return nullptr;
    }
    ReadGuard l(x_rawPrepareStatusCache);
    // has no the newest rawPrepareStatus
    if (m_rawPrepareStatusCache->size() == 0)
    {
        return nullptr;
    }
    // select node to request the newest-rawPrepare randomly
    std::srand(utcTime());
    auto it = m_rawPrepareStatusCache->begin();
    std::advance(it, std::rand() % m_rawPrepareStatusCache->size());
    _selectedNode = it->first;
    auto prepareStatusMsg = it->second;

    if (prepareStatusMsg->height != _expectedNumber)
    {
        prepareStatusMsg = nullptr;
        for (auto const& rawPrepareStatusCache : *m_rawPrepareStatusCache)
        {
            if (rawPrepareStatusCache.second->height == _expectedNumber)
            {
                prepareStatusMsg = rawPrepareStatusCache.second;
                _selectedNode = rawPrepareStatusCache.first;
                break;
            }
        }
    }
    if (!prepareStatusMsg)
    {
        return nullptr;
    }
    insertRequestedRawPrepare(prepareStatusMsg->block_hash);
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("selectNodeToRequestRawPrepare")
                             << LOG_KV("selectedNode", _selectedNode.abridged())
                             << LOG_KV("reqHeight", prepareStatusMsg->height)
                             << LOG_KV("reqView", prepareStatusMsg->view)
                             << LOG_KV("reqHash", prepareStatusMsg->block_hash.abridged())
                             << LOG_KV("reqIdx", prepareStatusMsg->idx);
    return prepareStatusMsg;
}

// reponse the requested-rawPrepare
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

void RPBFTReqCache::delCache(dev::eth::BlockHeader const& _highestBlockHeader)
{
    PBFTReqCache::delCache(_highestBlockHeader);
    clearInvalidRawPrepareStatusCache(_highestBlockHeader);
}

void RPBFTReqCache::clearInvalidRawPrepareStatusCache(
    dev::eth::BlockHeader const& _highestBlockHeader)
{
    for (auto it = m_rawPrepareStatusCache->begin(); it != m_rawPrepareStatusCache->end();)
    {
        if (it->second->height <= _highestBlockHeader.number())
        {
            it = m_rawPrepareStatusCache->erase(it);
        }
        else
        {
            it++;
        }
    }
}