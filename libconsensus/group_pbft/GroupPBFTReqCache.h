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
 * @brief : cache for group pbft
 * @file: GroupPBFTReqCache.h
 * @author: yujiechen
 * @date: 2019-5-28
 */
#pragma once
#include "Common.h"
#include "GroupPBFTMsg.h"
#include <libconsensus/pbft/PBFTReqCache.h>
namespace dev
{
namespace consensus
{
class GroupPBFTReqCache : public PBFTReqCache
{
public:
    bool superSignReqExists(std::shared_ptr<SuperSignReq> req, ZONETYPE const& zoneId)
    {
        return cacheExists(m_superSignCache, req->block_hash, zoneId);
    }

    bool superCommitReqExists(std::shared_ptr<SuperCommitReq> req, ZONETYPE const& zoneId)
    {
        return cacheExists(m_superSignCache, req->block_hash, zoneId);
    }

    void addSuperSignReq(std::shared_ptr<SuperSignReq> req, ZONETYPE const& zoneId)
    {
        m_superSignCache[req->block_hash][zoneId] = req;
    }
    void addSuperCommitReq(std::shared_ptr<SuperCommitReq> req, ZONETYPE const& zoneId)
    {
        m_superCommitCache[req->block_hash][zoneId] = req;
    }

    size_t getSuperSignCacheSize(h256 const& blockHash) const
    {
        return getSizeFromCache(blockHash, m_superSignCache);
    }

    size_t getSuperCommitCacheSize(h256 const& blockHash) const
    {
        return getSizeFromCache(blockHash, m_superCommitCache);
    }

    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperSignReq>>> const&
    superSignCache()
    {
        return m_superSignCache;
    }
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperCommitReq>>> const&
    superCommitCache()
    {
        return m_superCommitCache;
    }

    std::unordered_map<h256, std::unordered_map<std::string, std::shared_ptr<SignReq>>> const&
    signCache()
    {
        return m_signCache;
    }
    std::unordered_map<h256, std::unordered_map<std::string, std::shared_ptr<CommitReq>>> const&
    commitCache()
    {
        return m_commitCache;
    }

protected:
    // cache for SuperSignReq
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperSignReq>>>
        m_superSignCache;
    // cache for SuperCommitReq
    std::unordered_map<h256, std::unordered_map<ZONETYPE, std::shared_ptr<SuperCommitReq>>>
        m_superCommitCache;
};
}  // namespace consensus
}  // namespace dev
