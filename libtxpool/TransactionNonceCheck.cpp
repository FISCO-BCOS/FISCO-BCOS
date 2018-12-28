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
    if (_tx.blockLimit() == Invalid256 || u256(m_blockChain->number()) >= _tx.blockLimit() ||
        _tx.blockLimit() > (u256(m_blockChain->number()) + m_maxBlockLimit))
    {
        NONCECHECKER_LOG(ERROR) << "[#Verify] [#InvalidBlockLimit] invalid blockLimit: "
                                   "[blkLimit/maxBlkLimit/number/tx]:  "
                                << _tx.blockLimit() << "/" << m_maxBlockLimit << "/"
                                << m_blockChain->number() << "/" << _tx.sha3() << std::endl;
        return false;
    }
    return true;
}

bool TransactionNonceCheck::ok(Transaction const& _transaction, bool _needinsert)
{
    if (!isBlockLimitOk(_transaction))
        return false;
    return isNonceOk(_transaction, _needinsert);
}

void TransactionNonceCheck::updateCache(bool _rebuild)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        try
        {
            Timer timer;
            unsigned lastnumber = m_blockChain->number();
            unsigned prestartblk = m_startblk;
            unsigned preendblk = m_endblk;

            m_endblk = lastnumber;
            if (lastnumber > m_maxBlockLimit)
                m_startblk = lastnumber - m_maxBlockLimit;
            else
                m_startblk = 0;

            NONCECHECKER_LOG(TRACE)
                << "[#updateCache] [rebuild/startBlk/endBlk/prestartBlk/preEndBlk]:  " << _rebuild
                << "/" << m_startblk << "/" << m_endblk << "/" << prestartblk << "/" << preendblk
                << std::endl;
            if (_rebuild)
            {
                m_cache.clear();
                preendblk = 0;
            }
            else
            {
                for (unsigned i = prestartblk; i < m_startblk; i++)
                {
                    std::vector<u256> nonce_vec;
                    m_blockChain->getNonces(nonce_vec, i);
                    for (auto nonce : nonce_vec)
                    {
                        m_cache.erase(nonce);
                    }  // for
                    /*h256 blockhash = m_blockChain->numberHash(i);

                    Transactions trans = m_blockChain->getBlockByHash(blockhash)->transactions();
                    for (unsigned j = 0; j < trans.size(); j++)
                    {
                        auto key = this->generateKey(trans[j]);
                        auto iter = m_cache.find(key);
                        if (iter != m_cache.end())
                            m_cache.erase(iter);
                    }  // for*/
                }  // for
            }
            for (unsigned i = std::max(preendblk + 1, m_startblk); i <= m_endblk; i++)
            {
                std::vector<u256> nonce_vec;
                m_blockChain->getNonces(nonce_vec, i);
                for (auto nonce : nonce_vec)
                {
                    m_cache.insert(nonce);
                }
                /*h256 blockhash = m_blockChain->numberHash(i);

                Transactions trans = m_blockChain->getBlockByHash(blockhash)->transactions();
                for (unsigned j = 0; j < trans.size(); j++)
                {
                    auto key = this->generateKey(trans[j]);
                    auto iter = m_cache.find(key);
                    if (iter == m_cache.end())
                        m_cache.insert(key);
                }  // for*/
            }  // for
            NONCECHECKER_LOG(TRACE) << "[#updateCache] [cacheSize/costTime]:  " << m_cache.size()
                                    << "/" << (timer.elapsed() * 1000) << std::endl;
        }
        catch (...)
        {
            // should not happen as exceptions
            NONCECHECKER_LOG(WARNING)
                << "[#updateCache] update nonce cache failed: [EINFO]:  "
                << boost::current_exception_diagnostic_information() << std::endl;
        }
    }
}  // fun
}  // namespace txpool
}  // namespace dev
