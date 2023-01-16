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
 * @brief tars implementation for TransactionReceiptFactory
 * @file TransactionReceiptFactoryImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once
#include "../impl/TarsHashable.h"

#include "TransactionReceiptImpl.h"
#include <bcos-concepts/Hash.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <utility>


namespace bcostars::protocol
{
class TransactionReceiptFactoryImpl : public bcos::protocol::TransactionReceiptFactory
{
public:
    TransactionReceiptFactoryImpl(const bcos::crypto::CryptoSuite::Ptr& cryptoSuite)
      : m_hashImpl(cryptoSuite->hashImpl())
    {}
    ~TransactionReceiptFactoryImpl() override = default;

    TransactionReceiptImpl::Ptr createReceipt(bcos::bytesConstRef _receiptData) override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });

        transactionReceipt->decode(_receiptData);

        return transactionReceipt;
    }

    TransactionReceiptImpl::Ptr createReceipt(bcos::bytes const& _receiptData) override
    {
        return createReceipt(bcos::ref(_receiptData));
    }

    TransactionReceiptImpl::Ptr createReceipt(bcos::u256 const& gasUsed,
        std::string contractAddress, const std::vector<bcos::protocol::LogEntry>& logEntries,
        int32_t status, bcos::bytesConstRef output,
        bcos::protocol::BlockNumber blockNumber) override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
        auto& inner = transactionReceipt->mutableInner();
        inner.data.version = 0;
        inner.data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
        inner.data.contractAddress = std::move(contractAddress);
        inner.data.status = status;
        inner.data.output.assign(output.begin(), output.end());
        transactionReceipt->setLogEntries(logEntries);
        inner.data.blockNumber = blockNumber;

        // Update the hash field
        std::visit(
            [&inner](auto&& hasher) {
                using HasherType = std::decay_t<decltype(hasher)>;
                bcos::concepts::hash::calculate<HasherType>(inner, inner.dataHash);
            },
            m_hashImpl->hasher());
        return transactionReceipt;
    }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcostars::protocol