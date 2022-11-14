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

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "TransactionReceiptImpl.h"
#include <bcos-framework/protocol/TransactionReceiptFactory.h>

#include <utility>


namespace bcostars
{
namespace protocol
{
class TransactionReceiptFactoryImpl : public bcos::protocol::TransactionReceiptFactory
{
public:
    TransactionReceiptFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
      : m_cryptoSuite(std::move(cryptoSuite))
    {}
    ~TransactionReceiptFactoryImpl() override = default;

    TransactionReceiptImpl::Ptr createReceipt(bcos::bytesConstRef _receiptData) override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(m_cryptoSuite,
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
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(m_cryptoSuite,
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
        auto const& inner = transactionReceipt->innerGetter();
        inner()->data.version = 0;
        inner()->data.gasUsed = boost::lexical_cast<std::string>(gasUsed);
        inner()->data.contractAddress = std::move(contractAddress);
        inner()->data.status = status;
        inner()->data.output.assign(output.begin(), output.end());
        transactionReceipt->setLogEntries(logEntries);

        inner()->data.blockNumber = blockNumber;
        return transactionReceipt;
    }

    void setCryptoSuite(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
    {
        m_cryptoSuite = std::move(cryptoSuite);
    }
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcostars