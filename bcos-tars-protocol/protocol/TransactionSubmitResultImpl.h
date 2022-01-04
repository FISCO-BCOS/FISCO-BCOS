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
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/TransactionSubmitResult.h"
#include "interfaces/crypto/CommonType.h"
#include <bcos-framework/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/interfaces/protocol/TransactionSubmitResult.h>
#include <bcos-utilities/Common.h>
#include <boost/lexical_cast.hpp>
#include <memory>

namespace bcostars
{
namespace protocol
{
class TransactionSubmitResultImpl : public bcos::protocol::TransactionSubmitResult
{
public:
    TransactionSubmitResultImpl(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
      : m_cryptoSuite(_cryptoSuite),
        m_inner([inner = bcostars::TransactionSubmitResult()]() mutable { return &inner; })
    {}

    TransactionSubmitResultImpl(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        std::function<bcostars::TransactionSubmitResult*()> inner)
      : m_cryptoSuite(_cryptoSuite), m_inner(std::move(inner))
    {}
    uint32_t status() const override { return m_inner()->status; }
    void setStatus(uint32_t status) override { m_inner()->status = status; }

    bcos::crypto::HashType txHash() const override
    {
        if (m_inner()->txHash.size() == bcos::crypto::HashType::size)
        {
            return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner()->txHash.data()));
        }
        return bcos::crypto::HashType();
    }
    void setTxHash(bcos::crypto::HashType txHash) override
    {
        m_inner()->txHash.assign(txHash.begin(), txHash.end());
    }

    bcos::crypto::HashType blockHash() const override
    {
        if (m_inner()->blockHash.size() == bcos::crypto::HashType::size)
        {
            return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner()->blockHash.data()));
        }
        return bcos::crypto::HashType();
    }
    void setBlockHash(bcos::crypto::HashType blockHash) override
    {
        m_inner()->blockHash.assign(blockHash.begin(), blockHash.end());
    }

    int64_t transactionIndex() const override { return m_inner()->transactionIndex; }
    void setTransactionIndex(int64_t index) override { m_inner()->transactionIndex = index; }

    bcos::protocol::NonceType nonce() const override
    {
        return bcos::protocol::NonceType(m_inner()->nonce);
    }
    void setNonce(bcos::protocol::NonceType nonce) override { m_inner()->nonce = nonce.str(); }

    bcos::protocol::TransactionReceipt::Ptr transactionReceipt() const override
    {
        return std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            m_cryptoSuite, [innerPtr = &m_inner()->transactionReceipt]() { return innerPtr; });
    }
    void setTransactionReceipt(bcos::protocol::TransactionReceipt::Ptr transactionReceipt) override
    {
        auto transactionReceiptImpl =
            std::dynamic_pointer_cast<TransactionReceiptImpl>(transactionReceipt);
        m_inner()->transactionReceipt = transactionReceiptImpl->inner();  // FIXME: copy here!
    }

    bcostars::TransactionSubmitResult const& inner() { return *m_inner(); }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::function<bcostars::TransactionSubmitResult*()> m_inner;
};
}  // namespace protocol
}  // namespace bcostars