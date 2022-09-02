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

#include "../Common.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <variant>

namespace bcostars
{
namespace protocol
{
class TransactionReceiptImpl : public bcos::protocol::TransactionReceipt
{
public:
    TransactionReceiptImpl() = delete;

    explicit TransactionReceiptImpl(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        std::function<bcostars::TransactionReceipt*()> inner)
      : bcos::protocol::TransactionReceipt(_cryptoSuite), m_inner(inner)
    {}

    ~TransactionReceiptImpl() override {}
    void decode(bcos::bytesConstRef _receiptData) override;
    void encode(bcos::bytes& _encodedData) const override;
    bcos::crypto::HashType hash() const override;

    int32_t version() const override { return m_inner()->data.version; }
    bcos::u256 gasUsed() const override;

    std::string_view contractAddress() const override { return m_inner()->data.contractAddress; }
    int32_t status() const override { return m_inner()->data.status; }
    bcos::bytesConstRef output() const override
    {
        return bcos::bytesConstRef(
            (const unsigned char*)m_inner()->data.output.data(), m_inner()->data.output.size());
    }
    gsl::span<const bcos::protocol::LogEntry> logEntries() const override
    {
        if (m_logEntries.empty())
        {
            m_logEntries.reserve(m_inner()->data.logEntries.size());
            for (auto& it : m_inner()->data.logEntries)
            {
                auto bcosLogEntry = toBcosLogEntry(it);
                m_logEntries.emplace_back(std::move(bcosLogEntry));
            }
        }

        return gsl::span<const bcos::protocol::LogEntry>(m_logEntries.data(), m_logEntries.size());
    }
    bcos::protocol::BlockNumber blockNumber() const override { return m_inner()->data.blockNumber; }

    const bcostars::TransactionReceipt& inner() const { return *m_inner(); }

    void setInner(const bcostars::TransactionReceipt& inner) { *m_inner() = inner; }
    void setInner(bcostars::TransactionReceipt&& inner) { *m_inner() = std::move(inner); }

    std::function<bcostars::TransactionReceipt*()> const& innerGetter() { return m_inner; }

    void setLogEntries(std::vector<bcos::protocol::LogEntry> const& _logEntries)
    {
        m_logEntries.clear();
        m_inner()->data.logEntries.clear();
        m_inner()->data.logEntries.reserve(_logEntries.size());

        for (auto& it : _logEntries)
        {
            auto tarsLogEntry = toTarsLogEntry(it);
            m_inner()->data.logEntries.emplace_back(std::move(tarsLogEntry));
        }
    }

    std::string const& message() const override { return m_inner()->message; }

    void setMessage(std::string const& _message) override { m_inner()->message = _message; }

    void setMessage(std::string&& _message) override { m_inner()->message = std::move(_message); }

private:
    std::function<bcostars::TransactionReceipt*()> m_inner;
    mutable std::vector<bcos::protocol::LogEntry> m_logEntries;
};
}  // namespace protocol
}  // namespace bcostars