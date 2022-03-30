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
 * @brief factory for TransactionReceipt
 * @file TransactionReceiptFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "TransactionReceipt.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>

namespace bcos
{
namespace protocol
{
class LogEntry;
class TransactionReceiptFactory
{
public:
    using Ptr = std::shared_ptr<TransactionReceiptFactory>;
    TransactionReceiptFactory() = default;
    virtual ~TransactionReceiptFactory() {}
    virtual TransactionReceipt::Ptr createReceipt(bytesConstRef _receiptData) = 0;
    virtual TransactionReceipt::Ptr createReceipt(bytes const& _receiptData) = 0;
    virtual TransactionReceipt::Ptr createReceipt(u256 const& _gasUsed,
        const std::string_view& _contractAddress,
        std::shared_ptr<std::vector<LogEntry>> _logEntries, int32_t _status, bytes const& _output,
        BlockNumber _blockNumber) = 0;

    virtual TransactionReceipt::Ptr createReceipt(u256 const& _gasUsed,
        const std::string_view& _contractAddress,
        std::shared_ptr<std::vector<LogEntry>> _logEntries, int32_t _status, bytes&& _output,
        BlockNumber _blockNumber) = 0;
    virtual bcos::crypto::CryptoSuite::Ptr cryptoSuite() = 0;
};
}  // namespace protocol
}  // namespace bcos