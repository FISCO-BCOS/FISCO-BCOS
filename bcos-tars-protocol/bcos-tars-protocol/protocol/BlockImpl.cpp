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

#include "BlockImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include "bcos-concepts/Serialize.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-utilities/AnyHolder.h"
#include <boost/throw_exception.hpp>

using namespace bcostars;
using namespace bcostars::protocol;

void BlockImpl::decode(bcos::bytesConstRef _data, bool, bool)
{
    bcos::concepts::serialize::decode(_data, m_inner);
}

void BlockImpl::encode(bcos::bytes& _encodeData) const
{
    bcos::concepts::serialize::encode(m_inner, _encodeData);
}

bcos::protocol::BlockHeader::Ptr BlockImpl::blockHeader()
{
    return std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [self = shared_from_this()]() mutable {
            return std::addressof(self->m_inner.blockHeader);
        });
}

bcos::protocol::AnyBlockHeader BlockImpl::blockHeader() const
{
    return {bcos::InPlace<bcostars::protocol::BlockHeaderImpl>{},
        [self = shared_from_this()]() { return std::addressof(self->m_inner.blockHeader); }};
}

void BlockImpl::setBlockHeader(bcos::protocol::BlockHeader::Ptr _blockHeader)
{
    if (_blockHeader)
    {
        m_inner.blockHeader =
            std::dynamic_pointer_cast<bcostars::protocol::BlockHeaderImpl>(_blockHeader)->inner();
    }
}

void BlockImpl::setReceipt(uint64_t _index, bcos::protocol::TransactionReceipt::Ptr _receipt)
{
    if (_index >= m_inner.receipts.size())
    {
        m_inner.receipts.resize(m_inner.transactions.size());
    }
    auto innerReceipt =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(_receipt)->inner();
    m_inner.receipts[_index] = innerReceipt;
}

void BlockImpl::appendReceipt(bcos::protocol::TransactionReceipt::Ptr _receipt)
{
    m_inner.receipts.emplace_back(
        std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(_receipt)->inner());
}

void BlockImpl::setNonceList(::ranges::any_view<std::string> nonces)
{
    m_inner.nonceList = ::ranges::to<std::vector>(nonces);
}

::ranges::any_view<std::string> BlockImpl::nonceList() const
{
    return m_inner.nonceList;
}

bcos::crypto::HashType bcostars::protocol::BlockImpl::transactionHash(uint64_t _index) const
{
    const auto& hashBytes = m_inner.transactionsMetaData[_index].hash;
    return bcos::crypto::HashType{
        bcos::bytesConstRef((const bcos::byte*)hashBytes.data(), hashBytes.size())};
}

void BlockImpl::appendTransactionMetaData(bcos::protocol::TransactionMetaData::Ptr _txMetaData)
{
    auto txMetaDataImpl =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionMetaDataImpl>(_txMetaData);
    m_inner.transactionsMetaData.emplace_back(txMetaDataImpl->inner());
}

uint64_t BlockImpl::transactionsMetaDataSize() const
{
    return m_inner.transactionsMetaData.size();
}
bcos::protocol::BlockType bcostars::protocol::BlockImpl::blockType() const
{
    return (bcos::protocol::BlockType)m_inner.type;
}
void bcostars::protocol::BlockImpl::setBlockType(bcos::protocol::BlockType _blockType)
{
    m_inner.type = (int32_t)_blockType;
}
void bcostars::protocol::BlockImpl::setTransaction(
    uint64_t _index, bcos::protocol::Transaction::Ptr _transaction)
{
    m_inner.transactions[_index] =
        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(_transaction)->inner();
}
void bcostars::protocol::BlockImpl::appendTransaction(bcos::protocol::Transaction::Ptr _transaction)
{
    m_inner.transactions.emplace_back(
        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(_transaction)->inner());
}
uint64_t bcostars::protocol::BlockImpl::transactionsSize() const
{
    return m_inner.transactions.size();
}

