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
 * @brief tars implementation for TransactionSubmitResult
 * @file TransactionSubmitResultImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include "TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/TransactionSubmitResult.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-utilities/Common.h>
#include <boost/lexical_cast.hpp>
#include <memory>
#include <utility>

namespace bcostars::protocol
{
// Note: this will create a default transactionReceipt
class TransactionSubmitResultImpl : public bcos::protocol::TransactionSubmitResult
{
public:
    TransactionSubmitResultImpl()
      : m_inner([inner = bcostars::TransactionSubmitResult()]() mutable { return &inner; })
    {}

    TransactionSubmitResultImpl(std::function<bcostars::TransactionSubmitResult*()> inner)
      : m_inner(std::move(inner))
    {}
    uint32_t status() const override { return m_inner()->status; }
    void setStatus(uint32_t status) override { m_inner()->status = status; }

    bcos::crypto::HashType txHash() const override
    {
        if (m_inner()->txHash.size() == bcos::crypto::HashType::SIZE)
        {
            bcos::crypto::HashType hash(
                (const bcos::byte*)m_inner()->txHash.data(), m_inner()->txHash.size());
            return hash;
        }
        return {};
    }
    void setTxHash(bcos::crypto::HashType txHash) override
    {
        m_inner()->txHash.assign(txHash.begin(), txHash.end());
    }

    bcos::crypto::HashType blockHash() const override
    {
        if (m_inner()->blockHash.size() == bcos::crypto::HashType::SIZE)
        {
            bcos::crypto::HashType hash(
                (const bcos::byte*)m_inner()->blockHash.data(), m_inner()->blockHash.size());
            return hash;
        }
        return {};
    }
    void setBlockHash(bcos::crypto::HashType blockHash) override
    {
        m_inner()->blockHash.assign(blockHash.begin(), blockHash.end());
    }

    int64_t transactionIndex() const override { return m_inner()->transactionIndex; }
    void setTransactionIndex(int64_t index) override { m_inner()->transactionIndex = index; }

    bcos::protocol::NonceType nonce() const override { return {m_inner()->nonce}; }
    void setNonce(bcos::protocol::NonceType nonce) override { m_inner()->nonce = std::move(nonce); }

    bcos::protocol::TransactionReceipt::ConstPtr transactionReceipt() const override
    {
        return std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            [innerPtr = &m_inner()->transactionReceipt]() { return innerPtr; });
    }
    void setTransactionReceipt(
        bcos::protocol::TransactionReceipt::ConstPtr transactionReceipt) override
    {
        auto transactionReceiptImpl =
            std::dynamic_pointer_cast<TransactionReceiptImpl const>(transactionReceipt);
        m_inner()->transactionReceipt = transactionReceiptImpl->inner();  // FIXME: copy here!
    }

    bcostars::TransactionSubmitResult const& inner() { return *m_inner(); }

    std::string const& sender() const override { return m_inner()->sender; }
    void setSender(std::string const& _sender) override { m_inner()->sender = _sender; }

    std::string const& to() const override { return m_inner()->to; }
    void setTo(std::string const& _to) override { m_inner()->to = _to; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::function<bcostars::TransactionSubmitResult*()> m_inner;
};
}  // namespace bcostars::protocol