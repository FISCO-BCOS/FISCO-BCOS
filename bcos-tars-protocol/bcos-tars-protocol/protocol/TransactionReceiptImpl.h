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
 * @brief tars implementation for TransactionReceipt
 * @file TransactionReceiptImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-concepts/Hash.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <utility>

namespace bcostars::protocol
{
class TransactionReceiptImpl : public bcos::protocol::TransactionReceipt
{
public:
    TransactionReceiptImpl(const TransactionReceiptImpl&) = default;
    TransactionReceiptImpl(TransactionReceiptImpl&&) = delete;
    TransactionReceiptImpl& operator=(const TransactionReceiptImpl&) = default;
    TransactionReceiptImpl& operator=(TransactionReceiptImpl&&) = delete;
    explicit TransactionReceiptImpl(std::function<bcostars::TransactionReceipt*()> inner)
      : m_inner(std::move(inner))
    {}

    ~TransactionReceiptImpl() override = default;
    void decode(bcos::bytesConstRef _receiptData) override;
    void encode(bcos::bytes& _encodedData) const override;
    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl) override;

    int32_t version() const override;
    bcos::u256 gasUsed() const override;

    std::string_view contractAddress() const override;
    int32_t status() const override;
    bcos::bytesConstRef output() const override;
    gsl::span<const bcos::protocol::LogEntry> logEntries() const override;
    bcos::protocol::LogEntries&& takeLogEntries() override;
    bcos::protocol::BlockNumber blockNumber() const override;
    std::string_view effectiveGasPrice() const override;
    void setEffectiveGasPrice(std::string effectiveGasPrice) override;

    const bcostars::TransactionReceipt& inner() const;
    bcostars::TransactionReceipt& mutableInner();

    void setInner(const bcostars::TransactionReceipt& inner);
    void setInner(bcostars::TransactionReceipt&& inner);

    std::function<bcostars::TransactionReceipt*()> const& innerGetter();
    void setLogEntries(std::vector<bcos::protocol::LogEntry> const& _logEntries);
    std::string const& message() const override;
    void setMessage(std::string message) override;

    std::string toString() const override;

private:
    std::function<bcostars::TransactionReceipt*()> m_inner;
    mutable std::vector<bcos::protocol::LogEntry> m_logEntries;
};
}  // namespace bcostars::protocol