uint64_t bcostars::protocol::BlockImpl::receiptsSize() const
{
    return m_inner.receipts.size();
}
const bcostars::Block& bcostars::protocol::BlockImpl::inner() const
{
    return m_inner;
}
bcostars::Block& bcostars::protocol::BlockImpl::inner()
{
    return m_inner;
}
void bcostars::protocol::BlockImpl::setInner(bcostars::Block inner)
{
    m_inner = std::move(inner);
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
            m_inner.transactions |
            ::ranges::views::transform([&](const bcostars::Transaction& transaction) {
                bcos::bytes hash;
                bcos::concepts::hash::calculate(transaction, hashImpl.hasher(), hash);
                return hash;
            });
        merkle.generateMerkle(hashesRange, m_inner.transactionsMerkle);
    }
    else if (transactionsMetaDataSize() > 0)
    {
        auto hashesRange = m_inner.transactionsMetaData |
                           ::ranges::views::transform(
                               [](const bcostars::TransactionMetaData& transactionMetaData) {
                                   return transactionMetaData.hash;
                               });
        merkle.generateMerkle(hashesRange, m_inner.transactionsMerkle);
    }
    bcos::concepts::bytebuffer::assignTo(*::ranges::rbegin(m_inner.transactionsMerkle), txsRoot);

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
    auto hashesRange = m_inner.receipts |
                       ::ranges::views::transform([&](const bcostars::TransactionReceipt& receipt) {
                           bcos::bytes hash;
                           bcos::concepts::hash::calculate(receipt, hashImpl.hasher(), hash);
                           return hash;
                       });
    bcos::crypto::merkle::Merkle merkle(hashImpl.hasher());
    merkle.generateMerkle(hashesRange, m_inner.receiptsMerkle);
    bcos::concepts::bytebuffer::assignTo(*::ranges::rbegin(m_inner.receiptsMerkle), receiptsRoot);

    return receiptsRoot;
}

bcos::protocol::ViewResult<bcos::crypto::HashType>
bcostars::protocol::BlockImpl::transactionHashes() const
{
    return ::ranges::views::transform(m_inner.transactionsMetaData, [](auto& txMetaData) {
        return bcos::crypto::HashType{
            bcos::bytesConstRef((const bcos::byte*)txMetaData.hash.data(), txMetaData.hash.size())};
    });
}
bcos::protocol::ViewResult<bcos::protocol::AnyTransactionMetaData>
bcostars::protocol::BlockImpl::transactionMetaDatas() const
{
    return ::ranges::views::transform(m_inner.transactionsMetaData, [](auto& inner) {
        return bcos::protocol::AnyTransactionMetaData{
            bcos::InPlace<bcostars::protocol::TransactionMetaDataImpl>{},
            [&]() mutable { return &inner; }};
    });
}
bcos::protocol::ViewResult<bcos::protocol::AnyTransaction>
bcostars::protocol::BlockImpl::transactions() const
{
    return ::ranges::views::transform(m_inner.transactions, [](auto& inner) {
        return bcos::protocol::AnyTransaction{
            bcos::InPlace<bcostars::protocol::TransactionImpl>{}, [&]() mutable { return &inner; }};
    });
}
bcos::protocol::ViewResult<bcos::protocol::AnyTransactionReceipt>
bcostars::protocol::BlockImpl::receipts() const
{
    return ::ranges::views::transform(m_inner.receipts, [](auto& receipt) {
        return bcos::protocol::AnyTransactionReceipt{
            bcos::InPlace<bcostars::protocol::TransactionReceiptImpl>{},
            [&]() mutable { return std::addressof(receipt); }};
    });
}
size_t bcostars::protocol::BlockImpl::size() const
{
    size_t size = 0;
    size += blockHeader()->size();
    for (auto transaction : transactions())
    {
        size += transaction->size();
    }
    for (auto receipt : receipts())
    {
        size += receipt->size();
    }
    return size;
}

bcos::bytesConstRef bcostars::protocol::BlockImpl::logsBloom() const
{
    const auto& data = inner();
    return {(const unsigned char*)data.logsBloom.data(), data.logsBloom.size()};
}

void bcostars::protocol::BlockImpl::setLogsBloom(bcos::bytesConstRef logsBloom)
{
    auto& data = inner();
    data.logsBloom.assign(logsBloom.data(), logsBloom.data() + logsBloom.size());
}