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
#include "Transaction.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <utility>

namespace bcos::protocol
{
class TransactionFactory
{
public:
    TransactionFactory() = default;
    TransactionFactory(const TransactionFactory&) = default;
    TransactionFactory(TransactionFactory&&) = default;
    TransactionFactory& operator=(const TransactionFactory&) = default;
    TransactionFactory& operator=(TransactionFactory&&) = default;
    using Ptr = std::shared_ptr<TransactionFactory>;
    virtual ~TransactionFactory() = default;

    virtual Transaction::Ptr createTransaction(
        bytesConstRef txData, bool checkSig = true, bool checkHash = false) = 0;
    virtual Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bytes const& _input, std::string const& _nonce, int64_t blockLimit, std::string _chainId,
        std::string _groupId, int64_t _importTime, std::string _abi = "", std::string _value = "",
        std::string _gasPrice = "", int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string maxPriorityFeePerGas = "") = 0;
    virtual Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bytes const& _input, std::string const& _nonce, int64_t _blockLimit, std::string _chainId,
        std::string _groupId, int64_t _importTime, const bcos::crypto::KeyPairInterface& keyPair,
        std::string _abi = "", std::string _value = "", std::string _gasPrice = "",
        int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string maxPriorityFeePerGas = "") = 0;
    Transaction::Ptr createTransaction(int32_t _version, std::string _to, bytes const& _input,
        std::string const& _nonce, int64_t _blockLimit, std::string _chainId, std::string _groupId,
        int64_t _importTime, const bcos::crypto::KeyPairInterface::Ptr& keyPair,
        std::string _abi = "")
    {
        return createTransaction(_version, std::move(_to), _input, _nonce, _blockLimit,
            std::move(_chainId), std::move(_groupId), _importTime, *keyPair, std::move(_abi));
    }
    virtual bcos::crypto::CryptoSuite::Ptr cryptoSuite() = 0;
};
}  // namespace bcos::protocol