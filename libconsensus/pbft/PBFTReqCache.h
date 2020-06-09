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
 * @file: PBFTReqCache.h
 * @author: yujiechen
 *
 * @date: 2018-09-30
 */
#pragma once
#include <libconsensus/pbft/Common.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/Protocol.h>
namespace dev
{
namespace consensus
{
class PBFTReqCache : public std::enable_shared_from_this<PBFTReqCache>
{
public:
    PBFTReqCache()
      : m_prepareCache(std::make_shared<PrepareReq>()),
        m_rawPrepareCache(std::make_shared<PrepareReq>()),
        m_latestViewChangeReqCache(
            std::make_shared<std::unordered_map<IDXTYPE, ViewChangeReq::Ptr>>())
    {}

    virtual ~PBFTReqCache() { m_futurePrepareCache.clear(); }
    /// specified prepareRequest exists in raw-prepare-cache or not?
    /// @return true : the prepare request exists in the  raw-prepare-cache
    /// @return false : the prepare request doesn't exist in the  raw-prepare-cache
    inline bool isExistPrepare(PrepareReq const& req)
    {
        ReadGuard l(x_rawPrepareCache);
        if (!m_rawPrepareCache)
        {
            return false;
        }
        return m_rawPrepareCache->block_hash == req.block_hash;
    }

    PrepareReq::Ptr rawPrepareCachePtr() { return m_rawPrepareCache; }
    /// specified SignReq exists in the sign-cache or not?
    inline bool isExistSign(SignReq const& req)
    {
        return cacheExists(m_signCache, req.block_hash, toHex(req.sig));
    }

    /// specified commitReq exists in the commit-cache or not?
    inline bool isExistCommit(CommitReq const& req)
    {
        return cacheExists(m_commitCache, req.block_hash, toHex(req.sig));
    }

    /// specified viewchangeReq exists in the viewchang-cache or not?
    inline bool isExistViewChange(ViewChangeReq const& req)
    {
        return cacheExists(m_recvViewChangeReq, req.view, req.idx);
    }

    /// get the size of the cached sign requests according to given block hash
    // _sizeToCheckFutureSign: the threshold size that should check signature for the future
    // requests generally 2*f+1
    inline size_t getSigCacheSize(h256 const& _blockHash, IDXTYPE const& _sizeToCheckFutureSign)
    {
        auto cachedSignSize = getSizeFromCache(_blockHash, m_signCache);
        // to avoid performance overhead,
        // only collect at least 2*f+1 sign requests,
        // should check signature for the future sign request
        if (cachedSignSize >= _sizeToCheckFutureSign)
        {
            // to demand the condition "only collect 2*f sign requests can procedure to
            // commit-period" of checkAndCommit
            // at most 2*f future sign requests can be stored in the future sign cache,
            // avoid the case：
            // 1. some node receive more than 2*f future sign requests before handle a sign request
            // 2. the node receive a sign request and procedure checkAndCommit,
            //    the number of collected sign requests is more than 2*f+1,
            //    and the node will not procedure to the commit-period
            auto limitedFutureSignSize = _sizeToCheckFutureSign - 1;
            return checkAndRemoveInvalidFutureReq(
                _blockHash, m_signCache, true, limitedFutureSignSize);
        }
        return cachedSignSize;
    }

    /// get the size of the cached commit requests according to given block hash
    inline size_t getCommitCacheSize(
        h256 const& _blockHash, IDXTYPE const& _sizeToCheckFutureCommit)
    {
        auto cachedCommitSize = getSizeFromCache(_blockHash, m_commitCache);
        if (cachedCommitSize >= _sizeToCheckFutureCommit)
        {
            auto limitedSize = _sizeToCheckFutureCommit - 1;
            return checkAndRemoveInvalidFutureReq(_blockHash, m_commitCache, false, limitedSize);
        }
        return cachedCommitSize;
    }

    // 1. check signature for the future requests when collecting sign/commit requests
    // 2. remove redundant future requests for the sign cache(at most 2*f future sign requests)
    template <typename T>
    size_t checkAndRemoveInvalidFutureReq(h256 const& _blockHash, T& _cache,
        bool const& _needLimitFutureReqSize, IDXTYPE const& _limitedSize)
    {
        size_t futureReqSize = 0;
        auto it = _cache.find(_blockHash);
        if (it == _cache.end())
        {
            return 0;
        }
        auto cachedItem = it->second;
        for (auto pCachedItem = cachedItem.begin(); pCachedItem != cachedItem.end();)
        {
            auto req = pCachedItem->second;
            // don't check the signature for the non-future req
            if (!req->isFuture)
            {
                pCachedItem++;
                continue;
            }
            // _needLimitFutureReqSize is used for sign cache
            // if the futureReq size is over _limitedSize(2*f), remove all the redundant future req
            if (_needLimitFutureReqSize && futureReqSize >= _limitedSize)
            {
                // remove all the redundant future req
                auto endIterator = cachedItem.end();
                removeAllRedundantFutureReq(cachedItem, pCachedItem, endIterator);
                break;
            }
            // the signature of the future-req has already been checked
            if (req->signChecked)
            {
                pCachedItem++;
                futureReqSize++;
                continue;
            }
            // check the signature for the future req
            if (m_checkSignCallback(*req))
            {
                // with valid signature
                req->signChecked = true;
                pCachedItem++;
                futureReqSize++;
                continue;
            }
            else
            {
                // with invalid signature, erase the req
                pCachedItem = cachedItem.erase(pCachedItem);
            }
        }
        if (cachedItem.size() == 0)
        {
            _cache.erase(it);
            return 0;
        }
        return cachedItem.size();
    }

