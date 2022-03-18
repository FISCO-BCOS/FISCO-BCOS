/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief implementation for txpool nonce-checker
 * @file TxPoolNonceChecker.cpp
 * @author: yujiechen
 * @date 2021-05-10
 */
#include "TxPoolNonceChecker.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

bool TxPoolNonceChecker::exists(NonceType const& _nonce)
{
    ReadGuard l(x_nonceCache);
    if (m_nonceCache.count(_nonce))
    {
        return true;
    }
    return false;
}

TransactionStatus TxPoolNonceChecker::checkNonce(Transaction::ConstPtr _tx, bool _shouldUpdate)
{
    ReadGuard l(x_nonceCache);
    auto nonce = _tx->nonce();
    if (m_nonceCache.count(nonce))
    {
        return TransactionStatus::NonceCheckFail;
    }
    if (_shouldUpdate)
    {
        m_nonceCache.insert(nonce);
    }
    return TransactionStatus::None;
}

void TxPoolNonceChecker::insert(NonceType const& _nonce)
{
    ReadGuard l(x_nonceCache);
    m_nonceCache.insert(_nonce);
}

void TxPoolNonceChecker::batchInsert(BlockNumber, NonceListPtr _nonceList)
{
    ReadGuard l(x_nonceCache);
    for (auto const& nonce : *_nonceList)
    {
        m_nonceCache.insert(nonce);
    }
}

void TxPoolNonceChecker::remove(NonceType const& _nonce)
{
    if (m_nonceCache.count(_nonce))
    {
        m_nonceCache.unsafe_erase(_nonce);
    }
}

void TxPoolNonceChecker::batchRemove(NonceList const& _nonceList)
{
    WriteGuard l(x_nonceCache);
    for (auto const& nonce : _nonceList)
    {
        remove(nonce);
    }
}

void TxPoolNonceChecker::batchRemove(
    tbb::concurrent_set<bcos::protocol::NonceType> const& _nonceList)
{
    WriteGuard l(x_nonceCache);
    for (auto const& nonce : _nonceList)
    {
        remove(nonce);
    }
}