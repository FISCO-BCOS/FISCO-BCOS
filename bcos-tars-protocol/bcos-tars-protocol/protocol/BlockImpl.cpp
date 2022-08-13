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
        m_transactionFactory->cryptoSuite(),
        [inner = this->m_inner]() mutable { return &inner->blockHeader; });
}

bcos::protocol::BlockHeader::ConstPtr BlockImpl::blockHeaderConst() const
{
    bcos::ReadGuard l(x_blockHeader);
    return std::make_shared<const bcostars::protocol::BlockHeaderImpl>(
        m_transactionFactory->cryptoSuite(),
        [inner = this->m_inner]() { return &inner->blockHeader; });
}

bcos::protocol::Transaction::ConstPtr BlockImpl::transaction(uint64_t _index) const
{
    return std::make_shared<const bcostars::protocol::TransactionImpl>(
        m_transactionFactory->cryptoSuite(),
        [inner = m_inner, _index]() { return &(inner->transactions[_index]); });
}

bcos::protocol::TransactionReceipt::ConstPtr BlockImpl::receipt(uint64_t _index) const
{
    return std::make_shared<const bcostars::protocol::TransactionReceiptImpl>(
        m_transactionFactory->cryptoSuite(),
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

void BlockImpl::setNonceList(bcos::protocol::NonceList const& _nonceList)
{
    m_inner->nonceList.clear();
    m_inner->nonceList.reserve(_nonceList.size());
    for (auto const& it : _nonceList)
    {
        m_inner->nonceList.emplace_back(boost::lexical_cast<std::string>(it));
    }

    m_nonceList.clear();
}

void BlockImpl::setNonceList(bcos::protocol::NonceList&& _nonceList)
{
    m_inner->nonceList.clear();
    m_inner->nonceList.reserve(_nonceList.size());
    for (auto const& it : _nonceList)
    {
        m_inner->nonceList.emplace_back(boost::lexical_cast<std::string>(it));
    }

    m_nonceList.clear();
}
bcos::protocol::NonceList const& BlockImpl::nonceList() const
{
    if (m_nonceList.empty())
    {
        m_nonceList.reserve(m_inner->nonceList.size());

        for (auto const& it : m_inner->nonceList)
        {
            if (it.empty())
            {
                m_nonceList.push_back(bcos::protocol::NonceType(0));
            }
            else
            {
                m_nonceList.push_back(boost::lexical_cast<bcos::u256>(it));
            }
        }
    }

    return m_nonceList;
}

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
