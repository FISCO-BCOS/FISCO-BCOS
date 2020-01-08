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
    auto const& hash = _highestBlockHeader.hash();
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
    if (hash == m_prepareCache.block_hash)
    {
        m_prepareCache.clear();
    }
    removeInvalidFutureCache(_highestBlockHeader.number());
}

/**
 * @brief: obtain the sig-list from m_commitCache
 *         and append the sig-list to given block
 * @param block: block need to append sig-list
 * @param minSigSize: minimum size of the sig list
 */
bool PBFTReqCache::generateAndSetSigList(dev::eth::Block& block, IDXTYPE const& minSigSize)
{
    auto sig_list = std::make_shared<std::vector<std::pair<u256, Signature>>>();
    if (m_commitCache.count(m_prepareCache.block_hash) > 0)
    {
        for (auto const& item : m_commitCache[m_prepareCache.block_hash])
        {
            sig_list->push_back(
                std::make_pair(u256(item.second.idx), Signature(item.first.c_str())));
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

void PBFTReqCache::addViewChangeReq(ViewChangeReq const& req, int64_t const&)
{
    // insert the viewchangeReq with newer start
    auto it = m_recvViewChangeReq.find(req.view);
    if (it != m_recvViewChangeReq.end())
    {
        auto itv = it->second.find(req.idx);
        if (itv != it->second.end())
        {
            itv->second = req;
        }
        else
        {
            it->second.insert(std::make_pair(req.idx, req));
        }
    }
    else
    {
        std::unordered_map<IDXTYPE, ViewChangeReq> viewMap;
        viewMap.insert(std::make_pair(req.idx, req));

        m_recvViewChangeReq.insert(std::make_pair(req.view, viewMap));
    }
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
                    viewChangeEntry.second.height >= highestBlock.number())
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
                    if (min_height > viewChangeEntry.second.height)
                        min_height = viewChangeEntry.second.height;
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
        if (pview->second.height < highestBlock.number())
            pview = it->second.erase(pview);
        /// remove invalid view-change request with invalid hash
        else if (pview->second.height == highestBlock.number() &&
                 pview->second.block_hash != highestBlock.hash())
            pview = it->second.erase(pview);
        else
            pview++;
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
        if (pcache->second.view != view)
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
        if (pcache->second.view != view)
            pcache = it->second.erase(pcache);
        else
            pcache++;
    }
}

/// clear the cache of future block to solve the memory leak problems
void PBFTReqCache::removeInvalidFutureCache(int64_t const& _highestBlockNumber)
{
    for (auto pcache = m_futurePrepareCache.begin(); pcache != m_futurePrepareCache.end();)
    {
        if (pcache->first <= (uint64_t)(_highestBlockNumber))
        {
            pcache = m_futurePrepareCache.erase(pcache);
        }
        else
        {
            pcache++;
        }
    }
}
}  // namespace consensus
}  // namespace dev
