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
 * @file TxPoolNonceChecker.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include <tbb/concurrent_unordered_set.h>
namespace bcos
{
namespace txpool
{
class TxPoolNonceChecker : public NonceCheckerInterface
{
public:
    TxPoolNonceChecker() = default;
    bcos::protocol::TransactionStatus checkNonce(
        bcos::protocol::Transaction::ConstPtr _tx, bool _shouldUpdate = false) override;
    void batchInsert(
        bcos::protocol::BlockNumber _batchId, bcos::protocol::NonceListPtr _nonceList) override;
    void batchRemove(bcos::protocol::NonceList const& _nonceList) override;
    void batchRemove(tbb::concurrent_set<bcos::protocol::NonceType> const& _nonceList) override;
    bool exists(bcos::protocol::NonceType const& _nonce) override;

protected:
    void insert(bcos::protocol::NonceType const& _nonce) override;
    void remove(bcos::protocol::NonceType const& _nonce) override;

    tbb::concurrent_unordered_set<bcos::protocol::NonceType> m_nonceCache;
    mutable SharedMutex x_nonceCache;
};
}  // namespace txpool
}  // namespace bcos