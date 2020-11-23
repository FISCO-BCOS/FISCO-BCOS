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
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : class for pbft request cache
 * @file: PBFTReqCache.cpp
 * @author: yujiechen
 *
 * @date: 2018-10-09
 */
#include "PBFTReqCache.h"
#include <memory>

using namespace dev::eth;
namespace dev
{
namespace consensus
{
/**
 * @brief: delete requests cached in m_signCache, m_commitCache and m_prepareCache according to hash
 * @param hash
 */
void PBFTReqCache::delCache(dev::eth::BlockHeader const& _highestBlockHeader)
{
    // Note: the hash here must not be auto const& type, otherwise, the program will coredump
    auto hash = _highestBlockHeader.hash();
    PBFTReqCache_LOG(DEBUG) << LOG_DESC("delCache") << LOG_KV("hash", hash.abridged());
    /// delete from sign cache
    auto psign = m_signCache.find(hash);
    if (psign != m_signCache.end())
        m_signCache.erase(psign);
    /// delete from commit cache
    auto pcommit = m_commitCache.find(hash);
    if (pcommit != m_commitCache.end())
        m_commitCache.erase(pcommit);
    /// delete from prepare cache
    if (hash == m_prepareCache->block_hash)
    {
        m_prepareCache->clear();
    }
    // remove all the future prepare
    m_futurePrepareCache.clear();
}

/**
 * @brief: obtain the sig-list from m_commitCache
 *         and append the sig-list to given block
 * @param block: block need to append sig-list
 * @param minSigSize: minimum size of the sig list
 */
bool PBFTReqCache::generateAndSetSigList(dev::eth::Block& block, IDXTYPE const& minSigSize)
{
    auto sig_list = std::make_shared<std::vector<std::pair<u256, std::vector<unsigned char>>>>();
    if (m_commitCache.count(m_prepareCache->block_hash) > 0)
    {
        for (auto const& item : m_commitCache[m_prepareCache->block_hash])
        {
            sig_list->push_back(std::make_pair(u256(item.second->idx), fromHex(item.first)));
        }
        if (sig_list->size() < minSigSize)
        {
            return false;
        }
        /// set siglist for prepare cache
        block.setSigList(sig_list);
        return true;
    }
    return false;
}

// check the given viewChangeReq is valid
bool PBFTReqCache::checkViewChangeReq(ViewChangeReq::Ptr _req, int64_t const& _blockNumber)
{
    // check the blockHeight
    if (_req->height < _blockNumber)
    {
        PBFTReqCache_LOG(DEBUG) << LOG_DESC("return without addViewChangeReq for lower blockHeight")
                                << LOG_KV("reqIdx", _req->idx) << LOG_KV("reqHeight", _req->height)
                                << LOG_KV("blockNumber", _blockNumber);
        return false;
    }
    if (!m_latestViewChangeReqCache->count(_req->idx))
    {
        return true;
    }
    // already exist the viewChangeReq
    // only store the newest viewChangeReq
    auto viewChangeReq = (*m_latestViewChangeReqCache)[_req->idx];
    // remove the cached viewChangeReq with older Height
    if (viewChangeReq && viewChangeReq->height < _blockNumber)
    {
        eraseExpiredViewChange(viewChangeReq, _blockNumber);
        return true;
    }
    // the cached viewChangeReq with valid blockNumber
    if (_req->height < viewChangeReq->height)
    {
        PBFTReqCache_LOG(DEBUG) << LOG_DESC("return without addViewChangeReq for lower blockHeight")
                                << LOG_KV("reqIdx", _req->idx) << LOG_KV("reqHeight", _req->height)
                                << LOG_KV("cachedReqHeight", viewChangeReq->height);
        return false;
    }
    // the req with older view
    if (_req->view < viewChangeReq->view)
    {
        PBFTReqCache_LOG(DEBUG) << LOG_DESC("return without addViewChangeReq for lower view")
                                << LOG_KV("reqIdx", _req->idx) << LOG_KV("reqView", _req->view)
                                << LOG_KV("cachedReqView", viewChangeReq->view);
        return false;
    }
    return true;
}

// erase expired viewChangeReq from both m_latestViewChangeReq and m_recvViewChangeReq
void PBFTReqCache::eraseExpiredViewChange(ViewChangeReq::Ptr _req, int64_t const& _blockNumber)
{
    // remove older state from m_recvViewChangeReq
    if (m_latestViewChangeReqCache->count(_req->idx))
    {
        auto viewChangeReq = (*m_latestViewChangeReqCache)[_req->idx];
        if (viewChangeReq && m_recvViewChangeReq.count(viewChangeReq->view) &&
            m_recvViewChangeReq[viewChangeReq->view].count(_req->idx))
        {
            if (viewChangeReq == m_recvViewChangeReq[viewChangeReq->view][_req->idx])
            {
                m_recvViewChangeReq[viewChangeReq->view].erase(_req->idx);
            }
            m_latestViewChangeReqCache->erase(_req->idx);
            PBFTReqCache_LOG(DEBUG)
                << LOG_DESC("eraseExpiredViewChange") << LOG_KV("blkNumber", _blockNumber)
                << LOG_KV("cachedReqHeight", viewChangeReq->height)
                << LOG_KV("cachedReqIdx", viewChangeReq->idx)
                << LOG_KV("cachedReqHash", viewChangeReq->block_hash.abridged());
        }
    }
}

void PBFTReqCache::addViewChangeReq(ViewChangeReq::Ptr _req, int64_t const& _blockNumber)
{
    // only add valid viewChangeReq into the cache
    if (!checkViewChangeReq(_req, _blockNumber))
    {
        return;
    }
    // remove the expired viewChangeReq from cache
    // insert the viewchangeReq with newer state
    auto it = m_recvViewChangeReq.find(_req->view);
    if (it != m_recvViewChangeReq.end())
    {
        auto itv = it->second.find(_req->idx);
        if (itv != it->second.end())
        {
            itv->second = _req;
        }
        else
        {
            it->second.insert(std::make_pair(_req->idx, _req));
        }
    }
    else
    {
        std::unordered_map<IDXTYPE, ViewChangeReq::Ptr> viewMap;
        viewMap.insert(std::make_pair(_req->idx, _req));

        m_recvViewChangeReq.insert(std::make_pair(_req->view, viewMap));
    }

    // insert the viewChangeReq into m_latestViewChangeReq
    (*m_latestViewChangeReqCache)[_req->idx] = _req;
    PBFTReqCache_LOG(DEBUG) << LOG_DESC("addViewChangeReq") << LOG_KV("reqIdx", _req->idx)
                            << LOG_KV("reqHeight", _req->height)
                            << LOG_KV("curNumber", _blockNumber) << LOG_KV("reqView", _req->view)
                            << LOG_KV("reqHash", _req->block_hash.abridged());
}

/**
 * @brief: determine can trigger viewchange or not
 * @param minView: return value, the min view of the received-viewchange requests
 * @param minInvalidNodeNum: the min-valid num of received-viewchange-request required by trigger
 * viewchange
 * @param toView: next view, used to filter the received-viewchange-request
 * @param highestBlock: current block-header, used to filter the received-viewchange-request
 * @param consensusBlockNumber: number of the consensused block number
 * @return true: should trigger viewchange
 * @return false: can't trigger viewchange
 */
bool PBFTReqCache::canTriggerViewChange(VIEWTYPE& minView, IDXTYPE const& maxInvalidNodeNum,
    VIEWTYPE const& toView, dev::eth::BlockHeader const& highestBlock,
    int64_t const& consensusBlockNumber)
{
    std::map<IDXTYPE, VIEWTYPE> idx_view_map;
    minView = MAXVIEW;
    int64_t min_height = INT64_MAX;
    for (auto const& viewChangeItem : m_recvViewChangeReq)
    {
        if (viewChangeItem.first > toView)
        {
            for (auto const& viewChangeEntry : viewChangeItem.second)
            {
                auto it = idx_view_map.find(viewChangeEntry.first);
                if ((it == idx_view_map.end() || viewChangeItem.first > it->second) &&
                    viewChangeEntry.second->height >= highestBlock.number())
                {
                    /// update to lower view
                    if (it != idx_view_map.end())
                    {
                        it->second = viewChangeItem.first;
                    }
                    else
                    {
                        idx_view_map.insert(
                            std::make_pair(viewChangeEntry.first, viewChangeItem.first));
                    }

                    // idx_view_map[viewChangeEntry.first] = viewChangeItem.first;

                    if (minView > viewChangeItem.first)
                        minView = viewChangeItem.first;
                    /// update to lower height
                    if (min_height > viewChangeEntry.second->height)
                        min_height = viewChangeEntry.second->height;
                }
            }
        }
    }
    IDXTYPE count = idx_view_map.size();
    bool flag =
        (min_height == consensusBlockNumber) && (min_height == m_committedPrepareCache.height);
    return (count > maxInvalidNodeNum) && !flag;
}

/**
 * @brief: remove invalid view-change requests according to view and the current block header
 * @param view
 * @param highestBlock: the current block header
 */
void PBFTReqCache::removeInvalidViewChange(
    VIEWTYPE const& view, dev::eth::BlockHeader const& highestBlock)
{
    auto it = m_recvViewChangeReq.find(view);
    if (it == m_recvViewChangeReq.end())
    {
        return;
    }

    for (auto pview = it->second.begin(); pview != it->second.end();)
    {
        /// remove old received view-change
        if (pview->second->height < highestBlock.number())
            pview = it->second.erase(pview);
        /// remove invalid view-change request with invalid hash
        else if (pview->second->height == highestBlock.number() &&
                 pview->second->block_hash != highestBlock.hash())
            pview = it->second.erase(pview);
        else
            pview++;
    }
    removeInvalidLatestViewChangeReq(highestBlock);
}

void PBFTReqCache::removeInvalidLatestViewChangeReq(dev::eth::BlockHeader const& highestBlock)
{
    for (auto pLatestView = m_latestViewChangeReqCache->begin();
         pLatestView != m_latestViewChangeReqCache->end();)
    {
        // check block number
        if (pLatestView->second->height < highestBlock.number())
        {
            pLatestView = m_latestViewChangeReqCache->erase(pLatestView);
        }
        // check block number and hash
        else if (pLatestView->second->height == highestBlock.number() &&
                 pLatestView->second->block_hash != highestBlock.hash())
        {
            pLatestView = m_latestViewChangeReqCache->erase(pLatestView);
        }
        else
        {
            pLatestView++;
        }
    }
}

/// remove sign cache according to block hash and view
void PBFTReqCache::removeInvalidSignCache(h256 const& blockHash, VIEWTYPE const& view)
{
    auto it = m_signCache.find(blockHash);
    if (it == m_signCache.end())
        return;
    for (auto pcache = it->second.begin(); pcache != it->second.end();)
    {
        /// erase invalid view
        if (pcache->second->view != view)
            pcache = it->second.erase(pcache);
        else
            pcache++;
    }
}
/// remove commit cache according to block hash and view
void PBFTReqCache::removeInvalidCommitCache(h256 const& blockHash, VIEWTYPE const& view)
{
    auto it = m_commitCache.find(blockHash);
    if (it == m_commitCache.end())
        return;
    for (auto pcache = it->second.begin(); pcache != it->second.end();)
    {
        if (pcache->second->view != view)
            pcache = it->second.erase(pcache);
        else
            pcache++;
    }
}

/// clear the cache of future block to solve the memory leak problems
void PBFTReqCache::removeInvalidFutureCache(
    int64_t const& _highestBlockNumber, VIEWTYPE const& _view)
{
    for (auto pcache = m_futurePrepareCache.begin(); pcache != m_futurePrepareCache.end();)
    {
        if (pcache->first <= (uint64_t)(_highestBlockNumber))
        {
            pcache = m_futurePrepareCache.erase(pcache);
        }
        else if (pcache->second->view < _view)
        {
            pcache = m_futurePrepareCache.erase(pcache);
        }
        else
        {
            pcache++;
        }
    }
}

void PBFTReqCache::triggerViewChange(VIEWTYPE const& curView, int64_t const& _highestBlockNumber)
{
    WriteGuard l(x_rawPrepareCache);
    m_rawPrepareCache->clear();
    m_prepareCache->clear();
    // only remove the expired commitReq
    for (auto const& commitCacheIterator : m_commitCache)
    {
        removeExpiredCommitCache(commitCacheIterator.first, curView);
    }
    // remove expired signCache
    for (auto const& signCacheIterator : m_signCache)
    {
        // remove invalidSignCache
        removeExpiredSignCache(signCacheIterator.first, curView);
    }
    // go through all the items of m_signCache, only reserve signReq whose blockHash exists in
    // commitReqCache
    for (auto signCacheIterator = m_signCache.begin(); signCacheIterator != m_signCache.end();)
    {
        if (!m_commitCache.count(signCacheIterator->first))
        {
            signCacheIterator = m_signCache.erase(signCacheIterator);
        }
        else
        {
            signCacheIterator++;
        }
    }
    // remove the invalid future prepare cache
    removeInvalidFutureCache(_highestBlockNumber, curView);
    removeInvalidViewChange(curView);
}

void PBFTReqCache::eraseLatestViewChangeCacheForNodeUpdated(ViewChangeReq const& _req)
{
    m_latestViewChangeReqCache->erase(_req.idx);
}

void PBFTReqCache::removeExpiredSignCache(h256 const& _blockHash, VIEWTYPE const& _view)
{
    auto it = m_signCache.find(_blockHash);
    if (it == m_signCache.end())
        return;
    for (auto pcache = it->second.begin(); pcache != it->second.end();)
    {
        /// erase invalid view
        if (pcache->second->view < _view)
            pcache = it->second.erase(pcache);
        else
            pcache++;
    }
}

void PBFTReqCache::removeExpiredCommitCache(h256 const& _blockHash, VIEWTYPE const& _view)
{
    auto it = m_commitCache.find(_blockHash);
    if (it == m_commitCache.end())
        return;
    for (auto pcache = it->second.begin(); pcache != it->second.end();)
    {
        /// erase invalid view
        if (pcache->second->view < _view)
            pcache = it->second.erase(pcache);
        else
            pcache++;
    }
}

}  // namespace consensus
}  // namespace dev
