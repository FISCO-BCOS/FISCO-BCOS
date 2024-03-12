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
 * @file TransactionBuilderInterface.h
 * @author: octopus
 * @date 2022-01-13
 */
#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-cpp-sdk/utilities/crypto/Common.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionUtils.h>
#include <bcos-crypto/signature/key/KeyPair.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace cppsdk
{
namespace utilities
{
using CryptoType = crypto::KeyPairType;

class TransactionBuilderInterface
{
public:
    using Ptr = std::shared_ptr<TransactionBuilderInterface>;
    using ConstPtr = std::shared_ptr<const TransactionBuilderInterface>;
    using UniquePtr = std::unique_ptr<TransactionBuilderInterface>;

public:
    virtual ~TransactionBuilderInterface() {}

public:
    /**
     * @brief Create a Transaction Data object
     *
     * @param _groupID
     * @param _chainID
     * @param _to
     * @param _data
     * @param _abi
     * @param _blockLimit
     * @return bcostars::TransactionDataUniquePtr
     */
    virtual bcostars::TransactionDataUniquePtr createTransactionData(const std::string& _groupID,
        const std::string& _chainID, const std::string& _to, const bcos::bytes& _data,
        const std::string& _abi, int64_t _blockLimit) = 0;

    /**
     * @brief Create a Transaction Data object with json string
     *
     * @param _json
     *              version:number
     *              groupID:string
     *              chainID:string
     *              to:string
     *              data:hex string
     *              abi:string
     *              blockLimit:number
     *              nonce:string
     * @return bcostars::TransactionDataUniquePtr
     */
    virtual bcostars::TransactionDataUniquePtr createTransactionDataWithJson(
        const std::string& _json) = 0;

    /**
     * @brief
     *
     * @param _transactionData
     * @return bytesConstPtr
     */
    virtual bytesConstPtr encodeTransactionData(
        const bcostars::TransactionData& _transactionData) = 0;

    /**
     * @brief decode transaction data from encoded bytes
     *
     * @param _txBytes encoded bytes
     * @return transaction data
     */
    virtual bcostars::TransactionDataUniquePtr decodeTransactionData(
        const bcos::bytes& _txBytes) = 0;

    /**
     * @brief decode transaction data from encoded bytes
     *
     * @param _txBytes encoded bytes
     * @return transaction data json string
     */
    virtual std::string decodeTransactionDataToJsonObj(const bcos::bytes& _txBytes) = 0;

    /**
     * @brief
     *
     * @param _cryptoType
     * @param _transactionData
     * @return crypto::HashType
     */
    virtual crypto::HashType calculateTransactionDataHash(
        CryptoType _cryptoType, const bcostars::TransactionData& _transactionData) = 0;

    /**
     * @brief
     *
     * @param _keyPair
     * @param _transactionDataHash
     * @return bcos::bytesConstPtr
     */
    virtual bcos::bytesConstPtr signTransactionDataHash(
        const bcos::crypto::KeyPairInterface& _keyPair,
        const crypto::HashType& _transactionDataHash) = 0;

    /**
     * @brief Create a Transaction object
     *
     * @param _transactionData
     * @param _signData
     * @param _transactionDataHash
     * @param _attribute
     * @param _extraData
     * @return bcostars::TransactionUniquePtr
     */
    virtual bcostars::TransactionUniquePtr createTransaction(
        const bcostars::TransactionData& _transactionData, const bcos::bytes& _signData,
        const crypto::HashType& _transactionDataHash, int32_t _attribute,
        const std::string& _extraData = "") = 0;

    /**
     * @brief Create a Transaction object with json string
     *
     * @param _json
     *              transactionData:bcostars::TransactionData
     *              dataHash:string
     *              signature:string
     *              importTime:number
     *              attribute:number
     *              sender:string
     *              extraData:string
     * @return bcostars::TransactionUniquePtr
     */
    virtual bcostars::TransactionUniquePtr createTransactionWithJson(const std::string& _json) = 0;

    /**
     * @brief
     *
     * @param _transaction
     * @return bytesConstPtr
     */
    virtual bytesConstPtr encodeTransaction(const bcostars::Transaction& _transaction) = 0;

    /**
     * @brief decode transaction from encoded bytes
     *
     * @param _txBytes encoded bytes
     * @return transaction
     */
    virtual bcostars::TransactionUniquePtr decodeTransaction(const bcos::bytes& _txBytes) = 0;

    /**
     * @brief decode transaction data from encoded bytes
     *
     * @param _txBytes encoded bytes
     * @return transaction data json string
     */
    virtual std::string decodeTransactionToJsonObj(const bcos::bytes& _txBytes) = 0;

public:
    /**
     * @brief Create a Signed Transaction object
     *
     * @param _transactionData
     * @param _signData
     * @param _transactionDataHash
     * @param _attribute
     * @param _extraData
     * @return bytesConstPtr
     */
    virtual bytesConstPtr createSignedTransaction(const bcostars::TransactionData& _transactionData,
        const bcos::bytes& _signData, const crypto::HashType& _transactionDataHash,
        int32_t _attribute, const std::string& _extraData = "") = 0;

    /**
     * @brief Create a Signed Transaction object
     *
     * @param _keyPair
     * @param _groupID
     * @param _chainID
     * @param _to
     * @param _data
     * @param _abi
     * @param _blockLimit
     * @param _attribute
     * @param _extraData
     * @return std::pair<std::string, std::string>
     */
    virtual std::pair<std::string, std::string> createSignedTransaction(
        const bcos::crypto::KeyPairInterface& _keyPair, const std::string& _groupID,
        const std::string& _chainID, const std::string& _to, const bcos::bytes& _data,
        const std::string& _abi, int64_t _blockLimit, int32_t _attribute,
        const std::string& _extraData = "") = 0;
};
}  // namespace utilities
}  // namespace cppsdk
}  // namespace bcos