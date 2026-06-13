/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @brief Factory implementation for RLPTransaction
 * @file RLPTransactionFactory.cpp
 */

#include "RLPTransactionFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

#define RLP_FACTORY_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("RLPTransactionFactory")

using namespace bcos;
using namespace bcos::rlp;

RLPTransactionFactory::RLPTransactionFactory(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
  : m_cryptoSuite(std::move(cryptoSuite))
{}

// --- Empty creation ---

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction()
{
    auto tx = std::make_shared<RLPTransaction>();
    tx->setTainted(true);
    return tx;
}

// --- Copy from existing ---

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(
    bcos::protocol::Transaction& input)
{
    auto* rlpInput = dynamic_cast<RLPTransaction*>(&input);
    if (!rlpInput)
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("RLPTransactionFactory: input is not RLPTransaction"));
    }
    auto tx = std::make_shared<RLPTransaction>(*rlpInput);
    tx->setSynced(input.synced());
    tx->setSealed(input.sealed());
    tx->setInvalid(input.invalid());
    tx->setSystemTx(input.systemTx());
    tx->setBatchId(input.batchId());
    tx->setBatchHash(input.batchHash());
    tx->setTainted(input.tainted());
    tx->setStoreToBackend(input.storeToBackend());
    tx->setSubmitCallback(input.takeSubmitCallback());
    return tx;
}

// --- Create from RLP bytes (primary path) ---

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(
    bcos::bytesConstRef txData, bool checkSig, bool /*checkHash*/, bool tainted)
{
    auto tx = std::make_shared<RLPTransaction>();
    tx->setTainted(tainted);
    tx->decode(txData);

    if (checkSig && !tx->signatureR().empty() && !tx->signatureS().empty())
    {
        // Verify secp256k1 signature
        auto const hashForSign = tx->hashForSign();
        bcos::bytes sigBytes;
        sigBytes.reserve(tx->signatureR().size() + tx->signatureS().size() + 1);
        sigBytes.insert(sigBytes.end(), tx->signatureR().begin(), tx->signatureR().end());
        sigBytes.insert(sigBytes.end(), tx->signatureS().begin(), tx->signatureS().end());
        sigBytes.push_back(static_cast<byte>(tx->signatureV() & 0xFF));

        bcos::crypto::Keccak256 keccak;
        bcos::crypto::Secp256k1Crypto secp256k1;
        auto [recovered, sender] =
            secp256k1.recoverAddress(keccak, hashForSign, bcos::ref(sigBytes));

        if (!recovered)
        {
            RLP_FACTORY_LOG(WARNING)
                << LOG_DESC("RLPTransaction: signature recovery failed")
                << LOG_KV("hashForSign", hashForSign.abridged());
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransaction: recover sender address failed"));
        }

        tx->forceSender(sender);
        tx->setTainted(false);
    }

    return tx;
}

// --- BCOSTransaction-style creation (unsupported) ---

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(int32_t, std::string,
    bcos::bytes const&, std::string const&, int64_t, std::string, std::string, int64_t, std::string,
    std::string, std::string, int64_t, std::string, std::string)
{
    BOOST_THROW_EXCEPTION(std::logic_error(
        "RLPTransactionFactory: BCOSTransaction-style creation not supported for RLP transactions"));
}

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(int32_t, std::string,
    bcos::bytes const&, std::string const&, int64_t, std::string, std::string, int64_t,
    const bcos::crypto::KeyPairInterface&, std::string, std::string, std::string, int64_t,
    std::string, std::string)
{
    BOOST_THROW_EXCEPTION(std::logic_error(
        "RLPTransactionFactory: BCOSTransaction-style creation not supported for RLP transactions"));
}

// --- Decode without verification ---

bcos::protocol::Transaction::Ptr RLPTransactionFactory::decodeTransaction(
    bcos::bytesConstRef txData, bool tainted)
{
    auto tx = std::make_shared<RLPTransaction>();
    tx->setTainted(tainted);
    tx->decode(txData);
    return tx;
}
