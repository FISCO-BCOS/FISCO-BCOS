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
#include "bcos-utilities/AnyHolder.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/FixedBytes.h>
#include <gsl/span>

namespace bcos::protocol
{
class LogEntry;
class TransactionReceipt : public virtual MoveBase<TransactionReceipt>
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
    virtual LogEntries takeLogEntries() = 0;
    virtual protocol::BlockNumber blockNumber() const = 0;
    virtual std::string_view effectiveGasPrice() const = 0;
    virtual void setEffectiveGasPrice(std::string effectiveGasPrice) = 0;

    // additional information on transaction execution, no need to be involved in the hash
    // calculation
    virtual std::string const& message() const = 0;
    virtual void setMessage(std::string message) = 0;
    virtual size_t size() const = 0;

    // Fields after block execution
    virtual std::string_view cumulativeGasUsed() const = 0;
    virtual void setCumulativeGasUsed(std::string cumulativeGasUsed) = 0;
    virtual bcos::bytesConstRef logsBloom() const = 0;
    virtual void setLogsBloom(bcos::bytesConstRef logsBloom) = 0;
    virtual size_t transactionIndex() const = 0;
    virtual void setTransactionIndex(size_t index) = 0;
    virtual size_t logIndex() const = 0;
    virtual void setLogIndex(size_t index) = 0;

    friend std::ostream& operator<<(std::ostream& output, const TransactionReceipt& receipt)
    {
        output << "TransactionReceipt{" << "hash=" << receipt.hash() << ", "
               << "version=" << receipt.version() << ", "
               << "gasUsed=" << receipt.gasUsed() << ", "
               << "contractAddress=" << receipt.contractAddress() << ", "
               << "status=" << receipt.status() << ", " << "output=" << toHex(receipt.output())
               << ", "
               << "logEntries=" << receipt.logEntries().size() << ", "
               << "blockNumber=" << receipt.blockNumber() << ", "
               << "effectiveGasPrice=" << receipt.effectiveGasPrice() << ", "
               << "message=" << receipt.message() << "}";
        return output;
    }
};
using Receipts = std::vector<TransactionReceipt::Ptr>;
using ReceiptsPtr = std::shared_ptr<Receipts>;
using ReceiptsConstPtr = std::shared_ptr<const Receipts>;
using AnyTransactionReceipt = AnyHolder<TransactionReceipt, 72>;

}  // namespace bcos::protocol
