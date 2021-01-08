/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 *
 *  @file test_CryptoPrecompiled.cpp
 *  @author yujiechen
 *  @date 20201202
 */
#include "ffi_vrf.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/SM2Signature.h>
#include <libethcore/ABI.h>
#include <libprecompiled/extension/CryptoPrecompiled.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
using namespace dev::test;
using namespace dev::crypto;
using namespace dev::storage;
using namespace dev::precompiled;
using namespace dev::blockverifier;

namespace test_precompiled
{
class CryptoPrecompiledFixture : public TestOutputHelperFixture
{
public:
    CryptoPrecompiledFixture() { init(); }
    void init()
    {
        precompiledContext = std::make_shared<dev::blockverifier::ExecutiveContext>();
        cryptoPrecompiled = std::make_shared<CryptoPrecompiled>();
        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        cryptoPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~CryptoPrecompiledFixture() {}

    CryptoPrecompiled::Ptr cryptoPrecompiled;
    ExecutiveContext::Ptr precompiledContext;
};

class SMCryptoPrecompiledFixture : public CryptoPrecompiledFixture
{
public:
    SMCryptoPrecompiledFixture() : smCrypto(g_BCOSConfig.SMCrypto())
    {
        g_BCOSConfig.setUseSMCrypto(true);
        if (!smCrypto)
        {
            crypto::initSMCrypto();
        }
        clearName2SelectCache();
        init();
    }
    ~SMCryptoPrecompiledFixture()
    {
        g_BCOSConfig.setUseSMCrypto(smCrypto);
        clearName2SelectCache();
        if (!smCrypto)
        {
            crypto::initCrypto();
        }
    }

    bool smCrypto;
};

void testHash(CryptoPrecompiled::Ptr _cryptoPrecompiled, ExecutiveContext::Ptr _precompiledContext)
{
    ContractABI abi;
    std::string stringData = "abcd";
    bytesConstRef dataRef(stringData);
    bytes encodedData = abi.abiIn("sm3(bytes)", dataRef.toBytes());
    auto callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
    bytes out = callResult->execResult();
    string32 decodedHash;
    abi.abiOut(bytesConstRef(&out), decodedHash);
    h256 hash = h256("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    std::cout << "== testHash-sm3: decodedHash: " << toHex(fromString32(decodedHash)) << std::endl;
    std::cout << "== testHash-sm3: hash:" << toHex(hash) << std::endl;
    BOOST_CHECK(hash == fromString32(decodedHash));

    encodedData = abi.abiIn("keccak256Hash(bytes)", dataRef.toBytes());
    callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), decodedHash);
    hash = h256("48bed44d1bcd124a28c27f343a817e5f5243190d3c52bf347daf876de1dbbf77");
    std::cout << "== testHash-keccak256Hash: decodedHash: " << toHex(fromString32(decodedHash))
              << std::endl;
    std::cout << "== testHash-keccak256Hash: hash:" << toHex(hash) << std::endl;
    BOOST_CHECK(hash == fromString32(decodedHash));
}

void testVRFVerify(
    CryptoPrecompiled::Ptr _cryptoPrecompiled, ExecutiveContext::Ptr _precompiledContext)
{
    // generate vrfProof
    KeyPair keyPair = KeyPair::create();
    std::string vrfPrivateKey = toHex(bytesConstRef(keyPair.secret().data(), 32));
    std::string input = "abcd";
    std::string vrfPublicKey = curve25519_vrf_generate_key_pair(vrfPrivateKey.c_str());
    std::string vrfProof = curve25519_vrf_proof(vrfPrivateKey.c_str(), input.c_str());
    // case1: verify success
    u256 lastRandomValue;
    bool verifySucc;
    ContractABI abi;
    u256 randomValue;
    for (int i = 0; i < 10; i++)
    {
        bytes encodedData =
            abi.abiIn("curve25519VRFVerify(string,string,string)", input, vrfPublicKey, vrfProof);
        auto callResult =
            _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
        bytes out = callResult->execResult();
        abi.abiOut(bytesConstRef(&out), verifySucc, randomValue);
        std::cout << "###testVRFVerify: verifySucc: " << verifySucc
                  << ", randomValue: " << randomValue << std::endl;
        BOOST_CHECK(verifySucc == true);
        if (i > 0)
        {
            BOOST_CHECK(lastRandomValue == randomValue);
        }
        lastRandomValue = randomValue;
    }

    // case2: mismatch public key
    KeyPair mismatchKeyPair = KeyPair::create();
    auto mismatchVRFPrivateKey = toHex(bytesConstRef(mismatchKeyPair.secret().data(), 32));
    std::string mismatchVRFPublicKey =
        curve25519_vrf_generate_key_pair(mismatchVRFPrivateKey.c_str());
    bytes encodedData = abi.abiIn(
        "curve25519VRFVerify(string,string,string)", input, mismatchVRFPublicKey, vrfProof);
    auto callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
    bytes out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, randomValue);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(randomValue == 0);

