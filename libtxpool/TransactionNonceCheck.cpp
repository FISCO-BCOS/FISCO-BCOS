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
 * @file: TransactionNonceCheck.cpp
 * @author: toxotguo
 *
 * @date: 2017
 */

#include "TransactionNonceCheck.h"
#include <libdevcore/Common.h>

using namespace dev;
using namespace dev::eth;
using namespace dev::blockchain;
namespace dev
{
namespace txpool
{
void TransactionNonceCheck::init()
{
    m_startblk = 0;
    m_endblk = 0;
    updateCache(true);
}
bool TransactionNonceCheck::isBlockLimitOk(Transaction const& _tx)
{
    if (_tx.blockLimit() == Invalid256 || m_blockNumber >= _tx.blockLimit() ||
        _tx.blockLimit() > (m_blockNumber + m_maxBlockLimit))
    {
        NONCECHECKER_LOG(WARNING) << LOG_DESC("InvalidBlockLimit")
                                  << LOG_KV("blkLimit", _tx.blockLimit())
                                  << LOG_KV("maxBlkLimit", m_maxBlockLimit)
                                  << LOG_KV("curBlk", m_blockNumber) << LOG_KV("tx", _tx.hash());
        return false;
    }
    return true;
}

bool TransactionNonceCheck::ok(Transaction const& _transaction)
{
    return isBlockLimitOk(_transaction) && isNonceOk(_transaction);
}

std::shared_ptr<dev::txpool::NonceVec> TransactionNonceCheck::getNonceAndUpdateCache(
    int64_t const& blockNumber, bool const& update)
{
    std::shared_ptr<NonceVec> nonceVec;
    if (m_blockNonceCache.count(blockNumber))
    {
        nonceVec = m_blockNonceCache[blockNumber];
        NONCECHECKER_LOG(TRACE) << LOG_DESC("updateCache: getNonceAndUpdateCache cache hit ")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_KV("nonceSize", nonceVec->size())
                                << LOG_KV("nonceCacheSize", m_blockNonceCache.size());
    }
    /// block cache hit
    else if (m_blockChain->number() == blockNumber)
    {
        std::shared_ptr<Block> pBlock = m_blockChain->getBlockByNumber(blockNumber);
        if (pBlock)
        {
            nonceVec = pBlock->getAllNonces();
            NONCECHECKER_LOG(TRACE)
                << LOG_DESC("updateCache: getNonceAndUpdateCache block cache hit ")
                << LOG_KV("blockNumber", blockNumber) << LOG_KV("nonceSize", nonceVec->size())
                << LOG_KV("nonceCacheSize", m_blockNonceCache.size());
        }
        else
        {
            NONCECHECKER_LOG(WARNING)
                << LOG_DESC("Can't get block") << LOG_KV("blockNumber", blockNumber);
        }
    }
    else
    {
        nonceVec = m_blockChain->getNonces(blockNumber);
        NONCECHECKER_LOG(TRACE) << LOG_DESC("updateCache: getNonceAndUpdateCache cache miss ")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_KV("nonceSize", nonceVec->size())
                                << LOG_KV("nonceCacheSize", m_blockNonceCache.size());
    }
    if (update && m_blockNonceCache.size() < m_maxBlockLimit)
    {
        m_blockNonceCache[blockNumber] = nonceVec;
    }

    return nonceVec;
}

void TransactionNonceCheck::updateCache(bool _rebuild)
{
    WriteGuard l(m_lock);
    {
        try
        {
            Timer timer;
            m_blockNumber = m_blockChain->number();
            int64_t lastnumber = m_blockNumber;
            int64_t prestartblk = m_startblk;
            int64_t preendblk = m_endblk;

            m_endblk = lastnumber;
            if (lastnumber > m_maxBlockLimit)
            {
                m_startblk = lastnumber - m_maxBlockLimit;
            }
            else
            {
                m_startblk = 0;
            }

            NONCECHECKER_LOG(TRACE)
                << LOG_DESC("updateCache") << LOG_KV("rebuild", _rebuild)
                << LOG_KV("startBlk", m_startblk) << LOG_KV("endBlk", m_endblk)
                << LOG_KV("prestartBlk", prestartblk) << LOG_KV("preEndBlk", preendblk);
            if (_rebuild)
            {
                m_cache.clear();
                preendblk = 0;
            }
            else
            {
                /// erase the expired nonces
                for (auto i = prestartblk; i < m_startblk; i++)
                {
                    auto nonce_vec = getNonceAndUpdateCache(i, false);
                    if (nonce_vec)
                    {
                        for (auto& nonce : *nonce_vec)
                        {
                            m_cache.erase(nonce);
                        }
                    }
                    /// erase the expired nonces from cache since it can't be touched forever
                    if (m_blockNonceCache.count(i))
                    {
                        m_blockNonceCache.erase(i);
                    }
                }
            }
            /// insert the nonces of a new block
            for (auto i = std::max(preendblk + 1, m_startblk); i <= m_endblk; i++)
            {
                auto nonce_vec = getNonceAndUpdateCache(i);
                if (!nonce_vec)
                {
                    continue;
                }
                for (auto& nonce : *nonce_vec)
                {
                    m_cache.insert(nonce);
                }
            }  // for
            NONCECHECKER_LOG(DEBUG)
                << LOG_DESC("updateCache") << LOG_KV("nonceCacheSize", m_cache.size())
                << LOG_KV("costTime", timer.elapsed() * 1000);
        }
        catch (...)
        {
            // should not happen as exceptions
            NONCECHECKER_LOG(WARNING)
                << LOG_DESC("updateCache: update nonce cache failed")
                << LOG_KV("EINFO", boost::current_exception_diagnostic_information());
        }
    }
}  // fun
}  // namespace txpool
}  // namespace dev
