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
 * @file TransactionBuilder.cpp
 * @author: octopus
 * @date 2022-01-13
 */
#include <bcos-concepts/Hash.h>
#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2Crypto.h>
#include <bcos-tars-protocol/impl/TarsHashable.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <time.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;

/**
 * @brief
 *
 * @param _groupID
 * @param _chainID
 * @param _to
 * @param _data
 * @param _abi
 * @param _blockLimit
 * @return bcostars::TransactionDataUniquePtr
 */
bcostars::TransactionDataUniquePtr TransactionBuilder::createTransactionData(
    const std::string& _groupID, const std::string& _chainID, const std::string& _to,
    const bcos::bytes& _data, const std::string& _abi, int64_t _blockLimit)
{
    auto _transactionData = std::make_unique<bcostars::TransactionData>();
    _transactionData->version = 0;
    _transactionData->chainID = _chainID;
    _transactionData->groupID = _groupID;
    _transactionData->to = _to;
    _transactionData->blockLimit = _blockLimit;
    _transactionData->nonce = generateRandomStr();

    _transactionData->abi = _abi;
    _transactionData->input.insert(_transactionData->input.begin(), _data.begin(), _data.end());

    return _transactionData;
}

bcostars::TransactionDataUniquePtr TransactionBuilder::createTransactionDataWithJson(
    const std::string& _json)
{
    auto _transactionData = TarsTransactionDataReadFromJsonString(_json);
    return _transactionData;
}

/**
 * @brief
 *
 * @param _transactionData
 * @return bytesConstPtr
 */
bytesConstPtr TransactionBuilder::encodeTransactionData(
    const bcostars::TransactionData& _transactionData)
{
    tars::TarsOutputStream<tars::BufferWriter> output;
    _transactionData.writeTo(output);

    auto buffer = std::make_shared<bcos::bytes>();
    buffer->assign(output.getBuffer(), output.getBuffer() + output.getLength());
    return buffer;
}

bcostars::TransactionDataUniquePtr TransactionBuilder::decodeTransactionData(
    const bcos::bytes& _txBytes)
{
    tars::TarsInputStream<tars::BufferReader> inputStream;
    inputStream.setBuffer((const char*)_txBytes.data(), _txBytes.size());
    auto txData = std::make_unique<bcostars::TransactionData>();
    txData->readFrom(inputStream);
    return txData;
}

std::string TransactionBuilder::decodeTransactionDataToJsonObj(const bcos::bytes& _txBytes)
{
    auto txData = decodeTransactionData(_txBytes);
    auto txDataJson = TarsTransactionDataWriteToJsonString(*txData);
    return txDataJson;
}

/**
 * @brief
 *
 * @param _cryptoType
 * @param _transactionData
 * @return crypto::HashType
 */
crypto::HashType TransactionBuilder::calculateTransactionDataHash(
    CryptoType _cryptoType, const bcostars::TransactionData& _transactionData)
{
    bcos::crypto::CryptoSuite* cryptoSuite = nullptr;
    if (_cryptoType == bcos::crypto::KeyPairType::SM2 ||
        _cryptoType == bcos::crypto::KeyPairType::HsmSM2)
    {
        cryptoSuite = &*m_smCryptoSuite;
    }
    else
    {
        cryptoSuite = &*m_ecdsaCryptoSuite;
    }
    crypto::HashType txDataHash;
    bcos::concepts::hash::calculate(
        _transactionData, cryptoSuite->hashImpl()->hasher(), txDataHash);
    return txDataHash;
}

/**
 * @brief
 *
 * @param _keyPair
 * @param _transactionDataHash
 * @return bcos::bytesConstPtr
 */
bcos::bytesConstPtr TransactionBuilder::signTransactionDataHash(
    const bcos::crypto::KeyPairInterface& _keyPair, const crypto::HashType& _transactionDataHash)
{
    bcos::crypto::CryptoSuite* cryptoSuite = nullptr;
    if (_keyPair.keyPairType() == bcos::crypto::KeyPairType::SM2)
    {
        cryptoSuite = &*m_smCryptoSuite;
    }
    else if (_keyPair.keyPairType() == bcos::crypto::KeyPairType::HsmSM2)
    {
        if (!m_hsmCryptoSuite)
        {
            std::lock_guard<std::mutex> l(x_hsmCryptoSuite);
            if (!m_hsmCryptoSuite)
            {
                m_hsmCryptoSuite = std::make_unique<bcos::crypto::CryptoSuite>(
                    std::make_shared<bcos::crypto::SM3>(),
                    std::make_shared<bcos::crypto::HsmSM2Crypto>(
                        dynamic_cast<const bcos::crypto::HsmSM2KeyPair&>(_keyPair).hsmLibPath()),
                    nullptr);
            }
        }
        cryptoSuite = &*m_hsmCryptoSuite;
    }
    else
    {
        cryptoSuite = &*m_ecdsaCryptoSuite;
    }

    //  sign transaction data hash
    auto signData = cryptoSuite->signatureImpl()->sign(_keyPair, _transactionDataHash, true);
    return signData;
}

