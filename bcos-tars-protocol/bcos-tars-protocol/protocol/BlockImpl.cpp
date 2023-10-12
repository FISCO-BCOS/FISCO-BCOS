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
 * @brief implementation for Block
 * @file BlockImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "../impl/TarsSerializable.h"

#include "BlockImpl.h"
#include <bcos-concepts/Serialize.h>
#include <range/v3/view/transform.hpp>

using namespace bcostars;
using namespace bcostars::protocol;

void BlockImpl::decode(bcos::bytesConstRef _data, bool, bool)
{
    bcos::concepts::serialize::decode(_data, *m_inner);
}

void BlockImpl::encode(bcos::bytes& _encodeData) const
{
    bcos::concepts::serialize::encode(*m_inner, _encodeData);
}

bcos::protocol::BlockHeader::Ptr BlockImpl::blockHeader()
{
    bcos::ReadGuard l(x_blockHeader);
    return std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [inner = this->m_inner]() mutable { return &inner->blockHeader; });
}

bcos::protocol::BlockHeader::ConstPtr BlockImpl::blockHeaderConst() const
{
    // bcos::ReadGuard l(x_blockHeader);
    return std::make_shared<const bcostars::protocol::BlockHeaderImpl>(
        [inner = this->m_inner]() { return &inner->blockHeader; });
}

bcos::protocol::Transaction::ConstPtr BlockImpl::transaction(uint64_t _index) const
{
    return std::make_shared<const bcostars::protocol::TransactionImpl>(
        [inner = m_inner, _index]() { return &(inner->transactions[_index]); });
}

bcos::protocol::Transaction::Ptr BlockImpl::getTransaction(uint64_t _index) 
{
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = m_inner, _index]() { return &(inner->transactions[_index]); });
}

// TODO: return struct instead of pointer
bcos::protocol::TransactionReceipt::ConstPtr BlockImpl::receipt(uint64_t _index) const
{
    return std::make_shared<const bcostars::protocol::TransactionReceiptImpl>(
        [inner = m_inner, _index]() { return &(inner->receipts[_index]); });
}

void BlockImpl::setBlockHeader(bcos::protocol::BlockHeader::Ptr _blockHeader)
{
    if (_blockHeader)
    {
        bcos::WriteGuard l(x_blockHeader);
        m_inner->blockHeader =
            std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(_blockHeader)->inner();
    }
}

void BlockImpl::setReceipt(uint64_t _index, bcos::protocol::TransactionReceipt::Ptr _receipt)
{
    if (_index >= m_inner->receipts.size())
    {
        m_inner->receipts.resize(m_inner->transactions.size());
    }
    auto innerReceipt =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(_receipt)->inner();
    m_inner->receipts[_index] = innerReceipt;
}

void BlockImpl::appendReceipt(bcos::protocol::TransactionReceipt::Ptr _receipt)
{
    m_inner->receipts.emplace_back(
        std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(_receipt)->inner());
}

void BlockImpl::setNonceList(RANGES::any_view<std::string> nonces)
{
    m_inner->nonceList.clear();
    for (auto it : nonces)
    {
        m_inner->nonceList.emplace_back(boost::lexical_cast<std::string>(it));
    }
}

RANGES::any_view<std::string> BlockImpl::nonceList() const
{
    return m_inner->nonceList;
}

// TODO: return struct instead of pointer
bcos::protocol::TransactionMetaData::ConstPtr BlockImpl::transactionMetaData(uint64_t _index) const
{
    if (_index >= transactionsMetaDataSize())
    {
        return nullptr;
    }

    auto txMetaData = std::make_shared<bcostars::protocol::TransactionMetaDataImpl>(
        [inner = m_inner, _index]() { return &inner->transactionsMetaData[_index]; });

    return txMetaData;
}

TransactionMetaDataImpl BlockImpl::transactionMetaDataImpl(uint64_t _index) const
{
    if (_index >= transactionsMetaDataSize())
    {
        return bcostars::protocol::TransactionMetaDataImpl([] { return nullptr; });
    }

    return bcostars::protocol::TransactionMetaDataImpl(
        [inner = m_inner, _index]() { return &inner->transactionsMetaData[_index]; });
}

