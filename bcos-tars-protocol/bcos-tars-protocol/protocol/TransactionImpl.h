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
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/protocol/Authorization.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/Web3AccessList.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-utilities/Common.h"
#include <memory>
#include <optional>
#include <string_view>

namespace bcostars::protocol
{

class TransactionImpl : public bcos::protocol::Transaction
{
public:
    explicit TransactionImpl(std::function<bcostars::Transaction*()> inner);
    TransactionImpl();
    ~TransactionImpl() override = default;
    TransactionImpl& operator=(const TransactionImpl& _tx) = delete;
    TransactionImpl(const TransactionImpl& _tx) = delete;
    TransactionImpl& operator=(TransactionImpl&& _tx) = default;
    TransactionImpl(TransactionImpl&& _tx) = default;

    friend class TransactionFactoryImpl;

    bool operator==(const Transaction& rhs) const;

    void decode(bcos::bytesConstRef _txData) override;
    void encode(bcos::bytes& txData) const override;

    bcos::crypto::HashType hash() const override;
    void calculateHash(const bcos::crypto::Hash& hashImpl) override;

    int32_t version() const override;
    std::string_view chainId() const override;
    std::string_view groupId() const override;
    int64_t blockLimit() const override;
    std::string_view nonce() const override;
    // only for test
    void setNonce(std::string nonce) override;
    std::string_view to() const override;
    std::string_view abi() const override;

    bcos::u256 value() const override;
    std::optional<bcos::u256> gasPrice() const override;
    int64_t gasLimit() const override;
    std::optional<bcos::u256> maxFeePerGas() const override;
    std::optional<bcos::u256> maxPriorityFeePerGas() const override;
    bcos::bytesConstRef extension() const override;

    bcos::bytesConstRef input() const override;
    int64_t importTime() const override;
    void setImportTime(int64_t _importTime) override;
    bcos::bytesConstRef signatureData() const override;
    std::string_view sender() const override;
    void forceSender(const bcos::bytes& _sender) override;
    void clearSenderAndHash() override;

    void setSignatureData(bcos::bytes& signature);

    int32_t attribute() const override;
    void setAttribute(int32_t attribute) override;

    std::string_view extraData() const override;

    uint8_t type() const override;
    bcos::bytesConstRef extraTransactionBytes() const override;
    uint8_t web3TypedTxKind() const override;
    std::optional<uint64_t> web3ChainIdFromEnvelope() const override;
    std::string_view sourceHash() const override;
    bcos::u256 mint() const override;
    bool isDepositTx() const override;
    bool depositIsSystemTransaction() const override;
    bcos::protocol::Web3AccessList web3AccessList() const override;
    bcos::protocol::AuthorizationList authorizationList() const override;
    bcos::protocol::VersionedHashes blobVersionedHashes() const override;
    std::optional<bcos::u256> maxFeePerBlobGas() const override;

    const bcostars::Transaction& inner() const;
    bcostars::Transaction& mutableInner();
    void setInner(bcostars::Transaction inner);

    size_t size() const override;

private:
    std::function<bcostars::Transaction*()> m_inner;
};

// Guard: TransactionImpl must fit inside the AnyTransaction fixed-size buffer.
// If this assertion fires, update the size constant in
// bcos-framework/bcos-framework/protocol/Transaction.h  (using AnyTransaction = AnyHolder<..., N>).
static_assert(sizeof(TransactionImpl) <= 224,
    "TransactionImpl exceeds AnyTransaction buffer (224 bytes); "
    "update the size constant in bcos-framework/protocol/Transaction.h");

}  // namespace bcostars::protocol
