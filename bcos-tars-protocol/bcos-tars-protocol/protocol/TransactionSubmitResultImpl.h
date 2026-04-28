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

#include "bcos-tars-protocol/tars/TransactionSubmitResult.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-utilities/Common.h>
#include <boost/lexical_cast.hpp>
#include <functional>

namespace bcostars::protocol
{
// Note: this will create a default transactionReceipt
class TransactionSubmitResultImpl : public bcos::protocol::TransactionSubmitResult
{
public:
    TransactionSubmitResultImpl();

    TransactionSubmitResultImpl(std::function<bcostars::TransactionSubmitResult*()> inner);
    uint32_t status() const override;
    void setStatus(uint32_t status) override;

    bcos::crypto::HashType txHash() const override;
    void setTxHash(bcos::crypto::HashType txHash) override;

    bcos::crypto::HashType blockHash() const override;
    void setBlockHash(bcos::crypto::HashType blockHash) override;

    int64_t transactionIndex() const override;
    void setTransactionIndex(int64_t index) override;

    bcos::protocol::NonceType nonce() const override;
    void setNonce(bcos::protocol::NonceType nonce) override;

    bcos::protocol::TransactionReceipt::ConstPtr transactionReceipt() const override;
    void setTransactionReceipt(
        bcos::protocol::TransactionReceipt::ConstPtr transactionReceipt) override;

    bcostars::TransactionSubmitResult const& inner();

    std::string const& sender() const override;
    void setSender(std::string const& _sender) override;

    std::string const& to() const override;
    void setTo(std::string const& _to) override;

    uint8_t type() const override;
    void setType(uint8_t _type) override;

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::function<bcostars::TransactionSubmitResult*()> m_inner;
};
}  // namespace bcostars::protocol