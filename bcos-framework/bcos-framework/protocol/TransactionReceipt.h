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
 * @brief interface for TransactionReceipt
 * @file TransactionReceipt.h
 */
#pragma once

#include "LogEntry.h"
#include "ProtocolTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/FixedBytes.h>
#include <gsl/span>

namespace bcos::protocol
{
class LogEntry;
class TransactionReceipt
{
public:
    using Ptr = std::shared_ptr<TransactionReceipt>;
    using ConstPtr = std::shared_ptr<const TransactionReceipt>;

    virtual ~TransactionReceipt() = default;

    virtual void decode(bytesConstRef _receiptData) = 0;
    virtual void encode(bytes& _encodedData) const = 0;
    virtual bcos::crypto::HashType hash() const = 0;
    virtual void calculateHash(const crypto::Hash& hashImpl) = 0;
    virtual int32_t version() const = 0;
    virtual bcos::u256 gasUsed() const = 0;
    virtual std::string_view contractAddress() const = 0;
    virtual int32_t status() const = 0;
    virtual bcos::bytesConstRef output() const = 0;
    virtual gsl::span<const LogEntry> logEntries() const = 0;
    virtual LogEntries&& takeLogEntries() = 0;
    virtual protocol::BlockNumber blockNumber() const = 0;
    virtual std::string_view effectiveGasPrice() const = 0;
    virtual void setEffectiveGasPrice(std::string effectiveGasPrice) = 0;

    // additional information on transaction execution, no need to be involved in the hash
    // calculation
    virtual std::string const& message() const = 0;
    virtual void setMessage(std::string message) = 0;

    virtual std::string toString() const { return {}; }
};
using Receipts = std::vector<TransactionReceipt::Ptr>;
using ReceiptsPtr = std::shared_ptr<Receipts>;
using ReceiptsConstPtr = std::shared_ptr<const Receipts>;

}  // namespace bcos::protocol