    mismatchVRFPublicKey = "abc###bc$";
    encodedData = abi.abiIn(
        "curve25519VRFVerify(string,string,string)", input, mismatchVRFPublicKey, vrfProof);
    callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, randomValue);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(randomValue == 0);

    // case3: mismatch input data
    std::string mismatchInput = "abc^5%@@bc$";
    encodedData = abi.abiIn("curve25519VRFVerify(string,string,string)", mismatchInput,
        mismatchVRFPrivateKey, vrfProof);
    callResult = _cryptoPrecompiled->call(_precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, randomValue);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(randomValue == 0);
}
BOOST_FIXTURE_TEST_SUITE(CryptoPrecompiled, CryptoPrecompiledFixture)
BOOST_AUTO_TEST_CASE(testSM3AndKeccak256)
{
    testHash(cryptoPrecompiled, precompiledContext);
}

BOOST_AUTO_TEST_CASE(testCurve25519VRFVerify)
{
    testVRFVerify(cryptoPrecompiled, precompiledContext);
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(SMCryptoPrecompiled, SMCryptoPrecompiledFixture)

BOOST_AUTO_TEST_CASE(SMtestSM3AndKeccak256)
{
    testHash(cryptoPrecompiled, precompiledContext);
}

BOOST_AUTO_TEST_CASE(testSM2Verify)
{
    // case Verify success
    KeyPair keyPair = KeyPair::create();
    h256 hash = h256("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    auto signature = sm2Sign(keyPair, hash);
    auto sm2Signature = std::dynamic_pointer_cast<SM2Signature>(signature);
    ContractABI abi;
    bytes encodedData = abi.abiIn("sm2Verify(bytes32,bytes,bytes32,bytes32)", toString32(hash),
        sm2Signature->v.asBytes(), toString32(sm2Signature->r), toString32(sm2Signature->s));
    auto callResult = cryptoPrecompiled->call(precompiledContext, bytesConstRef(&encodedData));
    bytes out = callResult->execResult();

    bool verifySucc;
    Address accountAddress;
    abi.abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-normalCase, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-normalCase, accountAddress: " << toHex(accountAddress)
              << std::endl;
    std::cout << "== realAccountAddress:" << toHex(toAddress(keyPair.pub())) << std::endl;
    BOOST_CHECK(verifySucc == true);
    BOOST_CHECK(toHex(accountAddress) == toHex(toAddress(keyPair.pub())));

    // case wrong public key
    KeyPair mismatchKeyPair = KeyPair::create();
    encodedData = abi.abiIn("sm2Verify(bytes32,bytes,bytes32,bytes32)", toString32(hash),
        mismatchKeyPair.pub().asBytes(), toString32(sm2Signature->r), toString32(sm2Signature->s));
    callResult = cryptoPrecompiled->call(precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-mismatchPublicKeyCase, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-mismatchPublicKeyCase, accountAddress: " << toHex(accountAddress)
              << std::endl;
    std::cout << "== realAccountAddress:" << toHex(toAddress(keyPair.pub())) << std::endl;
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(toHex(accountAddress) == toHex(Address()));

    std::string mismatchPublicKeyStr = "abc!#";
    bytesConstRef mismatchPublicKey(mismatchPublicKeyStr);
    encodedData = abi.abiIn("sm2Verify(bytes32,bytes,bytes32,bytes32)", toString32(hash),
        mismatchPublicKey.toBytes(), toString32(sm2Signature->r), toString32(sm2Signature->s));
    callResult = cryptoPrecompiled->call(precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-mismatchPublicKeyCase2, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-mismatchPublicKeyCase2, accountAddress: "
              << toHex(accountAddress) << std::endl;
    std::cout << "== realAccountAddress:" << toHex(toAddress(keyPair.pub())) << std::endl;
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(toHex(accountAddress) == toHex(Address()));


    // case mismatch message
    h256 mismatchHash = h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    encodedData = abi.abiIn("sm2Verify(bytes32,bytes,bytes32,bytes32)", toString32(mismatchHash),
        keyPair.pub().asBytes(), toString32(sm2Signature->r), toString32(sm2Signature->s));
    callResult = cryptoPrecompiled->call(precompiledContext, bytesConstRef(&encodedData));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    std::cout << "== testSM2Verify-mismatchHashCase, verifySucc: " << verifySucc << std::endl;
    std::cout << "== testSM2Verify-mismatchHashCase, accountAddress: " << toHex(accountAddress)
              << std::endl;
    std::cout << "== realAccountAddress:" << toHex(toAddress(keyPair.pub())) << std::endl;
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(toHex(accountAddress) == toHex(Address()));
}

BOOST_AUTO_TEST_CASE(SMtestCurve25519VRFVerify)
{
    testVRFVerify(cryptoPrecompiled, precompiledContext);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test_precompiled