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
 * @file: PBFTReqCache.h
 * @author: yujiechen
 *
 * @date: 2018-09-30
 */
#pragma once
#include <libconsensus/pbft/Common.h>
#include <libdevcore/easylog.h>
namespace dev
{
namespace consensus
{
class PBFTReqCache : public std::enable_shared_from_this<PBFTReqCache>
{
public:
    /**
     * @brief : specified prepareRequest exists in raw-prepare-cache or not?
     *
     * @param req : specified prepare request
     * @return true : the prepare request exists in the  raw-prepare-cache
     * @return false : the prepare request doesn't exist in the  raw-prepare-cache
     */
    inline bool isExistPrepare(PrepareReq const& req)
    {
        return m_rawPrepareCache.block_hash == req.block_hash;
    }

    inline bool isExistSign(SignReq const& req)
    {
        return cacheExists(m_signCache, req.block_hash, req.sig.hex());
    }

    inline bool isExistCommit(CommitReq const& req)
    {
        return cacheExists(m_commitCache, req.block_hash, req.sig.hex());
    }

    inline bool isExistViewChange(ViewChangeReq const& req)
    {
        return cacheExists(m_recvViewChangeReq, req.block_hash, req.idx);
    }

    inline u256 const getSigCacheSize(h256 const& blockHash)
    {
        return u256(m_signCache[blockHash].size());
    }
    inline u256 const getCommitCacheSize(h256 const& blockHash)
    {
        return u256(m_commitCache[blockHash].size());
    }

    inline u256 const getViewChangeSize(u256 const& toView)
    {
        return u256(m_recvViewChangeReq[toView].size());
    }

    inline PrepareReq const& prepareCache() { return m_prepareCache; }
    inline PrepareReq const& committedPrepareCache() { return m_committedPrepareCache; }
    inline PrepareReq const& futurePrepareCache() { return m_futurePrepareCache; }
    inline PrepareReq const& rawPrepareCache() { return m_rawPrepareCache; }


    inline void addRawPrepare(PrepareReq const& req)
    {
        m_rawPrepareCache = req;
        LOG(DEBUG) << "addRawPrepare: current raw_prepare:" << req.block_hash.abridged()
                   << "| reset prepare cache";
        m_prepareCache = PrepareReq();
    }

    inline void addPrepareReq(PrepareReq const& req)
    {
        m_prepareCache = req;
        removeInvalidSignCache(req.block_hash, req.view);
        removeInvalidCommitCache(req.block_hash, req.view);
    }
    inline void addSignReq(SignReq const& req) { m_signCache[req.block_hash][req.sig.hex()] = req; }
    inline void addCommitReq(CommitReq const& req)
    {
        m_commitCache[req.block_hash][req.sig.hex()] = req;
    }
    inline void addViewChangeReq(ViewChangeReq const& req)
    {
        m_recvViewChangeReq[req.view][req.idx] = req;
    }
    inline void updateCommittedPrepare() { m_committedPrepareCache = m_rawPrepareCache; }

    void generateAndSetSigList(dev::eth::Block& block, u256 const& minSigSize);

    bool canTriggerViewChange(u256& minView, u256 const& minInvalidNodeNum, u256 const& toView,
        dev::eth::BlockHeader const& highestBlock, int64_t const& consensusBlockNumber,
        ViewChangeReq const& req);

    inline void triggerViewChange(u256 const& curView)
    {
        m_rawPrepareCache.clear();
        m_prepareCache.clear();
        m_signCache.clear();
        m_commitCache.clear();
        removeInvalidViewChange(curView);
    }

    void delCache(h256 const& hash);
    inline void collectGarbage(dev::eth::BlockHeader const& highestBlockHeader)
    {
        removeInvalidEntryFromCache(highestBlockHeader, m_signCache);
        removeInvalidEntryFromCache(highestBlockHeader, m_commitCache);
    }
    void removeInvalidViewChange(u256 const& view, dev::eth::BlockHeader const& highestBlock);
    inline void delInvalidViewChange(dev::eth::BlockHeader const& curHeader)
    {
        removeInvalidEntryFromCache(curHeader, m_recvViewChangeReq);
    }
    inline void clearAll()
    {
        m_prepareCache.clear();
        m_rawPrepareCache.clear();
        m_signCache.clear();
        m_recvViewChangeReq.clear();
        m_commitCache.clear();
    }
    void addFuturePrepareCache(PrepareReq const& req);
    void resetFuturePrepare() { m_futurePrepareCache = PrepareReq(); }

private:
    template <typename T, typename U, typename S>
    void inline removeInvalidEntryFromCache(dev::eth::BlockHeader const& highestBlockHeader,
        std::unordered_map<T, std::unordered_map<U, S>>& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            for (auto cache_entry = it->second.begin(); cache_entry != it->second.end();)
            {
                if (cache_entry->second.height < highestBlockHeader.number())
                    cache_entry = it->second.erase(cache_entry);
                /// in case that fake block hash
                else if (cache_entry->second.height == highestBlockHeader.number() &&
                         cache_entry->second.block_hash != highestBlockHeader.hash())
                    cache_entry = it->second.erase(cache_entry);
                else
                    cache_entry++;
            }
            if (it->second.size() == 0)
                it = cache.erase(it);
            else
                it++;
        }
    }

    void removeInvalidViewChange(u256 const& curView);
    /// remove sign cache according to block hash and view
    void removeInvalidSignCache(h256 const& blockHash, u256 const& view);
    /// remove commit cache according to block hash and view
    void removeInvalidCommitCache(h256 const& blockHash, u256 const& view);

    template <typename T, typename S>
    inline bool cacheExists(T cache, h256 const& blockHash, S const& key)
    {
        auto it = cache.find(blockHash);
        if (it == cache.end())
            return false;
        return (it->second.find(key)) != (it->second.end());
    }

private:
    /// cache for prepare request
    PrepareReq m_prepareCache;
    /// cache for raw prepare request
    PrepareReq m_rawPrepareCache;
    /// maps between
    std::unordered_map<h256, std::unordered_map<std::string, SignReq>> m_signCache;
    /// maps between  and view change requests
    std::unordered_map<u256, std::unordered_map<u256, ViewChangeReq>> m_recvViewChangeReq;
    std::unordered_map<h256, std::unordered_map<std::string, CommitReq>> m_commitCache;
    PrepareReq m_committedPrepareCache;
    PrepareReq m_futurePrepareCache;
};
}  // namespace consensus
}  // namespace dev
