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
 * @file: PartiallyPBFTReqCache.cpp
 * @author: yujiechen
 *
 * @date: 2019-11-13
 */
#include "PartiallyPBFTReqCache.h"
using namespace dev::consensus;
using namespace dev::eth;

bool PartiallyPBFTReqCache::addPartiallyRawPrepare(PrepareReq::Ptr _partiallyRawPrepare)
{
    // already cached the partially-raw-prepare
    if (m_partiallyRawPrepare &&
        m_partiallyRawPrepare->block_hash == _partiallyRawPrepare->block_hash)
    {
        return false;
    }
    m_partiallyRawPrepare = _partiallyRawPrepare;

    PartiallyPBFTReqCache_LOG(INFO)
        << LOG_DESC("addPartiallyRawPrepare") << LOG_KV("height", _partiallyRawPrepare->height)
        << LOG_KV("reqIdx", _partiallyRawPrepare->idx)
        << LOG_KV("hash", _partiallyRawPrepare->block_hash.abridged());
    return true;
}

bool PartiallyPBFTReqCache::fetchMissedTxs(
    std::shared_ptr<bytes> _encodedBytes, bytesConstRef _missInfo)
{
    PartiallyBlock::Ptr partiallyBlock = nullptr;
    dev::h256 expectedHash;
    // check the m_preRawPrepare
    {
        ReadGuard l(x_preRawPrepare);
        // fetch block from m_preRawPrepare
        if (m_preRawPrepare && m_preRawPrepare->pBlock)
        {
            partiallyBlock = std::dynamic_pointer_cast<PartiallyBlock>(m_preRawPrepare->pBlock);
            expectedHash = m_preRawPrepare->block_hash;
        }
    }
    if (!partiallyBlock)
    {
        // fetch from the rawPrepareCache
        // this lock is necessary since the transactions-request may occurred when rawPrepareCache
        // changed
        ReadGuard l(x_rawPrepareCache);
        // maybe the request-node falls behind
        if (!m_rawPrepareCache->pBlock)
        {
            return false;
        }
        partiallyBlock = std::dynamic_pointer_cast<PartiallyBlock>(m_rawPrepareCache->pBlock);
        expectedHash = m_rawPrepareCache->block_hash;
    }
    assert(partiallyBlock);
    partiallyBlock->fetchMissedTxs(_encodedBytes, _missInfo, expectedHash);
    PartiallyPBFTReqCache_LOG(DEBUG)
        << LOG_DESC("fetchMissedTxs") << LOG_KV("number", partiallyBlock->blockHeader().number())
        << LOG_KV("hash", partiallyBlock->blockHeader().hash().abridged())
        << LOG_KV("expectedHash", expectedHash.abridged());
    return true;
}

bool PartiallyPBFTReqCache::fillBlock(bytesConstRef _txsData)
{
    RLP blockRLP(_txsData);
    return fillBlock(m_partiallyRawPrepare, blockRLP);
}

bool PartiallyPBFTReqCache::fillPrepareCacheBlock(RLP const& _txsRLP)
{
    return fillBlock(m_partiallyRawPrepare, _txsRLP);
}

bool PartiallyPBFTReqCache::fillBlock(PrepareReq::Ptr _prepareReq, RLP const& _txsRLP)
{
    if (!_prepareReq || !_prepareReq->pBlock)
    {
        return false;
    }
    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(_prepareReq->pBlock);
    assert(partiallyBlock);
    partiallyBlock->fillBlock(_txsRLP);
    PartiallyPBFTReqCache_LOG(DEBUG)
        << LOG_DESC("fillBlock") << LOG_KV("number", partiallyBlock->blockHeader().number())
        << LOG_KV("hash", partiallyBlock->blockHeader().hash().abridged())
        << LOG_KV("fetchedTxs", partiallyBlock->missedTransactions()->size());
    return true;
}

void PartiallyPBFTReqCache::addPreRawPrepare(PrepareReq::Ptr _preRawPrepare)
{
    WriteGuard l(x_preRawPrepare);
    PartiallyPBFTReqCache_LOG(DEBUG)
        << LOG_DESC("addPreRawPrepare for the leader-self")
        << LOG_KV("number", _preRawPrepare->height)
        << LOG_KV("hash", _preRawPrepare->block_hash.abridged())
        << LOG_KV("idx", _preRawPrepare->idx) << LOG_KV("view", _preRawPrepare->view);
    m_preRawPrepare = _preRawPrepare;
}

void PartiallyPBFTReqCache::clearPreRawPrepare()
{
    WriteGuard l(x_preRawPrepare);
    m_preRawPrepare = nullptr;
}

void PartiallyPBFTReqCache::addPartiallyFuturePrepare(PrepareReq::Ptr _futurePrepare)
{
    if (!m_partiallyFuturePrepare)
    {
        m_partiallyFuturePrepare = _futurePrepare;
        return;
    }
    if ((_futurePrepare->height > m_partiallyFuturePrepare->height) ||
        (_futurePrepare->height == m_partiallyFuturePrepare->height &&
            _futurePrepare->view > m_partiallyFuturePrepare->view))
    {
        PartiallyPBFTReqCache_LOG(DEBUG)
            << LOG_DESC("addPartiallyFuturePrepare") << LOG_KV("number", _futurePrepare->height)
            << LOG_KV("hash", _futurePrepare->block_hash.abridged())
            << LOG_KV("idx", _futurePrepare->idx) << LOG_KV("view", _futurePrepare->view)
            << LOG_KV("cachedNumber", m_partiallyFuturePrepare->height)
            << LOG_KV("cachedView", m_partiallyFuturePrepare->view)
            << LOG_KV("cachedHash", m_partiallyFuturePrepare->block_hash.abridged());
        m_partiallyFuturePrepare = _futurePrepare;
    }
}

bool PartiallyPBFTReqCache::existInFuturePrepare(dev::h256 const& _blockHash)
{
    if (m_partiallyFuturePrepare && m_partiallyFuturePrepare->block_hash == _blockHash)
    {
        return true;
    }
    return false;
}

void PartiallyPBFTReqCache::fillFutureBlock(RLP const& _txsRLP)
{
    auto ret = fillBlock(m_partiallyFuturePrepare, _txsRLP);
    if (ret)
    {
        auto futurePrepare = m_partiallyFuturePrepare;
        m_partiallyFuturePrepare = nullptr;
        // re-encode the block into the completed block(for pbft-backup consideration)
        futurePrepare->pBlock->encode(*futurePrepare->block);
        // convert m_partiallyFuturePrepare into futurePrepare
        addFuturePrepareCache(futurePrepare);
        PartiallyPBFTReqCache_LOG(DEBUG)
            << LOG_DESC("fillFutureBlock and trans into the future prepareReq")
            << LOG_KV("number", futurePrepare->height)
            << LOG_KV("hash", futurePrepare->block_hash.abridged())
            << LOG_KV("idx", futurePrepare->idx) << LOG_KV("view", futurePrepare->view);
    }
}
