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

#include "RLPTransaction.h"
#include "RLPTransactionFactory.h"
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
    if (rlpInput == nullptr)
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

    if (checkSig)
    {
        // Reject empty signatures when verification is explicitly requested
        if (tx->signatureR().empty() || tx->signatureS().empty())
        {
            BOOST_THROW_EXCEPTION(
                std::invalid_argument("RLPTransactionFactory: checkSig=true but signature empty"));
        }

        // Verify secp256k1 signature using injected crypto suite
        auto const hashForSign = tx->hashForSign();
        auto const sigBytes = tx->signatureData();
        auto [recovered, sender] =
            m_cryptoSuite->signatureImpl()->recoverAddress(
                *m_cryptoSuite->hashImpl(), hashForSign, sigBytes);

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

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(int32_t /*_version*/,
    std::string /*_to*/, bcos::bytes const& /*_input*/, std::string const& /*_nonce*/,
    int64_t /*_blockLimit*/, std::string /*_chainId*/, std::string /*_groupId*/,
    int64_t /*_importTime*/, std::string /*_abi*/, std::string /*_value*/,
    std::string /*_gasPrice*/, int64_t /*_gasLimit*/, std::string /*_maxFeePerGas*/,
    std::string /*_maxPriorityFeePerGas*/)
{
    BOOST_THROW_EXCEPTION(std::logic_error(
        "RLPTransactionFactory: BCOSTransaction-style creation not supported for RLP transactions"));
}

bcos::protocol::Transaction::Ptr RLPTransactionFactory::createTransaction(int32_t /*_version*/,
    std::string /*_to*/, bcos::bytes const& /*_input*/, std::string const& /*_nonce*/,
    int64_t /*_blockLimit*/, std::string /*_chainId*/, std::string /*_groupId*/,
    int64_t /*_importTime*/, const bcos::crypto::KeyPairInterface& /*keyPair*/,
    std::string /*_abi*/, std::string /*_value*/, std::string /*_gasPrice*/,
    int64_t /*_gasLimit*/, std::string /*_maxFeePerGas*/,
    std::string /*_maxPriorityFeePerGas*/)
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
