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
 * @brief factory interface of Transaction
 * @file TransactionFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "../../interfaces/crypto/CryptoSuite.h"
#include "Transaction.h"
namespace bcos
{
namespace protocol
{
class TransactionFactory
{
public:
    using Ptr = std::shared_ptr<TransactionFactory>;
    TransactionFactory() = default;
    virtual ~TransactionFactory() {}
    virtual Transaction::Ptr createTransaction(bytesConstRef _txData, bool _checkSig = true) = 0;
    virtual Transaction::Ptr createTransaction(bytes const& _txData, bool _checkSig = true) = 0;
    virtual Transaction::Ptr createTransaction(int32_t _version, const std::string_view& _to,
        bytes const& _input, u256 const& _nonce, int64_t blockLimit, std::string const& _chainId,
        std::string const& _groupId, int64_t _importTime) = 0;
    virtual Transaction::Ptr createTransaction(int32_t _version, const std::string_view& _to,
        bytes const& _input, u256 const& _nonce, int64_t _blockLimit, std::string const& _chainId,
        std::string const& _groupId, int64_t _importTime,
        bcos::crypto::KeyPairInterface::Ptr keyPair) = 0;
    virtual bcos::crypto::CryptoSuite::Ptr cryptoSuite() = 0;
};
}  // namespace protocol
}  // namespace bcos