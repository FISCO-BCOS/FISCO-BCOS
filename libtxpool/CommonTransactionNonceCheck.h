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
 * @file: CommonTransactonNonceCheck.h
 * @author: yujiechen
 *
 * @date: 2018-11-08
 */
#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Guards.h>
#include <libethcore/Block.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <unordered_set>
namespace dev
{
namespace txpool
{
//通用的交易随机值检查对象
class CommonTransactionNonceCheck
{
public:
    //构造函数
    CommonTransactionNonceCheck() {}
    //析构函数,c++ 11的写法
    virtual ~CommonTransactionNonceCheck() = default;
    //声明成员函数
    virtual void delCache(dev::eth::NonceKeyType const& key);
    virtual void delCache(dev::eth::Transactions const& _transactions);
    virtual void insertCache(dev::eth::Transaction const& _transaction);
    virtual bool isNonceOk(dev::eth::Transaction const& _trans, bool needInsert = false);

protected:
    //共享锁
    mutable SharedMutex m_lock;
    //非自动排序的集合
    std::unordered_set<dev::eth::NonceKeyType> m_cache;
};
}  // namespace txpool
}  // namespace dev