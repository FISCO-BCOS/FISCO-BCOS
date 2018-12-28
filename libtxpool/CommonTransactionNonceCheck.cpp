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
 * @file: CommonTransactionNonceCheck.cpp
 * @author: yujiechen
 *
 * @date: 2018-11-08
 */
#include "CommonTransactionNonceCheck.h"
using namespace dev::eth;
namespace dev
{
namespace txpool
{
bool CommonTransactionNonceCheck::isNonceOk(dev::eth::Transaction const& _trans, bool needInsert)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        auto key = this->generateKey(_trans);
        auto iter = m_cache.find(key);
        if (iter != m_cache.end())
            return false;
        if (needInsert)
        {
            m_cache.insert(key);
        }
        return true;
    }
    /// obtain lock failed
    return false;
}

///void CommonTransactionNonceCheck::delCache(std::string const& key)
void CommonTransactionNonceCheck::delCache(dev::u256 const& key)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        auto iter = m_cache.find(key);
        if (iter != m_cache.end())
            m_cache.erase(iter);
    }
}

void CommonTransactionNonceCheck::delCache(Transactions const& _transcations)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        for (unsigned i = 0; i < _transcations.size(); i++)
        {
            auto key = this->generateKey(_transcations[i]);
            auto iter = m_cache.find(key);
            if (iter != m_cache.end())
                m_cache.erase(iter);
        }  // for
    }
}

void CommonTransactionNonceCheck::insertCache(dev::eth::Transaction const& _transcation)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        auto key = this->generateKey(_transcation);
        m_cache.insert(key);
    }
}

}  // namespace txpool
}  // namespace dev