    // remove all the redundant future req when collected enough futureReq(2*f)
    template <typename T, typename S>
    void removeAllRedundantFutureReq(T& _cache, S const& _startIterator, S const& _endIterator)
    {
        auto sizeBeforeRemove = _cache.size();
        for (auto it = _startIterator; it != _endIterator;)
        {
            auto req = it->second;
            if (req->isFuture)
            {
                it = _cache.erase(it);
            }
            else
            {
                it++;
            }
            PBFTReqCache_LOG(DEBUG)
                << LOG_DESC("removeAllRedundantFutureReq")
                << LOG_KV("hash", req->block_hash.abridged()) << LOG_KV("number", req->height)
                << LOG_KV("view", req->view) << LOG_KV("idx", req->idx);
        }
        PBFTReqCache_LOG(DEBUG) << LOG_DESC("removeAllRedundantFutureReq")
                                << LOG_KV("cacheSizeBeforeRemove", sizeBeforeRemove)
                                << LOG_KV("sizeAfterRemove", _cache.size());
    }

    /// get the size of cached viewchange requests according to given view
    inline size_t getViewChangeSize(VIEWTYPE const& toView) const
    {
        return getSizeFromCache(toView, m_recvViewChangeReq);
    }

    template <typename T, typename S>
    inline size_t getSizeFromCache(T const& key, S& cache) const
    {
        auto it = cache.find(key);
        if (it != cache.end())
        {
            return it->second.size();
        }
        return 0;
    }
    // only used for ut
    inline PrepareReq const& rawPrepareCache() { return *m_rawPrepareCache; }

    inline PrepareReq const& prepareCache() { return *m_prepareCache; }
    inline PrepareReq const& committedPrepareCache() { return m_committedPrepareCache; }

    // Note: when the threadPool backupDB, must add readGuard when access m_committedPrepareCache
    std::shared_ptr<bytes> committedPrepareBytes()
    {
        ReadGuard l(x_committedPrepareCache);
        std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
        m_committedPrepareCache.encode(*encodedData);
        return encodedData;
    }
    PrepareReq* mutableCommittedPrepareCache() { return &m_committedPrepareCache; }
    /// get the future prepare according to specified block hash
    inline std::shared_ptr<PrepareReq> futurePrepareCache(uint64_t const& blockNumber)
    {
        auto it = m_futurePrepareCache.find(blockNumber);
        if (it != m_futurePrepareCache.end())
        {
            return it->second;
        }
        return nullptr;
    }
    /// add specified raw-prepare-request into the raw-prepare-cache
    /// reset the prepare-cache
    virtual void addRawPrepare(PrepareReq::Ptr req)
    {
        auto startT = utcTime();
        WriteGuard l(x_rawPrepareCache);
        m_rawPrepareCache = req;
        m_prepareCache->clear();
        PBFTReqCache_LOG(INFO) << LOG_DESC("addRawPrepare") << LOG_KV("height", req->height)
                               << LOG_KV("reqIdx", req->idx)
                               << LOG_KV("hash", req->block_hash.abridged())
                               << LOG_KV("time", utcTime() - startT);
        if (m_randomSendRawPrepareStatusCallback)
        {
            m_randomSendRawPrepareStatusCallback(m_rawPrepareCache);
        }
    }

    /// add prepare request to prepare-cache
    /// remove cached request with the same block_hash but inconsistent view compaired with the
    /// specified prepare-request from the sign-cache and commit-cache
    inline void addPrepareReq(PrepareReq::Ptr req)
    {
        m_prepareCache = req;
        removeInvalidSignCache(req->block_hash, req->view);
        removeInvalidCommitCache(req->block_hash, req->view);
    }

    /// add specified signReq to the sign-cache
    inline void addSignReq(SignReq::Ptr req)
    {
        auto signature = toHex(req->sig);
        // determine existence: in case of assign overhead
        if (m_signCache.count(req->block_hash) && m_signCache[req->block_hash].count(signature))
        {
            return;
        }
        m_signCache[req->block_hash][signature] = req;
    }

