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
 * @brief implementation for ledger nonce-checker
 * @file LedgerNonceChecker.cpp
 * @author: yujiechen
 * @date 2021-05-10
 */
#include "LedgerNonceChecker.h"
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

void LedgerNonceChecker::initNonceCache(
    std::map<int64_t, bcos::protocol::NonceListPtr> _initialNonces)
{
    for (auto const& it : _initialNonces)
    {
        m_blockNonceCache[it.first] = it.second;
        TxPoolNonceChecker::batchInsert(it.first, it.second);
    }
}

TransactionStatus LedgerNonceChecker::checkNonce(Transaction::ConstPtr _tx, bool _shouldUpdate)
{
    // check nonce
    auto status = TxPoolNonceChecker::checkNonce(_tx, _shouldUpdate);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    // check blockLimit
    return checkBlockLimit(_tx);
}

TransactionStatus LedgerNonceChecker::checkBlockLimit(bcos::protocol::Transaction::ConstPtr _tx)
{
    auto blockNumber = m_blockNumber.load();
    if (blockNumber >= _tx->blockLimit() || (blockNumber + m_blockLimit) < _tx->blockLimit())
    {
        NONCECHECKER_LOG(WARNING) << LOG_DESC("InvalidBlockLimit")
                                  << LOG_KV("blkLimit", _tx->blockLimit())
                                  << LOG_KV("blockLimit", m_blockLimit)
                                  << LOG_KV("curBlk", m_blockNumber)
                                  << LOG_KV("tx", _tx->hash().abridged());
        return TransactionStatus::BlockLimitCheckFail;
    }
    return TransactionStatus::None;
}


void LedgerNonceChecker::batchInsert(BlockNumber _batchId, NonceListPtr _nonceList)
{
    if (m_blockNumber < _batchId)
    {
        m_blockNumber.store(_batchId);
    }
    ssize_t batchToBeRemoved = (_batchId > m_blockLimit) ? (_batchId - m_blockLimit) : -1;
    // insert the latest nonces
    TxPoolNonceChecker::batchInsert(_batchId, _nonceList);

    WriteGuard l(x_blockNonceCache);
    if (!m_blockNonceCache.count(_batchId))
    {
        m_blockNonceCache[_batchId] = _nonceList;
        NONCECHECKER_LOG(DEBUG) << LOG_DESC("batchInsert nonceList") << LOG_KV("batchId", _batchId)
                                << LOG_KV("nonceSize", _nonceList->size());
    }
    // the genesis has no nonceList
    if (batchToBeRemoved == -1)
    {
        return;
    }
    // remove the expired nonces
    if (!m_blockNonceCache.count(batchToBeRemoved))
    {
        NONCECHECKER_LOG(WARNING) << LOG_DESC("batchInsert: miss cache when remove expired cache")
                                  << LOG_KV("batchToBeRemoved", batchToBeRemoved);
        return;
    }
    auto nonceList = m_blockNonceCache[batchToBeRemoved];
    m_blockNonceCache.erase(batchToBeRemoved);
    batchRemove(*nonceList);
    NONCECHECKER_LOG(DEBUG) << LOG_DESC("batchInsert: remove expired nonce")
                            << LOG_KV("batchToBeRemoved", batchToBeRemoved)
                            << LOG_KV("nonceSize", nonceList->size());
}