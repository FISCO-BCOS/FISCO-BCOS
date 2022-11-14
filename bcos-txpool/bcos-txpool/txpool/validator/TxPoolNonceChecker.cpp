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
    decltype(m_nonces)::const_accessor it;
    return m_nonces.find(it, _nonce);
}

TransactionStatus TxPoolNonceChecker::checkNonce(Transaction::ConstPtr _tx, bool _shouldUpdate)
{
    auto nonce = _tx->nonce();

    decltype(m_nonces)::const_accessor it;
    if (m_nonces.find(it, nonce))
    {
        return TransactionStatus::NonceCheckFail;
    }

    if (_shouldUpdate)
    {
        m_nonces.emplace(it, std::move(nonce), std::monostate{});
    }
    return TransactionStatus::None;
}

void TxPoolNonceChecker::insert(NonceType const& _nonce)
{
    m_nonces.emplace(_nonce, std::monostate{});
}

void TxPoolNonceChecker::batchInsert(BlockNumber /*_batchId*/, NonceListPtr const& _nonceList)
{
    for (auto const& nonce : *_nonceList)
    {
        m_nonces.emplace(nonce, std::monostate{});
    }
}

void TxPoolNonceChecker::remove(NonceType const& _nonce)
{
    m_nonces.erase(_nonce);
}

void TxPoolNonceChecker::batchRemove(NonceList const& _nonceList)
{
    for (auto const& nonce : _nonceList)
    {
        remove(nonce);
    }
}

void TxPoolNonceChecker::batchRemove(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
    std::hash<bcos::crypto::HashType>> const& _nonceList)
{
    for (auto const& nonce : _nonceList)
    {
        remove(nonce);
    }
}