void BlockImpl::appendTransactionMetaData(bcos::protocol::TransactionMetaData::Ptr _txMetaData)
{
    auto txMetaDataImpl =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionMetaDataImpl>(_txMetaData);
    m_inner->transactionsMetaData.emplace_back(txMetaDataImpl->inner());
}

uint64_t BlockImpl::transactionsMetaDataSize() const
{
    return m_inner->transactionsMetaData.size();
}
bcos::protocol::BlockType bcostars::protocol::BlockImpl::blockType() const
{
    return (bcos::protocol::BlockType)m_inner->type;
}
void bcostars::protocol::BlockImpl::setBlockType(bcos::protocol::BlockType _blockType)
{
    m_inner->type = (int32_t)_blockType;
}
void bcostars::protocol::BlockImpl::setTransaction(
    uint64_t _index, bcos::protocol::Transaction::Ptr _transaction)
{
    m_inner->transactions[_index] =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(_transaction)->inner();
}
void bcostars::protocol::BlockImpl::appendTransaction(bcos::protocol::Transaction::Ptr _transaction)
{
    m_inner->transactions.emplace_back(
        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(_transaction)->inner());
}
uint64_t bcostars::protocol::BlockImpl::transactionsSize() const
{
    return m_inner->transactions.size();
}
uint64_t bcostars::protocol::BlockImpl::receiptsSize() const
{
    return m_inner->receipts.size();
}
const bcostars::Block& bcostars::protocol::BlockImpl::inner() const
{
    return *m_inner;
}
void bcostars::protocol::BlockImpl::setInner(bcostars::Block inner)
{
    *m_inner = std::move(inner);
}
bcos::crypto::HashType bcostars::protocol::BlockImpl::calculateTransactionRoot(
    const bcos::crypto::Hash& hashImpl) const
{
    auto txsRoot = bcos::crypto::HashType();
    // with no transactions
    if (transactionsSize() == 0 && transactionsMetaDataSize() == 0)
    {
        return txsRoot;
    }

    bcos::crypto::merkle::Merkle merkle(hashImpl.hasher());
    if (transactionsSize() > 0)
    {
        auto hashesRange =
            m_inner->transactions |
            RANGES::views::transform([&](const bcostars::Transaction& transaction) {
                bcos::bytes hash;
                bcos::concepts::hash::calculate(hashImpl.hasher(), transaction, hash);
                return hash;
            });
        merkle.generateMerkle(hashesRange, m_inner->transactionsMerkle);
    }
    else if (transactionsMetaDataSize() > 0)
    {
        auto hashesRange =
            m_inner->transactionsMetaData |
            RANGES::views::transform([](const bcostars::TransactionMetaData& transactionMetaData) {
                return transactionMetaData.hash;
            });
        merkle.generateMerkle(hashesRange, m_inner->transactionsMerkle);
    }
    bcos::concepts::bytebuffer::assignTo(*RANGES::rbegin(m_inner->transactionsMerkle), txsRoot);

    return txsRoot;
}
bcos::crypto::HashType bcostars::protocol::BlockImpl::calculateReceiptRoot(
    const bcos::crypto::Hash& hashImpl) const
{
    auto receiptsRoot = bcos::crypto::HashType();
    // with no receipts
    if (receiptsSize() == 0)
    {
        return receiptsRoot;
    }
    auto hashesRange = m_inner->receipts |
                       RANGES::views::transform([&](const bcostars::TransactionReceipt& receipt) {
                           bcos::bytes hash;
                           bcos::concepts::hash::calculate(hashImpl.hasher(), receipt, hash);
                           return hash;
                       });
    bcos::crypto::merkle::Merkle merkle(hashImpl.hasher());
    merkle.generateMerkle(hashesRange, m_inner->receiptsMerkle);
    bcos::concepts::bytebuffer::assignTo(*RANGES::rbegin(m_inner->receiptsMerkle), receiptsRoot);

    return receiptsRoot;
}
