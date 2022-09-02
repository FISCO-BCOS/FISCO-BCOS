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
 * @brief Transaction coded in PB format
 * @file PBTransaction.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#pragma once
#include "bcos-protocol/protobuf/proto/Transaction.pb.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/RefDataContainer.h>

namespace bcos
{
namespace protocol
{
class PBTransaction : public Transaction
{
public:
    using Ptr = std::shared_ptr<PBTransaction>;
    PBTransaction(bcos::crypto::CryptoSuite::Ptr _cryptoSuite, int32_t _version,
        const std::string_view& _to, bytes const& _input, u256 const& _nonce, int64_t _blockLimit,
        std::string const& _chainId, std::string const& _groupId, int64_t _importTime,
        std::string const& _abi = "");

    explicit PBTransaction(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytesConstRef _txData, bool _checkSig);
    explicit PBTransaction(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bytes const& _txData, bool _checkSig)
      : PBTransaction(_cryptoSuite, &_txData, _checkSig)
    {}

    ~PBTransaction() override {}

    bool operator==(PBTransaction const& _comparedTx)
    {
        return (type() == _comparedTx.type()) && (importTime() == _comparedTx.importTime()) &&
               (hash() == _comparedTx.hash());
    }

    void decode(bytesConstRef _txData) override;
    void encode(bcos::bytes& txData) const override;

    bcos::crypto::HashType hash(bool _useCache = true) const override;

    u256 nonce() const override { return m_nonce; }
    int32_t version() const override { return m_transactionHashFields->version(); }
    std::string_view chainId() const override { return m_transactionHashFields->chainid(); }
    std::string_view groupId() const override { return m_transactionHashFields->groupid(); }
    int64_t blockLimit() const override { return m_transactionHashFields->blocklimit(); }
    std::string_view to() const override { return m_transactionHashFields->to(); }
    std::string_view abi() const override { return m_transactionHashFields->abi(); }

    bytesConstRef input() const override;
    int64_t importTime() const override { return m_transaction->import_time(); }
    void setImportTime(int64_t _importTime) override
    {
        m_transaction->set_import_time(_importTime);
    }
    bytesConstRef signatureData() const override
    {
        return bytesConstRef((const byte*)m_transaction->signaturedata().data(),
            m_transaction->signaturedata().size());
    }

    // only for ut
    void updateSignature(bytesConstRef _signatureData, bytes const& _sender);

    uint32_t attribute() const override { return m_transaction->attribute(); }
    void setAttribute(uint32_t _attribute) override { m_transaction->set_attribute(_attribute); }

    std::string_view source() const override { return m_transaction->source(); }
    void setSource(std::string const& _source) override { m_transaction->set_source(_source); }

protected:
    explicit PBTransaction(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
      : Transaction(_cryptoSuite),
        m_transaction(std::make_shared<PBRawTransaction>()),
        m_transactionHashFields(std::make_shared<PBRawTransactionHashFields>()),
        m_dataCache(std::make_shared<bytes>())
    {
        GOOGLE_PROTOBUF_VERIFY_VERSION;
    }

private:
    std::shared_ptr<PBRawTransaction> m_transaction;
    std::shared_ptr<PBRawTransactionHashFields> m_transactionHashFields;
    bytesPointer m_dataCache;

    u256 m_nonce;
};
}  // namespace protocol
}  // namespace bcos
