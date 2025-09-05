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
 * @file FakeTransaction.h
 * @author: kyonRay
 * @date 2021-05-06
 */
#pragma once
#include "bcos-tars-protocol/bcos-tars-protocol/impl/TarsHashable.h"

#include "bcos-concepts/Hash.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/Common.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
inline auto fakeTransaction(CryptoSuite::Ptr _cryptoSuite, KeyPairInterface::Ptr _keyPair,
    const std::string_view& _to, bytes const& _input, std::string const& _nonce,
    int64_t _blockLimit, std::string const& _chainId, std::string const& _groupId,
    std::string const& _abi = "", const uint _type = 0, const uint64_t value = 0)
{
    bcostars::Transaction transaction;
    transaction.data.to = _to;
    transaction.data.input.assign(_input.begin(), _input.end());
    transaction.data.nonce = boost::lexical_cast<std::string>(_nonce);
    transaction.data.blockLimit = _blockLimit;
    transaction.data.chainID = _chainId;
    transaction.data.groupID = _groupId;
    transaction.data.abi = _abi;
    transaction.data.value = std::to_string(value);
    transaction.type = _type;
    auto pbTransaction = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_transaction = std::move(transaction)]() mutable { return &m_transaction; });
    // set signature
    pbTransaction->calculateHash(*_cryptoSuite->hashImpl());

    auto signData = _cryptoSuite->signatureImpl()->sign(*_keyPair, pbTransaction->hash(), true);
    pbTransaction->setSignatureData(*signData);
    pbTransaction->forceSender(_keyPair->address(_cryptoSuite->hashImpl()).asBytes());
    pbTransaction->calculateHash(*_cryptoSuite->hashImpl());
    return pbTransaction;
}


inline void checkTransaction(auto pbTransaction, auto decodedTransaction)
{
    // check the fields
    BOOST_CHECK(decodedTransaction->hash() == pbTransaction->hash());
    BOOST_CHECK(decodedTransaction->sender() == pbTransaction->sender());
    BOOST_CHECK(decodedTransaction->to() == pbTransaction->to());
    // check the transaction hash fields
    BOOST_CHECK(decodedTransaction->input().toBytes() == pbTransaction->input().toBytes());
    BOOST_CHECK(decodedTransaction->nonce() == pbTransaction->nonce());
    BOOST_CHECK(decodedTransaction->blockLimit() == pbTransaction->blockLimit());
    BOOST_CHECK(decodedTransaction->chainId() == pbTransaction->chainId());
    BOOST_CHECK(decodedTransaction->groupId() == pbTransaction->groupId());
}

inline Transaction::Ptr testTransaction(CryptoSuite::Ptr _cryptoSuite,
    KeyPairInterface::Ptr _keyPair, const std::string_view& _to, bytes const& _input,
    std::string const& _nonce, int64_t _blockLimit, std::string const& _chainId,
    std::string const& _groupId, bool isCheck = true, const uint64_t value = 0)
{
    auto factory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(_cryptoSuite);
    auto pbTransaction = fakeTransaction(
        _cryptoSuite, _keyPair, _to, _input, _nonce, _blockLimit, _chainId, _groupId, "", 0, value);
    auto addr = _keyPair->address(_cryptoSuite->hashImpl());
    if (isCheck)
    {
        BOOST_CHECK(pbTransaction->sender() == std::string_view((char*)addr.data(), 20));
    }
    bcos::bytes encodedData;
    pbTransaction->encode(encodedData);
    // auto encodedDataCache = pbTransaction->encode();
    // BOOST_CHECK(encodedData.toBytes() == encodedDataCache.toBytes());
#if 0
    std::cout << "#### encodedData is:" << *toHexString(encodedData) << std::endl;
    std::cout << "### hash:" << pbTransaction->hash().hex() << std::endl;
    std::cout << "### sender:" << *toHexString(pbTransaction->sender()) << std::endl;
    std::cout << "### type:" << pbTransaction->type() << std::endl;
    std::cout << "### to:" << *toHexString(pbTransaction->to()) << std::endl;
#endif
    // decode
    auto decodedTransaction = factory->createTransaction(bcos::ref(encodedData), true);
    if (isCheck)
    {
        checkTransaction(pbTransaction, decodedTransaction);
    }
    return decodedTransaction;
}

