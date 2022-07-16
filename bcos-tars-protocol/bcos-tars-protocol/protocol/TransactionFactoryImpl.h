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
#include <bcos-framework//protocol/TransactionFactory.h>

namespace bcostars
{
namespace protocol
{
class TransactionFactoryImpl : public bcos::protocol::TransactionFactory
{
public:
    TransactionFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite) : m_cryptoSuite(cryptoSuite)
    {}
    ~TransactionFactoryImpl() override {}

    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::bytesConstRef _txData, bool _checkSig = true) override
    {
        auto transaction = std::make_shared<TransactionImpl>(m_cryptoSuite,
            [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });

        transaction->decode(_txData);
        if (_checkSig)
        {
            transaction->verify();
        }
        return transaction;
    }

    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::bytes const& _txData, bool _checkSig = true) override
    {
        return createTransaction(bcos::ref(_txData), _checkSig);
    }

    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version,
        const std::string_view& _to, bcos::bytes const& _input, bcos::u256 const& _nonce,
        int64_t _blockLimit, std::string const& _chainId, std::string const& _groupId,
        int64_t _importTime) override
    {
        auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>(m_cryptoSuite,
            [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });
        auto const& inner = transaction->innerGetter();
        inner()->data.version = _version;
        inner()->data.to.assign(_to.begin(), _to.end());
        inner()->data.input.assign(_input.begin(), _input.end());
        inner()->data.blockLimit = _blockLimit;
        inner()->data.chainID = _chainId;
        inner()->data.groupID = _groupId;
        inner()->data.nonce = boost::lexical_cast<std::string>(_nonce);
        inner()->importTime = _importTime;

        return transaction;
    }

    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version,
        const std::string_view& _to, bcos::bytes const& _input, bcos::u256 const& _nonce,
        int64_t _blockLimit, std::string const& _chainId, std::string const& _groupId,
        int64_t _importTime, bcos::crypto::KeyPairInterface::Ptr keyPair) override
    {
        auto tx = createTransaction(
            _version, _to, _input, _nonce, _blockLimit, _chainId, _groupId, _importTime);
        auto sign = m_cryptoSuite->signatureImpl()->sign(*keyPair, tx->hash(), true);

        auto tarsTx = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        auto const& inner = tarsTx->innerGetter();
        inner()->signature.assign(sign->begin(), sign->end());

        return tx;
    }

    void setCryptoSuite(bcos::crypto::CryptoSuite::Ptr cryptoSuite) { m_cryptoSuite = cryptoSuite; }
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};

}  // namespace protocol
}  // namespace bcostars