/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file TransactionBuilderV1.h
 * @author: kyonGuo
 * @date 2023/11/9
 */

#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
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
class TransactionBuilderV1 : public TransactionBuilder
{
public:
    /**
     * @brief Create a Transaction Data object with full params
     *
     * @param _version tx version, if version==1, then enable
     * (value,gasPrice,gasLimit,maxFeePerGas,maxPriorityFeePerGas) fields
     * @param _groupID group id
     * @param _chainID  chain id
     * @param _to   contract address, if empty, then create contract
     * @param _nonce nonce, random number to avoid duplicate transactions
     * @param _data encoded contract method and params
     * @param _abi  contract abi, only create contract need
     * @param _blockLimit block limit
     * @param _value transfer value
     * @param _gasPrice gas price
     * @param _gasLimit gas limit
     * @param _maxFeePerGas max fee per gas
     * @param _maxPriorityFeePerGas max priority fee per gas
     * @throw Exception if lack of some required fields, or some fields are invalid
     * @return bcostars::TransactionDataUniquePtr
     */
    using TransactionBuilder::createTransactionData;
    virtual bcostars::TransactionDataUniquePtr createTransactionData(int32_t _version,
        std::string _groupID, std::string _chainID, std::string _to, std::string _nonce,
        bcos::bytes _input, std::string _abi, int64_t _blockLimit, std::string _value = "",
        std::string _gasPrice = "", int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string _maxPriorityFeePerGas = "");

    /**
     * @brief calculate TransactionData hash with full fields
     *
     * @param _cryptoType 0: keccak256, 1: sm3
     * @param _version tx version, if version==1, then enable
     * (value,gasPrice,gasLimit,maxFeePerGas,maxPriorityFeePerGas) fields
     * @param _groupID group id
     * @param _chainID  chain id
     * @param _to   contract address, if empty, then create contract
     * @param _nonce nonce, random number to avoid duplicate transactions
     * @param _data encoded contract method and params
     * @param _abi  contract abi, only create contract need
     * @param _blockLimit block limit
     * @param _value transfer value
     * @param _gasPrice gas price
     * @param _gasLimit gas limit
     * @param _maxFeePerGas max fee per gas
     * @param _maxPriorityFeePerGas max priority fee per gas
     * @return crypto::HashType
     */
    using TransactionBuilder::calculateTransactionDataHash;
    virtual crypto::HashType calculateTransactionDataHash(CryptoType _cryptoType, int32_t _version,
        std::string _groupID, std::string _chainID, std::string _to, std::string _nonce,
        bcos::bytes _input, std::string _abi, int64_t _blockLimit, std::string _value = "",
        std::string _gasPrice = "", int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string _maxPriorityFeePerGas = "");


    /**
     * @brief calculate TransactionData hash with jsonData
     *
     * @param _cryptoType 0: keccak256, 1: sm3
     * @param _json
     *              version:number
     *              groupID:string
     *              chainID:string
     *              to:string
     *              input:hex string
     *              abi:string
     *              blockLimit:number
     *              nonce:string
     *              value:string
     *              gasPrice:string
     *              gasLimit:number
     *              maxFeePerGas:string
     *              maxPriorityFeePerGas:string
     * @throw Exception if lack of some required fields
     * @return HashType
     */
    virtual crypto::HashType calculateTransactionDataHashWithJson(
        CryptoType _cryptoType, std::string _jsonData);

    /**
     * @brief Create a Transaction object with full fields
     *
     * @param _transactionData
     * @param _signData
     * @param _hash
     * @param _attribute
     * @param _extraData
     * @param _version tx version, if version==1, then enable
     * (value,gasPrice,gasLimit,maxFeePerGas,maxPriorityFeePerGas) fields
     * @param _groupID group id
     * @param _chainID  chain id
     * @param _to   contract address, if empty, then create contract
     * @param _nonce nonce, random number to avoid duplicate transactions
     * @param _data encoded contract method and params
     * @param _abi  contract abi, only create contract need
     * @param _blockLimit block limit
     * @param _value transfer value
     * @param _gasPrice gas price
     * @param _gasLimit gas limit
     * @param _maxFeePerGas max fee per gas
     * @param _maxPriorityFeePerGas max priority fee per gas
     * @return bcostars::TransactionUniquePtr
     */
    using TransactionBuilder::createTransaction;
    virtual bcostars::TransactionUniquePtr createTransaction(bcos::bytes _signData,
        crypto::HashType _hash, int32_t _attribute, int32_t _version, std::string _groupID,
        std::string _chainID, std::string _to, std::string _nonce, bcos::bytes _input,
        std::string _abi, int64_t _blockLimit, std::string _value = "", std::string _gasPrice = "",
        int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string _maxPriorityFeePerGas = "", std::string _extraData = "");

    /**
     * @brief Create a Signed Transaction object
     *
     * @param _keyPair key pair
     * @param _groupID group id
     * @param _chainID chain id
     * @param _to   contract address, if empty, then create contract
     * @param _data encoded contract method and params
     * @param _abi  contract abi, only create contract need
     * @param _blockLimit   block limit
     * @param _attribute    transaction attribute
     * @param _extraData    extra data
     * @param _version  tx version, if version==1, then enable
     * (value,gasPrice,gasLimit,maxFeePerGas,maxPriorityFeePerGas) fields
     * @param _value    transfer value
     * @param _gasPrice    gas price
     * @param _gasLimit    gas limit
     * @param _maxFeePerGas    max fee per gas
     * @param _maxPriorityFeePerGas    max priority fee per gas
     * @param _nonce    nonce, random number to avoid duplicate transactions
     * @throw Exception if lack of some required fields
     * @return std::pair<std::string, std::string>
     */
    using TransactionBuilder::createSignedTransaction;
    virtual std::pair<std::string, std::string> createSignedTransaction(
        bcos::crypto::KeyPairInterface const& _keyPair, int32_t _attribute, int32_t _version,
        std::string _groupID, std::string _chainID, std::string _to, std::string _nonce,
        bcos::bytes _input, std::string _abi, int64_t _blockLimit, std::string _value = "",
        std::string _gasPrice = "", int64_t _gasLimit = 0, std::string _maxFeePerGas = "",
        std::string _maxPriorityFeePerGas = "", std::string _extraData = "");
};
}  // namespace bcos::cppsdk::utilities