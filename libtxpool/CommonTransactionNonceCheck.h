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

#include <libprotocol/Block.h>
#include <libprotocol/Protocol.h>
#include <libprotocol/Transaction.h>
#include <libutilities/Common.h>
#include <unordered_set>
namespace bcos
{
namespace txpool
{
class CommonTransactionNonceCheck
{
public:
    CommonTransactionNonceCheck() {}
    virtual ~CommonTransactionNonceCheck() = default;
    virtual void delCache(bcos::protocol::NonceKeyType const& key);
    virtual void delCache(bcos::protocol::Transactions const& _transactions);
    virtual void insertCache(bcos::protocol::Transaction const& _transaction);
    virtual bool isNonceOk(bcos::protocol::Transaction const& _trans, bool needInsert = false);

protected:
    mutable SharedMutex m_lock;
    std::unordered_set<bcos::protocol::NonceKeyType> m_cache;
};
}  // namespace txpool
}  // namespace bcos
