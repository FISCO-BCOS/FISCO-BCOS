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
 * @file TransactionSubmitResultImpl.h
 * @author: yujiechen
 * @date: 2021-04-07
 */
#pragma once
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>

namespace bcos
{
namespace protocol
{
class TransactionSubmitResultImpl : public TransactionSubmitResult
{
public:
    using Ptr = std::shared_ptr<TransactionSubmitResultImpl>;
    ~TransactionSubmitResultImpl() override {}

    uint32_t status() const override { return m_status; }
    void setStatus(uint32_t status) override { m_status = status; }

    bcos::crypto::HashType txHash() const override { return m_txHash; }
    void setTxHash(bcos::crypto::HashType txHash) override { m_txHash = txHash; }

    bcos::crypto::HashType blockHash() const override { return m_blockHash; }
    void setBlockHash(bcos::crypto::HashType blockHash) override { m_blockHash = blockHash; }

    int64_t transactionIndex() const override { return m_transactionIndex; }
    void setTransactionIndex(int64_t transactionIndex) override
    {
        m_transactionIndex = transactionIndex;
    }

    NonceType nonce() const override { return m_nonce; }
    void setNonce(NonceType nonce) override { m_nonce = nonce; }

    TransactionReceipt::Ptr transactionReceipt() const override { return m_receipt; }
    void setTransactionReceipt(TransactionReceipt::Ptr transactionReceipt) override
    {
        m_receipt = std::move(transactionReceipt);
    }

    std::string const& sender() const override { return m_sender; }
    void setSender(std::string const& _sender) override { m_sender = _sender; }

    std::string const& to() const override { return m_to; }
    void setTo(std::string const& _to) override { m_to = _to; }

private:
    uint32_t m_status = (uint32_t)TransactionStatus::None;
    bcos::crypto::HashType m_txHash;
    bcos::crypto::HashType m_blockHash;
    int64_t m_transactionIndex;
    NonceType m_nonce = -1;
    TransactionReceipt::Ptr m_receipt;
    std::string m_sender;
    std::string m_to;
};
}  // namespace protocol
}  // namespace bcos