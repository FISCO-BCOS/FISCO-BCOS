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
 * @file TransactionBuilderV1.cpp
 * @author: kyonGuo
 * @date 2023/11/21
 */


#include "TransactionBuilderV1.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>

#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;

bcostars::TransactionDataUniquePtr TransactionBuilderV1::createTransactionData(int32_t _version,
    std::string _groupID, std::string _chainID, std::string _to, std::string _nonce,
    bcos::bytes _input, std::string _abi, int64_t _blockLimit, std::string _value,
    std::string _gasPrice, int64_t _gasLimit, std::string _maxFeePerGas,
    std::string _maxPriorityFeePerGas)
{
    auto txData = std::make_unique<bcostars::TransactionData>();
    txData->version = _version;
    txData->groupID = std::move(_groupID);
    txData->chainID = std::move(_chainID);
    txData->to = std::move(_to);
    txData->nonce = _nonce.empty() ? generateRandomStr() : std::move(_nonce);
    txData->input.insert(txData->input.begin(), _input.begin(), _input.end());
    txData->abi = std::move(_abi);
    txData->blockLimit = _blockLimit;
    txData->value = std::move(_value);
    txData->gasPrice = std::move(_gasPrice);
    txData->gasLimit = _gasLimit;
    txData->maxFeePerGas = std::move(_maxFeePerGas);
    txData->maxPriorityFeePerGas = std::move(_maxPriorityFeePerGas);
    return txData;
}

crypto::HashType TransactionBuilderV1::calculateTransactionDataHash(CryptoType _cryptoType,
    int32_t _version, std::string _groupID, std::string _chainID, std::string _to,
    std::string _nonce, bcos::bytes _input, std::string _abi, int64_t _blockLimit,
    std::string _value, std::string _gasPrice, int64_t _gasLimit, std::string _maxFeePerGas,
    std::string _maxPriorityFeePerGas)
{
    auto txData = createTransactionData(_version, std::move(_groupID), std::move(_chainID),
        std::move(_to), std::move(_nonce), std::move(_input), std::move(_abi), _blockLimit,
        std::move(_value), std::move(_gasPrice), _gasLimit, std::move(_maxFeePerGas),
        std::move(_maxPriorityFeePerGas));
    return calculateTransactionDataHash(_cryptoType, *txData);
}

crypto::HashType TransactionBuilderV1::calculateTransactionDataHashWithJson(
    CryptoType _cryptoType, std::string _jsonData)
{
    auto txData = TarsTransactionDataReadFromJsonString(std::move(_jsonData));
    return calculateTransactionDataHash(_cryptoType, *txData);
}

bcostars::TransactionUniquePtr TransactionBuilderV1::createTransaction(bcos::bytes _signData,
    crypto::HashType _hash, int32_t _attribute, int32_t _version, std::string _groupID,
    std::string _chainID, std::string _to, std::string _nonce, bcos::bytes _input, std::string _abi,
    int64_t _blockLimit, std::string _value, std::string _gasPrice, int64_t _gasLimit,
    std::string _maxFeePerGas, std::string _maxPriorityFeePerGas, std::string _extraData)
{
    auto tx = std::make_unique<bcostars::Transaction>();
    tx->signature.insert(tx->signature.begin(), _signData.begin(), _signData.end());
    tx->dataHash.insert(tx->dataHash.begin(), _hash.begin(), _hash.end());
    tx->attribute = _attribute;
    tx->data.version = _version;
    tx->data.groupID = std::move(_groupID);
    tx->data.chainID = std::move(_chainID);
    tx->data.to = std::move(_to);
    tx->data.nonce = _nonce.empty() ? generateRandomStr() : std::move(_nonce);
    tx->data.input.insert(tx->data.input.begin(), _input.begin(), _input.end());
    tx->data.abi = std::move(_abi);
    tx->data.blockLimit = _blockLimit;
    tx->data.value = std::move(_value);
    tx->data.gasPrice = std::move(_gasPrice);
    tx->data.gasLimit = _gasLimit;
    tx->data.maxFeePerGas = std::move(_maxFeePerGas);
    tx->data.maxPriorityFeePerGas = std::move(_maxPriorityFeePerGas);
    tx->extraData = std::move(_extraData);
    return tx;
}

std::pair<std::string, std::string> TransactionBuilderV1::createSignedTransaction(
    bcos::crypto::KeyPairInterface const& _keyPair, int32_t _attribute, int32_t _version,
    std::string _groupID, std::string _chainID, std::string _to, std::string _nonce,
    bcos::bytes _input, std::string _abi, int64_t _blockLimit, std::string _value,
    std::string _gasPrice, int64_t _gasLimit, std::string _maxFeePerGas,
    std::string _maxPriorityFeePerGas, std::string _extraData)
{
    auto txData = createTransactionData(_version, std::move(_groupID), std::move(_chainID),
        std::move(_to), std::move(_nonce), std::move(_input), std::move(_abi), _blockLimit,
        std::move(_value), std::move(_gasPrice), _gasLimit, std::move(_maxFeePerGas),
        std::move(_maxPriorityFeePerGas));
    auto txDataHash = calculateTransactionDataHash(_keyPair.keyPairType(), *txData);
    auto signData = TransactionBuilder::signTransactionDataHash(_keyPair, txDataHash);

    auto tx = createTransaction(*txData, *signData, txDataHash, _attribute, _extraData);
    auto txBytes = encodeTransaction(*tx);
    return std::make_pair(toHexStringWithPrefix(txDataHash), toHexStringWithPrefix(*txBytes));
}
