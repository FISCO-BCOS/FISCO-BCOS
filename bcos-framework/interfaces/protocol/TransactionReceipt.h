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

#include "ProtocolTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/FixedBytes.h>
#include <gsl/span>

namespace bcos
{
namespace protocol
{
class LogEntry;
class TransactionReceipt
{
public:
    using Ptr = std::shared_ptr<TransactionReceipt>;
    using ConstPtr = std::shared_ptr<const TransactionReceipt>;
    explicit TransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
      : m_cryptoSuite(_cryptoSuite)
    {}

    virtual ~TransactionReceipt() {}

    virtual void decode(bytesConstRef _receiptData) = 0;
    virtual void encode(bytes& _encodedData) const = 0;
    virtual bytesConstRef encode(bool _onlyHashFieldData = false) const = 0;

    virtual bcos::crypto::HashType hash() const
    {
        auto hashFields = encode(true);
        return m_cryptoSuite->hash(hashFields);
    }

    virtual int32_t version() const = 0;
    virtual u256 gasUsed() const = 0;
    virtual std::string_view contractAddress() const = 0;
    virtual int32_t status() const = 0;
    virtual bytesConstRef output() const = 0;
    virtual gsl::span<const LogEntry> logEntries() const = 0;
    virtual bcos::crypto::CryptoSuite::Ptr cryptoSuite() { return m_cryptoSuite; }
    virtual BlockNumber blockNumber() const = 0;

    // additional information on transaction execution, no need to be involved in the hash
    // calculation
    virtual std::string const& message() const = 0;
    virtual void setMessage(std::string const& _message) = 0;
    virtual void setMessage(std::string&& _message) = 0;

protected:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
using Receipts = std::vector<TransactionReceipt::Ptr>;
using ReceiptsPtr = std::shared_ptr<Receipts>;
using ReceiptsConstPtr = std::shared_ptr<const Receipts>;
}  // namespace protocol
}  // namespace bcos
