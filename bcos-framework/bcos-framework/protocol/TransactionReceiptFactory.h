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
#include "Protocol.h"
#include "TransactionReceipt.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>

namespace bcos::protocol
{
class LogEntry;
class TransactionReceiptFactory
{
public:
    using Ptr = std::shared_ptr<TransactionReceiptFactory>;
    TransactionReceiptFactory() = default;
    TransactionReceiptFactory(const TransactionReceiptFactory&) = default;
    TransactionReceiptFactory(TransactionReceiptFactory&&) = default;
    TransactionReceiptFactory& operator=(const TransactionReceiptFactory&) = default;
    TransactionReceiptFactory& operator=(TransactionReceiptFactory&&) = default;

    virtual ~TransactionReceiptFactory() = default;
    virtual TransactionReceipt::Ptr createReceipt(bytesConstRef _receiptData) const = 0;
    virtual TransactionReceipt::Ptr createReceipt(bytes const& _receiptData) const = 0;
    virtual TransactionReceipt::Ptr createReceipt(u256 const& gasUsed, std::string contractAddress,
        const std::vector<LogEntry>& logEntries, int32_t status, bcos::bytesConstRef output,
        BlockNumber blockNumber) const = 0;
    virtual TransactionReceipt::Ptr createReceipt2(u256 const& gasUsed, std::string contractAddress,
        const std::vector<LogEntry>& logEntries, int32_t status, bcos::bytesConstRef output,
        BlockNumber blockNumber, std::string effectiveGasPrice = "1",
        TransactionVersion version = TransactionVersion::V1_VERSION,
        bool withHash = true) const = 0;
};

}  // namespace bcos::protocol
