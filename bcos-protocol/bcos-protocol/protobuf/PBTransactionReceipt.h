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
 * @brief PB implementation of TransactionReceipt
 * @file PBTransactionReceipt.h
 * @author: yujiechen
 * @date: 2021-03-18
 */
#pragma once
#include "../TransactionStatus.h"
#include "bcos-protocol/protobuf/proto/TransactionReceipt.pb.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/LogEntry.h>
#include <bcos-framework/interfaces/protocol/TransactionReceipt.h>
namespace bcos
{
namespace protocol
{
class PBTransactionReceipt : public TransactionReceipt
{
public:
    PBTransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _receiptData);
    PBTransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytes const& _receiptData)
      : PBTransactionReceipt(_cryptoSuite, ref(_receiptData))
    {}

    PBTransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, int32_t _version,
        u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
        int32_t _status, bytes const& _output, BlockNumber _blockNumber);

    PBTransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, int32_t _version,
        u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
        int32_t _status, bytes&& _output, BlockNumber _blockNumber);

    ~PBTransactionReceipt() {}

    void decode(bytesConstRef _receiptData) override;
    void encode(bytes& _encodeReceiptData) const override;
    bytesConstRef encode(bool _onlyHashFieldData = false) const override;

    int32_t version() const override { return m_receipt->version(); }
    int32_t status() const override { return m_status; }
    bytesConstRef output() const override { return ref(m_output); }
    std::string_view contractAddress() const override
    {
        return std::string_view((char*)m_contractAddress.data(), m_contractAddress.size());
    }
    u256 gasUsed() const override { return m_gasUsed; }
    gsl::span<const LogEntry> logEntries() const override
    {
        return gsl::span<const LogEntry>(m_logEntries->data(), m_logEntries->size());
    }
    BlockNumber blockNumber() const override { return m_blockNumber; }

private:
    PBTransactionReceipt(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, int32_t _version,
        u256 const& _gasUsed, const std::string_view& _contractAddress, LogEntriesPtr _logEntries,
        int32_t _status, BlockNumber _blockNumber);
    virtual void encodeHashFields() const;

private:
    std::shared_ptr<PBRawTransactionReceipt> m_receipt;
    bytesPointer m_dataCache;

    int32_t m_version;
    u256 m_gasUsed;
    bytes m_contractAddress;
    LogEntriesPtr m_logEntries;
    int32_t m_status;
    bytes m_output;
    BlockNumber m_blockNumber;
};
}  // namespace protocol
}  // namespace bcos