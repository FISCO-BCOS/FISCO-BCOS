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
 * @file: RPBFTReqCache.cpp
 * @author: yujiechen
 *
 * @date: 2019-11-13
 */
#include "RPBFTReqCache.h"
using namespace dev::consensus;
using namespace dev::eth;

bool RPBFTReqCache::addPartiallyRawPrepare(PrepareReq::Ptr _partiallyRawPrepare)
{
    // already cached the partially-raw-prepare
    if (m_partiallyRawPrepare &&
        m_partiallyRawPrepare->block_hash == _partiallyRawPrepare->block_hash)
    {
        return false;
    }
    // decode the partiallyBlock
    _partiallyRawPrepare->pBlock->decodeProposal(ref(*_partiallyRawPrepare->block), true);
    m_partiallyRawPrepare = _partiallyRawPrepare;

    RPBFTReqCache_LOG(INFO) << LOG_DESC("addPartiallyRawPrepare")
                            << LOG_KV("height", _partiallyRawPrepare->height)
                            << LOG_KV("reqIdx", _partiallyRawPrepare->idx)
                            << LOG_KV("hash", _partiallyRawPrepare->block_hash.abridged());
    return true;
}

void RPBFTReqCache::addPartiallyFuturePrepare(PrepareReq::Ptr _partiallyRawPrepare)
{
    if (m_partiallyFuturePrepare->size() >= 20)
    {
        return;
    }
    if (!m_partiallyFuturePrepare->count(_partiallyRawPrepare->height))
    {
        RPBFTReqCache_LOG(INFO) << LOG_DESC("addPartiallyFuturePrepare")
                                << LOG_KV("height", _partiallyRawPrepare->height)
                                << LOG_KV("reqIdx", _partiallyRawPrepare->idx)
                                << LOG_KV("hash", _partiallyRawPrepare->block_hash.abridged());
        (*m_partiallyFuturePrepare)[_partiallyRawPrepare->height] = _partiallyRawPrepare;
    }
}

bool RPBFTReqCache::fetchMissedTxs(std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo)
{
    // this lock is necessary since the transactions-request may occurred when rawPrepareCache
    // changed
    ReadGuard l(x_rawPrepareCache);
    if (!m_rawPrepareCache.pBlock)
    {
        return false;
    }
    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(m_rawPrepareCache.pBlock);
    assert(partiallyBlock);
    partiallyBlock->encodeMissedTxs(_encodedBytes, _missInfo);
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("fetchMissedTxs")
                             << LOG_KV("number", partiallyBlock->blockHeader().number())
                             << LOG_KV("hash", partiallyBlock->blockHeader().hash().abridged())
                             << LOG_KV("missedSize", partiallyBlock->missedTxs()->size());
    return true;
}

bool RPBFTReqCache::fillBlock(bytesConstRef _txsData)
{
    if (!m_partiallyRawPrepare || !m_partiallyRawPrepare->pBlock)
    {
        return false;
    }
    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(m_partiallyRawPrepare->pBlock);
    assert(partiallyBlock);
    partiallyBlock->fillBlock(_txsData);
    RPBFTReqCache_LOG(DEBUG) << LOG_DESC("fillBlock")
                             << LOG_KV("number", partiallyBlock->blockHeader().number())
                             << LOG_KV("hash", partiallyBlock->blockHeader().hash().abridged())
                             << LOG_KV("fetchedTxs", partiallyBlock->missedTransactions()->size());
    return true;
}

PrepareReq::Ptr RPBFTReqCache::getPartiallyFuturePrepare(int64_t const& _consensusNumber)
{
    if (!m_partiallyFuturePrepare->count(_consensusNumber))
    {
        return nullptr;
    }
    return (*m_partiallyFuturePrepare)[_consensusNumber];
}


void RPBFTReqCache::eraseHandledPartiallyFutureReq(int64_t const& _blockNumber)
{
    if (m_partiallyFuturePrepare->count(_blockNumber))
    {
        m_partiallyFuturePrepare->erase(_blockNumber);
    }
}

void RPBFTReqCache::removeInvalidFutureCache(int64_t const& _highestBlockNumber)
{
    PBFTReqCache::removeInvalidFutureCache(_highestBlockNumber);
    for (auto it = m_partiallyFuturePrepare->begin(); it != m_partiallyFuturePrepare->end();)
    {
        if (it->first <= _highestBlockNumber)
        {
            it = m_partiallyFuturePrepare->erase(it);
        }
        else
        {
            it++;
        }
    }
}