/**
 * @brief
 *
 * @param _transactionData
 * @param _signData
 * @param _hash
 * @param _attribute
 * @param _extraData
 * @return bcostars::TransactionUniquePtr
 */
bcostars::TransactionUniquePtr TransactionBuilder::createTransaction(
    const bcostars::TransactionData& _transactionData, const bcos::bytes& _signData,
    const crypto::HashType& _hash, int32_t _attribute, const std::string& _extraData)
{
    auto transaction = std::make_unique<bcostars::Transaction>();
    transaction->data = _transactionData;
    transaction->dataHash.insert(transaction->dataHash.begin(), _hash.begin(), _hash.end());
    transaction->signature.insert(
        transaction->signature.begin(), _signData.begin(), _signData.end());
    transaction->importTime = 0;
    transaction->attribute = _attribute;
    transaction->extraData = _extraData;
    return transaction;
}

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
bcostars::TransactionUniquePtr TransactionBuilder::createTransactionWithJson(
    const std::string& _json)
{
    auto _transaction = TarsTransactionReadFromJsonString(_json);
    return _transaction;
}

/**
 * @brief
 *
 * @param _transactionData
 * @return bytesConstPtr
 */
bytesConstPtr TransactionBuilder::encodeTransaction(const bcostars::Transaction& _transaction)
{
    tars::TarsOutputStream<tars::BufferWriter> output;
    _transaction.writeTo(output);

    auto buffer = std::make_shared<bcos::bytes>();
    buffer->assign(output.getBuffer(), output.getBuffer() + output.getLength());
    return buffer;
}

bcostars::TransactionUniquePtr TransactionBuilder::decodeTransaction(const bcos::bytes& _txBytes)
{
    tars::TarsInputStream<tars::BufferReader> inputStream;
    inputStream.setBuffer((const char*)_txBytes.data(), _txBytes.size());
    auto tx = std::make_unique<bcostars::Transaction>();
    tx->readFrom(inputStream);
    return tx;
}

std::string TransactionBuilder::decodeTransactionToJsonObj(const bcos::bytes& _txBytes)
{
    auto tx = decodeTransaction(_txBytes);
    return TarsTransactionWriteToJsonString(tx);
}

/**
 * @brief
 *
 * @param _transactionData
 * @param _signData
 * @param _transactionDataHash
 * @param _attribute
 * @param _extraData
 * @return bytesConstPtr
 */
bytesConstPtr TransactionBuilder::createSignedTransaction(
    const bcostars::TransactionData& _transactionData, const bcos::bytes& _signData,
    const crypto::HashType& _transactionDataHash, int32_t _attribute, const std::string& _extraData)
{
    auto transaction = createTransaction(
        _transactionData, _signData, _transactionDataHash, _attribute, _extraData);
    return encodeTransaction(*transaction);
}

/**
 * @brief
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
std::pair<std::string, std::string> TransactionBuilder::createSignedTransaction(
    const bcos::crypto::KeyPairInterface& _keyPair, const std::string& _groupID,
    const std::string& _chainID, const std::string& _to, const bcos::bytes& _data,
    const std::string& _abi, int64_t _blockLimit, int32_t _attribute, const std::string& _extraData)
{
    auto transactionData = createTransactionData(_groupID, _chainID, _to, _data, _abi, _blockLimit);
    auto transactionDataHash =
        calculateTransactionDataHash(_keyPair.keyPairType(), *transactionData);
    auto signData = signTransactionDataHash(_keyPair, transactionDataHash);
    auto transaction =
        createTransaction(*transactionData, *signData, transactionDataHash, _attribute, _extraData);
    auto encodedTx = encodeTransaction(*transaction);

    return std::make_pair<std::string, std::string>(
        toHexStringWithPrefix(transactionDataHash), toHexStringWithPrefix(*encodedTx));
}

std::string TransactionBuilder::generateRandomStr()
{
    static thread_local std::mt19937 generator(std::random_device{}());
    /*
    static thread_local auto timeTicks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                             .count();
    static thread_local std::mt19937 generator(timeTicks);
    */

    std::uniform_int_distribution<int> dis(0, 255);
    std::array<bcos::byte, 16> randomFixedBytes;
    for (auto& element : randomFixedBytes)
    {
        element = dis(generator);
    }

    uint64_t* firstNum = (uint64_t*)randomFixedBytes.data();
    uint64_t* lastNum = (uint64_t*)(randomFixedBytes.data() + sizeof(uint64_t));

    return std::to_string(*firstNum) + std::to_string(*lastNum);
}

u256 TransactionBuilder::genRandomUint256()
{
    static thread_local std::mt19937 generator(std::random_device{}());
    /*
    static thread_local auto timeTicks = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                             .count();
    static thread_local std::mt19937 generator(timeTicks);
    */

    std::uniform_int_distribution<int> dis(0, 255);
    std::array<bcos::byte, 32> random256;
    for (auto& element : random256)
    {
        element = dis(generator);
    }

    return u256(bcos::toHexStringWithPrefix(bcos::bytesRef(random256.data(), random256.size())));
}