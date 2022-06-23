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
#include <bcos-framework//protocol/TransactionReceiptFactory.h>


namespace bcostars
{
namespace protocol
{
class TransactionReceiptFactoryImpl : public bcos::protocol::TransactionReceiptFactory
{
public:
    TransactionReceiptFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
      : m_cryptoSuite(cryptoSuite)
    {}
    ~TransactionReceiptFactoryImpl() override {}

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

    TransactionReceiptImpl::Ptr createReceipt(bcos::u256 const& _gasUsed,
        const std::string_view& _contractAddress,
        std::shared_ptr<std::vector<bcos::protocol::LogEntry>> _logEntries, int32_t _status,
        bcos::bytes const& _output, bcos::protocol::BlockNumber _blockNumber) override
    {
        auto transactionReceipt = std::make_shared<TransactionReceiptImpl>(m_cryptoSuite,
            [m_receipt = bcostars::TransactionReceipt()]() mutable { return &m_receipt; });
        auto const& inner = transactionReceipt->innerGetter();
        // required: version
        inner()->data.version = 0;
        // required: gasUsed
        inner()->data.gasUsed = boost::lexical_cast<std::string>(_gasUsed);
        // optional: contractAddress
        inner()->data.contractAddress.assign(_contractAddress.begin(), _contractAddress.end());
        // required: status
        inner()->data.status = _status;
        // optional: output
        inner()->data.output.assign(_output.begin(), _output.end());
        transactionReceipt->setLogEntries(*_logEntries);

        inner()->data.blockNumber = _blockNumber;
        return transactionReceipt;
    }

    TransactionReceiptImpl::Ptr createReceipt(bcos::u256 const& _gasUsed,
        const std::string_view& _contractAddress,
        std::shared_ptr<std::vector<bcos::protocol::LogEntry>> _logEntries, int32_t _status,
        bcos::bytes&& _output, bcos::protocol::BlockNumber _blockNumber) override
    {
        return createReceipt(
            _gasUsed, _contractAddress, _logEntries, _status, _output, _blockNumber);
    }

    void setCryptoSuite(bcos::crypto::CryptoSuite::Ptr cryptoSuite) { m_cryptoSuite = cryptoSuite; }
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcostars