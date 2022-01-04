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
#include "bcos-framework/libprotocol/protobuf/PBTransactionFactory.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/testutils/crypto/HashImpl.h>
#include <bcos-framework/testutils/crypto/SignatureImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
inline PBTransaction::Ptr fakeTransaction(CryptoSuite::Ptr _cryptoSuite,
    KeyPairInterface::Ptr _keyPair, std::string const& _to, bytes const& _input, u256 const& _nonce,
    int64_t const& _blockLimit, std::string const& _chainId, std::string const& _groupId)
{
    auto pbTransaction = std::make_shared<PBTransaction>(
        _cryptoSuite, 1, _to, _input, _nonce, _blockLimit, _chainId, _groupId, utcTime());
    // set signature
    auto signData = _cryptoSuite->signatureImpl()->sign(_keyPair, pbTransaction->hash(), true);
    pbTransaction->updateSignature(bytesConstRef(signData->data(), signData->size()),
        _keyPair->address(_cryptoSuite->hashImpl()).asBytes());
    return pbTransaction;
}

inline Transaction::Ptr testTransaction(CryptoSuite::Ptr _cryptoSuite,
    KeyPairInterface::Ptr _keyPair, std::string const& _to, bytes const& _input, u256 const& _nonce,
    int64_t const& _blockLimit, std::string const& _chainId, std::string const& _groupId)
{
    auto factory = std::make_shared<PBTransactionFactory>(_cryptoSuite);
    auto pbTransaction = fakeTransaction(
        _cryptoSuite, _keyPair, _to, _input, _nonce, _blockLimit, _chainId, _groupId);
    return pbTransaction;
}

inline Transaction::Ptr fakeTransaction(CryptoSuite::Ptr _cryptoSuite)
{
    auto keyPair = _cryptoSuite->signatureImpl()->generateKeyPair();
    auto to = *toHexString(keyPair->address(_cryptoSuite->hashImpl()).asBytes());
    std::string inputStr = "testTransaction";
    bytes input = asBytes(inputStr);
    u256 nonce = 120012323;
    int64_t blockLimit = 1000023;
    std::string chainId = "chainId";
    std::string groupId = "groupId";
    return testTransaction(_cryptoSuite, keyPair, to, input, nonce, blockLimit, chainId, groupId);
}
inline TransactionsPtr fakeTransactions(int _size)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto signatureImpl = std::make_shared<Secp256k1SignatureImpl>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
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
        txs->emplace_back(
            testTransaction(cryptoSuite, keyPair, to, input, nonce, blockLimit, chainId, groupId));
    }
    return txs;
}
}  // namespace test
}  // namespace bcos
