/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 * @brief Factory for RLP-based Ethereum Web3 transactions
 * @file RLPTransactionFactory.h
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/protocol/TransactionFactory.h>

namespace bcos::rlp
{

/// Factory that creates RLPTransaction objects for Web3 (Ethereum-style) transactions.
///
/// Uses Ethereum RLP encoding and secp256k1 signatures exclusively.
/// Does NOT support BCOSTransaction creation paths (createTransaction with individual
/// field parameters); those should use bcostars::protocol::TransactionFactoryImpl.
class RLPTransactionFactory : public bcos::protocol::TransactionFactory
{
public:
    explicit RLPTransactionFactory(bcos::crypto::CryptoSuite::Ptr cryptoSuite);
    ~RLPTransactionFactory() noexcept override = default;

    // --- Creation from nothing ---
    bcos::protocol::Transaction::Ptr createTransaction() override;

    // --- Copy-construct from existing Transaction ---
    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::protocol::Transaction& input) override;

    // --- Create from RLP bytes (primary path for Web3 txs) ---
    bcos::protocol::Transaction::Ptr createTransaction(
        bcos::bytesConstRef txData, bool checkSig = true, bool checkHash = false,
        bool tainted = true) override;

    // --- BCOSTransaction-style creation (NOT supported for RLP) ---
    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bcos::bytes const& _input, std::string const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime,
        std::string _abi = {}, std::string _value = {}, std::string _gasPrice = {},
        int64_t _gasLimit = 0, std::string _maxFeePerGas = {},
        std::string _maxPriorityFeePerGas = {}) override;

    bcos::protocol::Transaction::Ptr createTransaction(int32_t _version, std::string _to,
        bcos::bytes const& _input, std::string const& _nonce, int64_t _blockLimit,
        std::string _chainId, std::string _groupId, int64_t _importTime,
        const bcos::crypto::KeyPairInterface& keyPair, std::string _abi = {},
        std::string _value = {}, std::string _gasPrice = {}, int64_t _gasLimit = 0,
        std::string _maxFeePerGas = {},
        std::string _maxPriorityFeePerGas = {}) override;

    // --- Decode without verification ---
    bcos::protocol::Transaction::Ptr decodeTransaction(
        bcos::bytesConstRef txData, bool tainted = true) override;

    bcos::crypto::CryptoSuite::Ptr cryptoSuite() override { return m_cryptoSuite; }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};

}  // namespace bcos::rlp
