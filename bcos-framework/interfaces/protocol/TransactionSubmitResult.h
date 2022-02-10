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
 * @file TransactionSubmitResult.h
 * @author: yujiechen
 * @date: 2021-04-07
 */
#pragma once
#include "ProtocolTypeDef.h"
#include "TransactionReceipt.h"
namespace bcos
{
namespace protocol
{
class TransactionSubmitResult
{
public:
    using Ptr = std::shared_ptr<TransactionSubmitResult>;
    virtual ~TransactionSubmitResult() = default;

    virtual uint32_t status() const = 0;
    virtual void setStatus(uint32_t status) = 0;

    virtual bcos::crypto::HashType txHash() const = 0;
    virtual void setTxHash(bcos::crypto::HashType txHash) = 0;

    virtual bcos::crypto::HashType blockHash() const = 0;
    virtual void setBlockHash(bcos::crypto::HashType blockHash) = 0;

    virtual int64_t transactionIndex() const = 0;
    virtual void setTransactionIndex(int64_t index) = 0;

    virtual NonceType nonce() const = 0;
    virtual void setNonce(NonceType nonce) = 0;

    virtual TransactionReceipt::Ptr transactionReceipt() const = 0;
    virtual void setTransactionReceipt(TransactionReceipt::Ptr transactionReceipt) = 0;

    virtual std::string const& sender() const = 0;
    virtual void setSender(std::string const& _sender) = 0;

    virtual std::string const& to() const = 0;
    virtual void setTo(std::string const& _to) = 0;
};

using TransactionSubmitResults = std::vector<TransactionSubmitResult::Ptr>;
using TransactionSubmitResultsPtr = std::shared_ptr<TransactionSubmitResults>;
}  // namespace protocol
}  // namespace bcos