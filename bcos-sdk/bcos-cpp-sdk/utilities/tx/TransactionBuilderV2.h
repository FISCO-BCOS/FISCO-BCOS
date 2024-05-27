/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file TransactionBuilderV2.h
 * @author: kyonGuo
 * @date 2024/3/5
 */

#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-cpp-sdk/utilities/crypto/Common.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilderV1.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionUtils.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-utilities/Common.h>
#include <memory>
#include <mutex>

namespace bcos::cppsdk::utilities
{
class TransactionBuilderV2 : public TransactionBuilderV1
{
public:
    /**
     * @brief Create a Signed Transaction and encoded with signature data
     * @param _signData signature data of transaction hash
     * @param _hash transaction data hash
     * @param _txData transaction data
     * @param _attribute attribute of the transaction, default is 0
     * @param _extraData extra data of the transaction, default is empty
     * @param checkHash whether check the hash of the transaction data, default is true
     * @param _cryptoType crypto type, default is Secp256K1
     * @return
     */
    virtual bcos::bytes createSignedTransactionWithSign(bcos::bytes _signData,
        crypto::HashType _hash, bcostars::TransactionData const& _txData, int32_t _attribute = 0,
        std::string _extraData = "", bool checkHash = true,
        CryptoType _cryptoType = CryptoType::Secp256K1);

    using TransactionBuilder::createSignedTransaction;
    using TransactionBuilderV1::createSignedTransaction;
    /**
     * @brief Create a Signed Transaction object
     * @param _keyPair keyParer object
     * @param _txData transaction data
     * @param _attribute attribute, default is 0
     * @param _extraData extra data, default is empty
     * @return pair of transaction data hash and signed tx
     */
    virtual std::pair<bcos::bytes, bcos::bytes> createSignedTransaction(
        bcos::crypto::KeyPairInterface const& _keyPair, bcostars::TransactionData const& _txData,
        int32_t _attribute = 0, std::string _extraData = "");
};
}  // namespace bcos::cppsdk::utilities