    /// add specified commit cache to the commit-cache
    inline void addCommitReq(CommitReq::Ptr req)
    {
        auto signature = toHex(req->sig);
        // determine existence: in case of assign overhead
        if (m_commitCache.count(req->block_hash) && m_commitCache[req->block_hash].count(signature))
        {
            return;
        }
        m_commitCache[req->block_hash][signature] = req;
    }

    /// add specified viewchange cache to the viewchange-cache
    bool checkViewChangeReq(ViewChangeReq::Ptr _req, int64_t const& _blockNumber);
    void eraseExpiredViewChange(ViewChangeReq::Ptr _req, int64_t const& _blockNumber);

    // When the node restarts and the view becomes smaller, call this function to clear the history
    // cache
    void eraseLatestViewChangeCacheForNodeUpdated(ViewChangeReq const& _req);

    void addViewChangeReq(ViewChangeReq::Ptr _req, int64_t const& _blockNumber = 0);

    template <typename T, typename S>
    inline void addReq(std::shared_ptr<T> req, S& cache)
    {
        cache[req->block_hash][toHex(req->sig)] = req;
    }

    /// add future-prepare cache
    inline void addFuturePrepareCache(PrepareReq::Ptr req)
    {
        /// at most 20 future prepare cache
        if (m_futurePrepareCache.size() >= m_maxFuturePrepareCacheSize)
        {
            return;
        }
        auto it = m_futurePrepareCache.find(req->height);
        if (it == m_futurePrepareCache.end())
        {
            PBFTReqCache_LOG(INFO)
                << LOG_DESC("addFuturePrepareCache") << LOG_KV("height", req->height)
                << LOG_KV("reqView", req->view) << LOG_KV("reqIdx", req->idx)
                << LOG_KV("hash", req->block_hash.abridged());
            m_futurePrepareCache[req->height] = req;
        }
    }

    /// get the future prepare cache size
    inline size_t futurePrepareCacheSize() { return m_futurePrepareCache.size(); }

    /// update m_committedPrepareCache to m_rawPrepareCache before broadcast the commit-request
    inline void updateCommittedPrepare()
    {
        // Note: must add WriteGuard when updateCommittedPrepare
        //       since it may be accessed by the threadPool when backupDB
        WriteGuard commitLock(x_committedPrepareCache);
        ReadGuard l(x_rawPrepareCache);
        m_committedPrepareCache = *m_rawPrepareCache;
    }
    /// obtain the sig-list from m_commitCache, and append the sig-list to given block
    bool generateAndSetSigList(dev::eth::Block& block, const IDXTYPE& minSigSize);
    ///  determine can trigger viewchange or not
    bool canTriggerViewChange(VIEWTYPE& minView, IDXTYPE const& minInvalidNodeNum,
        VIEWTYPE const& toView, dev::eth::BlockHeader const& highestBlock,
        int64_t const& consensusBlockNumber);

    /// trigger viewchange
    void triggerViewChange(VIEWTYPE const& curView, int64_t const& _highestBlockNumber);

    /// delete requests cached in m_signCache, m_commitCache and m_prepareCache according to hash
    /// update the sign cache and commit cache immediately
    /// in case of that the commit/sign requests with the same hash are solved in
    /// handleCommitMsg/handleSignMsg again
    virtual void delCache(dev::eth::BlockHeader const& _highestBlockHeader);
    inline void collectGarbage(dev::eth::BlockHeader const& highestBlockHeader)
    {
        removeInvalidEntryFromCache(highestBlockHeader, m_signCache);
        removeInvalidEntryFromCache(highestBlockHeader, m_commitCache);
        /// remove invalid future block cache from cache
        removeInvalidFutureCache(highestBlockHeader.number());
        /// delete invalid viewchange from cache
        delInvalidViewChange(highestBlockHeader);
        removeInvalidLatestViewChangeReq(highestBlockHeader);
    }
    /// remove invalid view-change requests according to view and the current block header
    void removeInvalidViewChange(VIEWTYPE const& view, dev::eth::BlockHeader const& highestBlock);
    void removeInvalidLatestViewChangeReq(dev::eth::BlockHeader const& highestBlock);

    inline void delInvalidViewChange(dev::eth::BlockHeader const& curHeader)
    {
        removeInvalidEntryFromCache(curHeader, m_recvViewChangeReq);
    }
    inline void clearAllExceptCommitCache()
    {
        m_prepareCache->clear();
        m_signCache.clear();
        m_recvViewChangeReq.clear();
    }

    virtual void removeInvalidFutureCache(int64_t const& _highestBlockNumber);

    // only used for UT
    inline void clearAll()
    {
        WriteGuard l(x_rawPrepareCache);
        m_rawPrepareCache->clear();
        m_futurePrepareCache.clear();
        clearAllExceptCommitCache();
        m_commitCache.clear();
    }

