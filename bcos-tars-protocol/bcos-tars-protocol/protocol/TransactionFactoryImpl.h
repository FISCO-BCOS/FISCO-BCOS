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
#include "../impl/TarsHashable.h"
#include "TransactionImpl.h"
#include <bcos-concepts/Hash.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace bcostars::protocol
{
class TransactionFactoryImpl : public bcos::protocol::TransactionFactory
{
public:
    using TransactionType = TransactionImpl;

    TransactionFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
      : m_cryptoSuite(std::move(cryptoSuite))
    {}
    TransactionFactoryImpl(const TransactionFactoryImpl&) = default;
    TransactionFactoryImpl(TransactionFactoryImpl&&) = default;
    TransactionFactoryImpl& operator=(const TransactionFactoryImpl&) = default;
    TransactionFactoryImpl& operator=(TransactionFactoryImpl&&) = default;
    ~TransactionFactoryImpl() override = default;

    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::bytesConstRef txData, bool checkSig = true, bool checkHash = false) override
    {
        auto transaction = std::make_shared<TransactionImpl>(
            [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });

        transaction->decode(txData);

        auto originDataHash = std::move(transaction->mutableInner().dataHash);
        transaction->mutableInner().dataHash.clear();

        auto anyHasher = m_cryptoSuite->hashImpl()->hasher();
        std::visit(
            [&transaction](auto& hasher) {
                transaction->calculateHash<std::remove_cvref_t<decltype(hasher)>>();
            },
            anyHasher);

        // check if hash matching
        if (checkHash && !originDataHash.empty() &&
            (originDataHash != transaction->mutableInner().dataHash)) [[unlikely]]
        {
            bcos::crypto::HashType originHashResult(
                (bcos::byte*)originDataHash.data(), originDataHash.size());
            bcos::crypto::HashType hashResult(
                (bcos::byte*)transaction->mutableInner().dataHash.data(),
                transaction->mutableInner().dataHash.size());

            BCOS_LOG(WARNING) << LOG_DESC("the transaction hash does not match")
                              << LOG_KV("originHash", originHashResult.hex())
                              << LOG_KV("realHash", hashResult.hex());
            BOOST_THROW_EXCEPTION(std::invalid_argument("transaction hash mismatching"));
        }

        if (checkSig)
        {
            transaction->verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
        }
        return transaction;
    }

    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bcos::bytes const& _input, bcos::u256 const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime) override
    {
        auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>(
            [m_transaction = bcostars::Transaction()]() mutable { return &m_transaction; });
        auto& inner = transaction->mutableInner();
        inner.data.version = _version;
        inner.data.to = std::move(_to);
        inner.data.input.assign(_input.begin(), _input.end());
        inner.data.blockLimit = _blockLimit;
        inner.data.chainID = std::move(_chainId);
        inner.data.groupID = std::move(_groupId);
        inner.data.nonce = boost::lexical_cast<std::string>(_nonce);
        inner.importTime = _importTime;

        // Update the hash field
        std::visit(
            [&inner](auto&& hasher) {
                using HasherType = std::decay_t<decltype(hasher)>;
                bcos::concepts::hash::calculate<HasherType>(inner, inner.dataHash);
            },
            m_cryptoSuite->hashImpl()->hasher());

        return transaction;
    }

    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bcos::bytes const& _input, bcos::u256 const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime,
        bcos::crypto::KeyPairInterface::Ptr keyPair) override
    {
        auto tx = createTransaction(_version, std::move(_to), _input, _nonce, _blockLimit,
            std::move(_chainId), std::move(_groupId), _importTime);
        auto sign = m_cryptoSuite->signatureImpl()->sign(*keyPair, tx->hash(), true);

        auto tarsTx = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
        auto& inner = tarsTx->mutableInner();
        inner.signature.assign(sign->begin(), sign->end());

        return tx;
    }

    void setCryptoSuite(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
    {
        m_cryptoSuite = std::move(cryptoSuite);
    }
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};

}  // namespace bcostars::protocol