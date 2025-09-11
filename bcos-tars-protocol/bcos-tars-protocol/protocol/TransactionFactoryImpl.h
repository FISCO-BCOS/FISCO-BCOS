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
 * @brief tars implementation for TransactionFactory
 * @file TransactionFactoryImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once

#include "TransactionImpl.h"
#include <bcos-framework/protocol/TransactionFactory.h>

namespace bcostars::protocol
{
class TransactionFactoryImpl : public bcos::protocol::TransactionFactory
{
public:
    TransactionFactoryImpl(const TransactionFactoryImpl&) = default;
    TransactionFactoryImpl(TransactionFactoryImpl&&) = default;
    TransactionFactoryImpl& operator=(const TransactionFactoryImpl&) = default;
    TransactionFactoryImpl& operator=(TransactionFactoryImpl&&) = default;
    TransactionFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite);
    ~TransactionFactoryImpl() noexcept override = default;

    bcos::protocol::Transaction::Ptr createTransaction() override;
    bcos::protocol::Transaction::Ptr createTransaction(bcos::protocol::Transaction& input) override;
    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::bytesConstRef txData, bool checkSig = true, bool checkHash = false) override;

    std::shared_ptr<bcos::protocol::Transaction> createTransaction(int32_t _version,
        std::string _to, bcos::bytes const& _input, std::string const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime, std::string _abi = {},
        std::string _value = {}, std::string _gasPrice = {}, int64_t _gasLimit = 0,
        std::string _maxFeePerGas = {}, std::string _maxPriorityFeePerGas = {}) override;


    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bcos::bytes const& _input, std::string const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime,
        const bcos::crypto::KeyPairInterface& keyPair, std::string _abi = {},
        std::string _value = {}, std::string _gasPrice = {}, int64_t _gasLimit = 0,
        std::string _maxFeePerGas = {}, std::string _maxPriorityFeePerGas = {}) override;

    void setCryptoSuite(bcos::crypto::CryptoSuite::Ptr cryptoSuite);
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override;

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};

}  // namespace bcostars::protocol
