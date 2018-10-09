/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @brief : class for pbft request cache
 * @file: PBFTReqCache.cpp
 * @author: yujiechen
 *
 * @date: 2018-10-09
 */
#include "PBFTReqCache.h"
using namespace dev::eth;
namespace dev
{
namespace consensus
{
void inline PBFTReqCache::addRawPrepare(PrepareReq const& req)
{
    m_rawPrepareCache = req;
    LOG(DEBUG) << "addRawPrepare: current raw_prepare:" << req.block_hash.abridged()
               << "| reset prepare cache";
    m_prepareCache = PrepareReq();
}

void inline PBFTReqCache::addSignReq(SignReq const& req)
{
    m_signCache[req.block_hash][req.sig.hex()] = req;
}
void inline PBFTReqCache::addPrepareReq(PrepareReq const& req)
{
    m_prepareCache = req;
    removeInvalidSignCache(req.block_hash, req.view);
    removeInvalidCommitCache(req.block_hash, req.view);
}

void inline PBFTReqCache::addCommitReq(CommitReq const& req)
{
    m_commitCache[req.block_hash][req.sig.hex()] = req;
}
void inline PBFTReqCache::addViewChangeReq(ViewChangeReq const& req)
{
    m_recvViewChangeReq[req.view][req.idx] = req;
}

void inline PBFTReqCache::updateCommittedPrepare()
{
    m_committedPrepareCache = m_rawPrepareCache;
}

void PBFTReqCache::delCache(h256 const& hash)
{
    LOG(DEBUG) << "try to delete hash=" << hash << " from pbft cache";
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
        m_prepareCache.clear();
}

dev::eth::Block PBFTReqCache::generateAndSetSigList(u256 const& minSigSize)
{
    std::vector<std::pair<u256, Signature>> sig_list;
    for (auto item : m_commitCache[m_prepareCache.block_hash])
    {
        sig_list.push_back(std::make_pair(item.second.idx, Signature(item.first.c_str())));
    }
    assert(u256(sig_list.size()) >= minSigSize);
    /// set siglist for prepare cache
    dev::eth::Block block(m_prepareCache.block);
    block.setSigList(sig_list);
    return block;
}

bool PBFTReqCache::canTriggerViewChange(u256& minView, u256 const& minInvalidNodeNum,
    u256 const& toView, dev::eth::BlockHeader const& highestBlock,
    int64_t const& consensusBlockNumber, ViewChangeReq const& req)
{
    std::map<u256, u256> idx_view_map;
    minView = u256(0);
    int64_t min_height = 0;
    for (auto viewChangeItem : m_recvViewChangeReq)
    {
        if (viewChangeItem.first > toView)
        {
            for (auto viewChangeEntry : viewChangeItem.second)
            {
                auto it = idx_view_map.find(viewChangeEntry.first);

                if ((it != idx_view_map.end() ||
                        viewChangeItem.first > idx_view_map[viewChangeEntry.first]) &&
                    viewChangeEntry.second.height >= highestBlock.number())
                {
                    /// update to higher view
                    idx_view_map[viewChangeEntry.first] = viewChangeItem.first;
                    if (minView > viewChangeItem.first)
                        minView = viewChangeItem.first;
                    if (min_height > viewChangeEntry.second.height)
                        min_height = viewChangeEntry.second.height;
                }
            }
        }
    }
    u256 count = u256(idx_view_map.size());
    bool flag =
        (min_height == consensusBlockNumber) && (min_height == m_committedPrepareCache.height);
    return (count > minInvalidNodeNum) && !flag;
}

void inline PBFTReqCache::triggerViewChange(u256 const& curView)
{
    m_rawPrepareCache.clear();
    m_prepareCache.clear();
    m_signCache.clear();
    m_commitCache.clear();
    removeInvalidViewChange(curView);
}

void inline PBFTReqCache::collectGarbage(dev::eth::BlockHeader const& highestBlockHeader)
{
    removeInvalidEntryFromCache(highestBlockHeader, m_signCache);
    removeInvalidEntryFromCache(highestBlockHeader, m_commitCache);
}

void inline PBFTReqCache::clearAll()
{
    m_prepareCache.clear();
    m_rawPrepareCache.clear();
    m_signCache.clear();
    m_recvViewChangeReq.clear();
    m_commitCache.clear();
}

void PBFTReqCache::removeInvalidViewChange(
    u256 const& view, dev::eth::BlockHeader const& highestBlock)
{
    for (auto pview = m_recvViewChangeReq[view].begin(); pview != m_recvViewChangeReq[view].end();
         pview++)
    {
        if (pview->second.height < highestBlock.number())
            pview = m_recvViewChangeReq[view].erase(pview);
        else if (pview->second.height == highestBlock.number() &&
                 pview->second.block_hash != highestBlock.hash())
            pview = m_recvViewChangeReq[view].erase(pview);
        else
            pview++;
    }
}

void inline PBFTReqCache::addFuturePrepareCache(PrepareReq const& req)
{
    if (m_futurePrepareCache.block_hash != req.block_hash)
    {
        m_futurePrepareCache = req;
        LOG(INFO) << "recvFutureBlock, blk=" << req.height << ", hash=" << req.block_hash
                  << ", idx=" << req.idx;
    }
}

void inline PBFTReqCache::removeInvalidViewChange(u256 const& curView)
{
    for (auto it = m_recvViewChangeReq.begin(); it != m_recvViewChangeReq.end();)
    {
        if (it->first <= curView)
            it = m_recvViewChangeReq.erase(it);
        else
            it++;
    }
}

/// remove sign cache according to block hash and view
void inline PBFTReqCache::removeInvalidSignCache(h256 const& blockHash, u256 const& view)
{
    auto it = m_signCache.find(blockHash);
    if (it == m_signCache.end())
        return;
    auto signCache_entry = it->second;
    for (auto pcache = signCache_entry.begin(); pcache != signCache_entry.end();)
    {
        /// erase invalid view
        if (pcache->second.view != view)
            pcache = signCache_entry.erase(pcache);
        else
            pcache++;
    }
}
/// remove commit cache according to block hash and view
void inline PBFTReqCache::removeInvalidCommitCache(h256 const& blockHash, u256 const& view)
{
    auto it = m_commitCache.find(blockHash);
    if (it == m_commitCache.end())
        return;
    auto commitCache_entry = it->second;
    for (auto pcache = commitCache_entry.begin(); pcache != commitCache_entry.end();)
    {
        if (pcache->second.view != view)
            pcache = commitCache_entry.erase(pcache);
        else
            pcache++;
    }
}
}  // namespace consensus
}  // namespace dev
