/*
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
 * @brief factory for PBTransactionReceipt
 * @file PBTransactionReceiptFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "PBTransactionReceipt.h"
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/TransactionReceiptFactory.h>

namespace bcos
{
namespace protocol
{
class PBTransactionReceiptFactory : public TransactionReceiptFactory
{
public:
    using Ptr = std::shared_ptr<PBTransactionReceiptFactory>;
    explicit PBTransactionReceiptFactory(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
      : m_cryptoSuite(_cryptoSuite)
    {}
    ~PBTransactionReceiptFactory() override {}

    TransactionReceipt::Ptr createReceipt(bytes const& _receiptData) override
    {
        return std::make_shared<PBTransactionReceipt>(m_cryptoSuite, _receiptData);
    }

    TransactionReceipt::Ptr createReceipt(bytesConstRef _receiptData) override
    {
        return std::make_shared<PBTransactionReceipt>(m_cryptoSuite, _receiptData);
    }

    TransactionReceipt::Ptr createReceipt(u256 const& _gasUsed,
        const std::string_view& _contractAddress, LogEntriesPtr _logEntries, int32_t _status,
        bytes const& _output, BlockNumber _blockNumber) override
    {
        return std::make_shared<PBTransactionReceipt>(m_cryptoSuite, 0, _gasUsed, _contractAddress,
            _logEntries, _status, _output, _blockNumber);
    }

    TransactionReceipt::Ptr createReceipt(u256 const& _gasUsed,
        const std::string_view& _contractAddress, LogEntriesPtr _logEntries, int32_t _status,
        bytes&& _output, BlockNumber _blockNumber) override
    {
        return std::make_shared<PBTransactionReceipt>(m_cryptoSuite, 0, _gasUsed, _contractAddress,
            _logEntries, _status, _output, _blockNumber);
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcos