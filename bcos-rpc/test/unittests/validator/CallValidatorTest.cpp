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
 * @file CallValidatorTest.cpp
 * @author: kyonGuo
 * @date 2023/4/18
 */

#include "bcos-crypto/encrypt/AESCrypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-crypto/signature/sm2/SM2KeyPair.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-rpc/validator/CallValidator.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
namespace bcos::test
{

class ValidatorFixture
{
public:
    ValidatorFixture() = default;
};

BOOST_FIXTURE_TEST_SUITE(testValidator, ValidatorFixture)

BOOST_AUTO_TEST_CASE(verifyTest)
{
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    crypto::Secp256k1KeyPair keyPair(sec1);

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto encryptImpl = std::make_shared<bcos::crypto::AESCrypto>();
    auto address = keyPair.address(hashImpl);
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, encryptImpl);
    CallValidator callValidator;
    bcos::bytes input{};
    // precompiled::SYS_CONFIG_ADDRESS string_view to address
    input.insert(
        input.end(), precompiled::SYS_CONFIG_ADDRESS, precompiled::SYS_CONFIG_ADDRESS + 40);
    std::string data = "bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd";
    input += bcos::fromHex(data);

    auto hash = hashImpl->hash(bcos::ref(input));
    auto sign = signatureImpl->sign(keyPair, hash, true);
    std::string signStr = bcos::toHex(*sign);
    // success
    {
        auto [result, recoverAdd] = callValidator.verify(precompiled::SYS_CONFIG_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::NON_SM_NODE);
        BOOST_CHECK(result);
        BOOST_CHECK(bcos::toHex(recoverAdd) == address.hex());
    }

    // error
    {
        auto [result, recoverAdd] = callValidator.verify(precompiled::CONSENSUS_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::NON_SM_NODE);
        BOOST_CHECK(bcos::toHex(recoverAdd) != address.hex());
    }
}

BOOST_AUTO_TEST_CASE(smVerifyTest)
{
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    crypto::SM2KeyPair keyPair(sec1);

    auto hashImpl = std::make_shared<bcos::crypto::SM3>();
    auto signatureImpl = std::make_shared<bcos::crypto::SM2Crypto>();
    auto address = keyPair.address(hashImpl);
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    CallValidator callValidator;
    bcos::bytes input{};
    // precompiled::SYS_CONFIG_ADDRESS string_view to address
    input.insert(
        input.end(), precompiled::SYS_CONFIG_ADDRESS, precompiled::SYS_CONFIG_ADDRESS + 40);
    std::string data = "bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd";
    input += bcos::fromHex(data);

    auto hash = hashImpl->hash(bcos::ref(input));
    auto sign = signatureImpl->sign(keyPair, hash, true);
    std::string signStr = bcos::toHex(*sign);

    // success
    {
        auto [result, recoverAdd] = callValidator.verify(precompiled::SYS_CONFIG_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::SM_NODE);
        BOOST_CHECK(result);
        BOOST_CHECK(bcos::toHex(recoverAdd) == address.hex());
    }

    // error
    {
        auto [result, recoverAdd] = callValidator.verify(precompiled::CONSENSUS_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::SM_NODE);
        BOOST_CHECK(bcos::toHex(recoverAdd) != address.hex());
    }
}


BOOST_AUTO_TEST_CASE(wasmTest)
{
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    crypto::Secp256k1KeyPair keyPair(sec1);

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto encryptImpl = std::make_shared<bcos::crypto::AESCrypto>();
    auto address = keyPair.address(hashImpl);
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, encryptImpl);
    bcos::bytes input{};
    // precompiled::SYS_CONFIG_ADDRESS string_view to address
    std::string_view name(precompiled::SYS_CONFIG_NAME);
    input.insert(
        input.end(), precompiled::SYS_CONFIG_NAME, precompiled::SYS_CONFIG_NAME + name.size());
    std::string data = "bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd";
    input += bcos::fromHex(data);

    auto hash = hashImpl->hash(bcos::ref(input));
    auto sign = signatureImpl->sign(keyPair, hash, true);
    std::string signStr = bcos::toHex(*sign);
    // success
    {
        auto [result, recoverAdd] = bcos::rpc::CallValidator::verify(precompiled::SYS_CONFIG_NAME,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::NON_SM_NODE);
        BOOST_CHECK(result);
        BOOST_CHECK(bcos::toHex(recoverAdd) == address.hex());
    }

    // error
    {
        auto [result, recoverAdd] = bcos::rpc::CallValidator::verify(precompiled::CONSENSUS_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::NON_SM_NODE);
        BOOST_CHECK(bcos::toHex(recoverAdd) != address.hex());
    }
}


BOOST_AUTO_TEST_CASE(wasmSmTest)
{
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    crypto::SM2KeyPair keyPair(sec1);

    auto hashImpl = std::make_shared<bcos::crypto::SM3>();
    auto signatureImpl = std::make_shared<bcos::crypto::SM2Crypto>();
    auto address = keyPair.address(hashImpl);
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    bcos::bytes input{};
    // precompiled::SYS_CONFIG_ADDRESS string_view to address
    std::string_view name(precompiled::SYS_CONFIG_NAME);
    input.insert(
        input.end(), precompiled::SYS_CONFIG_NAME, precompiled::SYS_CONFIG_NAME + name.size());
    std::string data = "bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd";
    input += bcos::fromHex(data);

    auto hash = hashImpl->hash(bcos::ref(input));
    auto sign = signatureImpl->sign(keyPair, hash, true);
    std::string signStr = bcos::toHex(*sign);
    // success
    {
        auto [result, recoverAdd] = bcos::rpc::CallValidator::verify(precompiled::SYS_CONFIG_NAME,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::SM_NODE);
        BOOST_CHECK(result);
        BOOST_CHECK(bcos::toHex(recoverAdd) == address.hex());
    }

    // error
    {
        auto [result, recoverAdd] = bcos::rpc::CallValidator::verify(precompiled::CONSENSUS_ADDRESS,
            "0xbcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd", signStr,
            group::SM_NODE);
        BOOST_CHECK(bcos::toHex(recoverAdd) != address.hex());
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test