inline Transaction::Ptr fakeTransaction(CryptoSuite::Ptr _cryptoSuite, bool isCheck = true)
{
    bcos::crypto::KeyPairInterface::Ptr keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = *toHexString(keyPair->address(_cryptoSuite->hashImpl()).asBytes());
    std::string inputStr = "testTransaction";
    bytes input = asBytes(inputStr);
    u256 nonce = 120012323;
    int64_t blockLimit = 1000023;
    std::string chainId = "chainId";
    std::string groupId = "groupId";
    return testTransaction(
        _cryptoSuite, keyPair, to, input, nonce.str(), blockLimit, chainId, groupId, isCheck);
}

inline Transaction::Ptr fakeTransaction(CryptoSuite::Ptr _cryptoSuite, const std::string& nonce,
    int64_t blockLimit = 1000023, std::string chainId = "chainId", std::string groupId = "groupId",
    bytes _to = bytes())
{
    KeyPairInterface::Ptr keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = keyPair->address(_cryptoSuite->hashImpl()).asBytes();
    if (_to != bytes())
    {
        to = _to;
    }
    std::string inputStr = "testTransaction";
    bytes input = asBytes(inputStr);
    return fakeTransaction(_cryptoSuite, keyPair, std::string_view((char*)_to.data(), _to.size()),
        input, nonce, blockLimit, chainId, groupId);
}

inline Transaction::Ptr fakeWeb3Tx(CryptoSuite::Ptr _cryptoSuite, std::string nonce,
    crypto::KeyPairInterface::UniquePtr const& key, std::string inputStr = "testTransaction")
{
    bcostars::Transaction transaction;

    transaction.data.to = key->address(_cryptoSuite->hashImpl()).hex();
    transaction.data.input.assign(inputStr.begin(), inputStr.end());
    transaction.data.nonce = std::move(nonce);
    transaction.type = static_cast<tars::Char>(TransactionType::Web3Transaction);
    std::mt19937 random(std::random_device{}());
    std::string extraData = "extraData" + std::to_string(random());
    auto hash = _cryptoSuite->hash(extraData);
    transaction.extraTransactionBytes.assign(extraData.begin(), extraData.end());
    transaction.extraTransactionHash.assign(hash.begin(), hash.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_transaction = std::move(transaction)]() mutable { return &m_transaction; });
    // set signature
    tx->calculateHash(*_cryptoSuite->hashImpl());

    auto signData = _cryptoSuite->signatureImpl()->sign(*key, tx->hash(), true);
    tx->setSignatureData(*signData);
    tx->forceSender(key->address(_cryptoSuite->hashImpl()).asBytes());
    tx->calculateHash(*_cryptoSuite->hashImpl());
    return tx;
}

inline TransactionsPtr fakeTransactions(int _size)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = *toHexString(cryptoSuite->calculateAddress(keyPair->publicKey()).asBytes());

    TransactionsPtr txs = std::make_shared<Transactions>();
    for (int i = 0; i < _size; ++i)
    {
        std::string inputStr = "testTransaction" + std::to_string(i);
        bytes input = asBytes(inputStr);
        u256 nonce = 120012323 + i;
        int64_t blockLimit = 1000 + i;
        std::string chainId = "chainId";
        std::string groupId = "groupId";
        txs->emplace_back(testTransaction(
            cryptoSuite, keyPair, to, input, nonce.str(), blockLimit, chainId, groupId));
    }
    return txs;
}

inline Transaction::Ptr fakeInvalidateTransacton(std::string inputStr, const uint64_t value)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::crypto::KeyPairInterface::Ptr keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = *toHexString(cryptoSuite->calculateAddress(keyPair->publicKey()).asBytes());
    bytes input = asBytes(inputStr);
    u256 nonce = 120012323;
    int64_t blockLimit = 1001;
    std::string chainId = "InvalidateChainId";
    std::string groupId = "InvalidateGroupId";
    return testTransaction(
        cryptoSuite, keyPair, to, input, nonce.str(), blockLimit, chainId, groupId, false, value);
}
}  // namespace test
}  // namespace bcos
