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
 * @brief factory for PBTransaction
 * @file PBTransactionFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "PBTransaction.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <memory>

namespace bcos
{
namespace protocol
{
class PBTransactionFactory : public TransactionFactory
{
public:
    using Ptr = std::shared_ptr<PBTransactionFactory>;
    explicit PBTransactionFactory(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
    {
        m_cryptoSuite = _cryptoSuite;
    }

    ~PBTransactionFactory() override {}

    Transaction::Ptr createTransaction(bytesConstRef _txData, bool _checkSig = true) override
    {
        return std::make_shared<PBTransaction>(m_cryptoSuite, _txData, _checkSig);
    }

    Transaction::Ptr createTransaction(bytes const& _txData, bool _checkSig = true) override
    {
        return std::make_shared<PBTransaction>(m_cryptoSuite, _txData, _checkSig);
    }

    Transaction::Ptr createTransaction(int32_t _version, const std::string_view& _to,
        bytes const& _input, u256 const& _nonce, int64_t _blockLimit, std::string const& _chainId,
        std::string const& _groupId, int64_t _importTime) override
    {
        return std::make_shared<PBTransaction>(m_cryptoSuite, _version, _to, _input, _nonce,
            _blockLimit, _chainId, _groupId, _importTime);
    }

    Transaction::Ptr createTransaction(int32_t _version, const std::string_view& _to,
        bytes const& _input, u256 const& _nonce, int64_t _blockLimit, std::string const& _chainId,
        std::string const& _groupId, int64_t _importTime,
        bcos::crypto::KeyPairInterface::Ptr keyPair) override
    {
        auto tx = createTransaction(
            _version, _to, _input, _nonce, _blockLimit, _chainId, _groupId, _importTime);
        auto sign = m_cryptoSuite->signatureImpl()->sign(*keyPair, tx->hash(), true);

        std::dynamic_pointer_cast<PBTransaction>(tx)->updateSignature(
            bcos::ref(*sign), keyPair->publicKey()->data());

        return tx;
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcos