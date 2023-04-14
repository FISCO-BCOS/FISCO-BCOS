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
#include <variant>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

bool TxPoolNonceChecker::exists(NonceType const& _nonce)
{
    return m_nonces.contains(_nonce);
}

TransactionStatus TxPoolNonceChecker::checkNonce(Transaction::ConstPtr _tx, bool _shouldUpdate)
{
    auto nonce = _tx->nonce();

    if (m_nonces.contains(nonce))
    {
        return TransactionStatus::NonceCheckFail;
    }

    if (_shouldUpdate)
    {
        NonceSet::WriteAccessor::Ptr accessor;
        m_nonces.insert(accessor, std::move(nonce));
    }
    return TransactionStatus::None;
}

void TxPoolNonceChecker::insert(NonceType const& _nonce)
{
    NonceSet::WriteAccessor::Ptr accessor;
    m_nonces.insert(accessor, _nonce);
}

void TxPoolNonceChecker::batchInsert(BlockNumber /*_batchId*/, NonceListPtr const& _nonceList)
{
    m_nonces.batchInsert(*_nonceList);
}

void TxPoolNonceChecker::remove(NonceType const& _nonce)
{
    m_nonces.remove(_nonce);
}

void TxPoolNonceChecker::batchRemove(NonceList const& _nonceList)
{
    m_nonces.batchRemove(_nonceList);
}

void TxPoolNonceChecker::batchRemove(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
    std::hash<bcos::protocol::NonceType>> const& _nonceList)
{
    for (auto const& nonce : _nonceList)
    {
        remove(nonce);
    }
}