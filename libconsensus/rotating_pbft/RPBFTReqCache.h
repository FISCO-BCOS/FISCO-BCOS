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
 * @file: RPBFTReqCache.h
 * @author: yujiechen
 *
 * @date: 2019-11-13
 */
#pragma once
#include <libconsensus/pbft/PBFTReqCache.h>
#include <libethcore/PartiallyBlock.h>

#define RPBFTReqCache_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("RPBFTReqCache")

namespace dev
{
namespace consensus
{
class RPBFTReqCache : public PBFTReqCache
{
public:
    using Ptr = std::shared_ptr<RPBFTReqCache>;

    RPBFTReqCache() : PBFTReqCache()
    {
        m_partiallyFuturePrepare = std::make_shared<std::unordered_map<int64_t, PrepareReq::Ptr>>();
    }
    ~RPBFTReqCache() override { m_partiallyFuturePrepare->clear(); }

    virtual bool addPartiallyRawPrepare(PrepareReq::Ptr _partiallyRawPrepare);
    virtual void addPartiallyFuturePrepare(PrepareReq::Ptr _partiallyRawPrepare);
    PrepareReq::Ptr partiallyRawPrepare() { return m_partiallyRawPrepare; }
    virtual void transPartiallyPrepareIntoRawPrepare()
    {
        WriteGuard l(x_rawPrepareCache);
        m_rawPrepareCache = *m_partiallyRawPrepare;
    }

    // fetch missed transaction
    virtual bool fetchMissedTxs(std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo);
    // fill block with fetched transaction
    virtual bool fillBlock(bytesConstRef _txsData);


    PrepareReq::Ptr getPartiallyFuturePrepare(int64_t const& _consensusNumber);
    virtual void eraseHandledPartiallyFutureReq(int64_t const& _blockNumber);

    void removeInvalidFutureCache(int64_t const& _highestBlockNumber) override;

private:
    PrepareReq::Ptr m_partiallyRawPrepare;
    std::shared_ptr<std::unordered_map<int64_t, PrepareReq::Ptr>> m_partiallyFuturePrepare;
};
}  // namespace consensus
}  // namespace dev