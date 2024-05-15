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
 * @brief tars implementation for Transaction
 * @file TransactionImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include "bcos-tars-protocol/tars/Transaction.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>

namespace bcostars::protocol
{

class TransactionImpl : public bcos::protocol::Transaction
{
public:
    explicit TransactionImpl(std::function<bcostars::Transaction*()> inner)
      : m_inner(std::move(inner))
    {}
    ~TransactionImpl() override = default;
    TransactionImpl& operator=(const TransactionImpl& _tx) = delete;
    TransactionImpl(const TransactionImpl& _tx) = delete;
    TransactionImpl& operator=(TransactionImpl&& _tx) = delete;
    TransactionImpl(TransactionImpl&& _tx) = delete;

    friend class TransactionFactoryImpl;

    bool operator==(const Transaction& rhs) const { return this->hash() == rhs.hash(); }

    void decode(bcos::bytesConstRef _txData) override;
    void encode(bcos::bytes& txData) const override;

    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl);

    int32_t version() const override;
    std::string_view chainId() const override;
    std::string_view groupId() const override;
    int64_t blockLimit() const override;
    const std::string& nonce() const override;
    // only for test
    void setNonce(std::string nonce) override;
    std::string_view to() const override;
    std::string_view abi() const override;

    std::string_view value() const override;
    std::string_view gasPrice() const override;
    int64_t gasLimit() const override;
    std::string_view maxFeePerGas() const override;
    std::string_view maxPriorityFeePerGas() const override;
    bcos::bytesConstRef extension() const override;

    bcos::bytesConstRef input() const override;
    int64_t importTime() const override;
    void setImportTime(int64_t _importTime) override;
    bcos::bytesConstRef signatureData() const override;
    std::string_view sender() const override;
    void forceSender(const bcos::bytes& _sender) const override;

    void setSignatureData(bcos::bytes& signature);

    int32_t attribute() const override;
    void setAttribute(int32_t attribute) override;

    std::string_view extraData() const override;
    void setExtraData(std::string const& _extraData) override;

    uint8_t type() const override;
    bcos::bytesConstRef extraTransactionBytes() const override;

    const bcostars::Transaction& inner() const;
    bcostars::Transaction& mutableInner();
    void setInner(bcostars::Transaction inner);

private:
    std::function<bcostars::Transaction*()> m_inner;
};
}  // namespace bcostars::protocol
