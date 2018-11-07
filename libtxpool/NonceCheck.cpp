/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: NonceCheck.cpp
 * @author: toxotguo
 *
 * @date: 2017
 */

#include "NonceCheck.h"
#include <libdevcore/Common.h>

using namespace dev;
using namespace dev::blockchain;
NonceCheck::~NonceCheck() {}

u256 NonceCheck::maxblocksize = u256(1000);

void NonceCheck::init()
{
    m_startblk = 0;
    m_endblk = 0;
    updateCache(true);
}
std::string NonceCheck::generateKey(Transaction const& _t)
{
    Address account = _t.from();
    std::string key = toHex(account.ref());
    key += "_" + toString(_t.nonce());
    return key;
}

bool NonceCheck::ok(Transaction const& _transaction, bool _needinsert)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        string key = this->generateKey(_transaction);
        auto iter = m_cache.find(key);
        if (iter != m_cache.end())
            return false;
        if (_needinsert)
        {
            m_cache.insert(std::pair<std::string, bool>(key, true));
        }
    }
    return true;
}

void NonceCheck::delCache(Transaction const& _transcation)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        string key = this->generateKey(_transcation);
        auto iter = m_cache.find(key);
        if (iter != m_cache.end())
            m_cache.erase(iter);
    }
}

void NonceCheck::updateCache(bool _rebuild)
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
            if (lastnumber > (unsigned)NonceCheck::maxblocksize)
                m_startblk = lastnumber - (unsigned)NonceCheck::maxblocksize;
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
                    h256 blockhash = m_blockChain->numberHash(i);

                    Transactions trans = m_blockChain->getBlockByHash(blockhash)->transactions();
                    for (unsigned j = 0; j < trans.size(); j++)
                    {
                        string key = this->generateKey(trans[j]);
                        auto iter = m_cache.find(key);
                        if (iter != m_cache.end())
                            m_cache.erase(iter);
                    }  // for
                }      // for
            }
            for (unsigned i = std::max(preendblk + 1, m_startblk); i <= m_endblk; i++)
            {
                h256 blockhash = m_blockChain->numberHash(i);

                Transactions trans = m_blockChain->getBlockByHash(blockhash)->transactions();
                for (unsigned j = 0; j < trans.size(); j++)
                {
                    string key = this->generateKey(trans[j]);
                    auto iter = m_cache.find(key);
                    if (iter == m_cache.end())
                        m_cache.insert(std::pair<std::string, bool>(key, true));
                }  // for
            }      // for
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
