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
//输入参数为一笔交易和一个是否插入的布尔值
//首先检查m_cache中是否包含当前的nounce值，如果包含了直接返回false
bool CommonTransactionNonceCheck::isNonceOk(dev::eth::Transaction const& _trans, bool needInsert)
{
    UpgradableGuard l(m_lock);
    {
        const auto& key = _trans.nonce();
        if (m_cache.count(key))
        {
            // duplicated transaction sync may cause duplicated nonce
            LOG(TRACE) << LOG_DESC("CommonTransactionNonceCheck: isNonceOk: duplicated nonce")
                       << LOG_KV("nonce", key) << LOG_KV("transHash", _trans.hash().abridged());

            return false;
        }
        if (needInsert)
        {
            UpgradeGuard ul(l);
            m_cache.insert(key);
        }
        return true;
    }
    /// obtain lock failed
    return false;
}
//根据具体的nounce值删除缓存中的key
void CommonTransactionNonceCheck::delCache(dev::eth::NonceKeyType const& key)
{
    UpgradableGuard l(m_lock);
    {
        auto iter = m_cache.find(key);
        if (iter != m_cache.end())
        {
            UpgradeGuard ul(l);
            m_cache.erase(iter);
        }
    }
}
//删除缓存，输入的参数为transactions,为transaction的集合，使用的是vector
//删除m_cache中指定的nounce值
//using Transactions = std::vector<Transaction::Ptr>;
void CommonTransactionNonceCheck::delCache(Transactions const& _transactions)
{
    UpgradableGuard l(m_lock);
    {
        //delList是u256类型的vector
        std::vector<dev::eth::NonceKeyType> delList;
        for (unsigned i = 0; i < _transactions.size(); i++)
        {
            const auto& key = _transactions[i]->nonce();
            if (m_cache.count(key))
            {
                delList.push_back(key);
            }
        }

        if (delList.size() > 0)
        {
            UpgradeGuard ul(l);
            for (auto key : delList)
            {
                m_cache.erase(key);
            }
        }
    }
}
//插入交易的nounce值
//nouce值的作用是确保发送的时候两笔相同的交易的哈希值不同
void CommonTransactionNonceCheck::insertCache(dev::eth::Transaction const& _transaction)
{
    WriteGuard l(m_lock);
    {
        m_cache.insert(_transaction.nonce());
    }
}

}  // namespace txpool
}  // namespace dev