    /// erase specified future request from the future prepare cache
    void eraseHandledFutureReq(uint64_t const& blockNumber)
    {
        if (m_futurePrepareCache.find(blockNumber) != m_futurePrepareCache.end())
        {
            m_futurePrepareCache.erase(blockNumber);
        }
    }
    /// complemented functions for UTs
    std::unordered_map<h256, std::unordered_map<std::string, SignReq::Ptr>>& mutableSignCache()
    {
        return m_signCache;
    }
    std::unordered_map<h256, std::unordered_map<std::string, CommitReq::Ptr>>& mutableCommitCache()
    {
        return m_commitCache;
    }

    std::unordered_map<VIEWTYPE, std::unordered_map<IDXTYPE, ViewChangeReq::Ptr>>&
    mutableViewChangeCache()
    {
        return m_recvViewChangeReq;
    }

    int64_t rawPrepareCacheHeight()
    {
        ReadGuard l(x_rawPrepareCache);
        return m_rawPrepareCache->height;
    }

    void setCheckSignCallback(std::function<bool(PBFTMsg const&)> const& _checkSignCallback)
    {
        m_checkSignCallback = _checkSignCallback;
    }

protected:
    /// remove invalid requests cached in cache according to current block
    template <typename T, typename U, typename S>
    void inline removeInvalidEntryFromCache(dev::eth::BlockHeader const& highestBlockHeader,
        std::unordered_map<T, std::unordered_map<U, S>>& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
        {
            for (auto cache_entry = it->second.begin(); cache_entry != it->second.end();)
            {
                /// delete expired cache
                if (cache_entry->second->height < highestBlockHeader.number())
                    cache_entry = it->second.erase(cache_entry);
                /// in case of faked block hash
                else if (cache_entry->second->height == highestBlockHeader.number() &&
                         cache_entry->second->block_hash != highestBlockHeader.hash())
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

    inline void removeInvalidViewChange(VIEWTYPE const& curView)
    {
        for (auto it = m_recvViewChangeReq.begin(); it != m_recvViewChangeReq.end();)
        {
            if (it->first <= curView)
                it = m_recvViewChangeReq.erase(it);
            else
                it++;
        }
        // remove invalid viewchange req from m_latestViewChangeReqCache
        for (auto it = m_latestViewChangeReqCache->begin();
             it != m_latestViewChangeReqCache->end();)
        {
            if (it->second->view <= curView)
            {
                it = m_latestViewChangeReqCache->erase(it);
            }
            else
            {
                it++;
            }
        }
    }
    /// remove sign cache according to block hash and view
    void removeInvalidSignCache(h256 const& blockHash, VIEWTYPE const& view);
    /// remove commit cache according to block hash and view
    void removeInvalidCommitCache(h256 const& blockHash, VIEWTYPE const& view);

    template <typename T, typename U, typename S>
    inline bool cacheExists(T const& cache, U const& mainKey, S const& key)
    {
        auto it = cache.find(mainKey);
        if (it == cache.end())
            return false;
        return (it->second.find(key)) != (it->second.end());
    }

protected:
    /// cache for prepare request
    PrepareReq::Ptr m_prepareCache;
    /// cache for raw prepare request
    PrepareReq::Ptr m_rawPrepareCache;
    /// cache for signReq(maps between hash and sign requests)
    std::unordered_map<h256, std::unordered_map<std::string, SignReq::Ptr>> m_signCache;

    /// cache for received-viewChange requests(maps between view and view change requests)
    std::unordered_map<VIEWTYPE, std::unordered_map<IDXTYPE, ViewChangeReq::Ptr>>
        m_recvViewChangeReq;
    // only record the latest view of all the nodes
    std::shared_ptr<std::unordered_map<IDXTYPE, ViewChangeReq::Ptr>> m_latestViewChangeReqCache;

    /// cache for commited requests(maps between hash and commited requests)
    std::unordered_map<h256, std::unordered_map<std::string, CommitReq::Ptr>> m_commitCache;
    /// cache for prepare request need to be backup and saved
    PrepareReq m_committedPrepareCache;
    mutable SharedMutex x_committedPrepareCache;
    /// cache for the future prepare cache
    /// key: block hash, value: the cached future prepeare
    std::unordered_map<uint64_t, std::shared_ptr<PrepareReq>> m_futurePrepareCache;
    const unsigned m_maxFuturePrepareCacheSize = 10;

    mutable SharedMutex x_rawPrepareCache;

    std::function<void(PBFTMsg::Ptr)> m_randomSendRawPrepareStatusCallback;
    std::function<bool(PBFTMsg const&)> m_checkSignCallback;
};
}  // namespace consensus
}  // namespace dev
