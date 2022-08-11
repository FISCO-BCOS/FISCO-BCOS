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
 */
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/TransactionExecutive.h"
#include "bcos-executor/src/precompiled/CryptoPrecompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>
#include <wedpr-crypto/WedprCrypto.h>

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
namespace bcos::test
{
class VRFPrecompiledFixture
{
public:
    VRFPrecompiledFixture(bool _useSM, uint32_t _blockVersion)
    {
        clearName2SelectCache();
        if (!_useSM)
        {
            m_cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
        }
        else
        {
            m_cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<SM3>(), std::make_shared<SM2Crypto>(), nullptr);
        }
        m_cryptoPrecompiled = std::make_shared<CryptoPrecompiled>(m_cryptoSuite->hashImpl());
        m_blockContext = std::make_shared<BlockContext>(nullptr, m_cryptoSuite->hashImpl(), 0,
            h256(), utcTime(), _blockVersion, FiscoBcosScheduleV4, false, false);
        std::shared_ptr<wasm::GasInjector> gasInjector = nullptr;
        m_executive = std::make_shared<TransactionExecutive>(
            std::weak_ptr<BlockContext>(m_blockContext), "", 100, 0, gasInjector);
        m_abi = std::make_shared<bcos::codec::abi::ContractABICodec>(m_cryptoSuite->hashImpl());
    }

    ~VRFPrecompiledFixture() {}

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    BlockContext::Ptr m_blockContext;
    TransactionExecutive::Ptr m_executive;
    CryptoPrecompiled::Ptr m_cryptoPrecompiled;
    std::string m_vrfVerifyFunction = "curve25519VRFVerify(bytes,bytes,bytes)";
    std::shared_ptr<bcos::codec::abi::ContractABICodec> m_abi;
};

BOOST_FIXTURE_TEST_SUITE(test_VRFPrecompiled, TestPromptFixture)

void testVRFVerify(VRFPrecompiledFixture _fixture)
{
    auto keyPair = _fixture.m_cryptoSuite->signatureImpl()->generateKeyPair();
    // generate vrfProof
    std::string input = "abcd";
    bytes vrfPublicKey;
    vrfPublicKey.resize(32);
    CInputBuffer privateKey{
        (const char*)keyPair->secretKey()->data().data(), keyPair->secretKey()->size()};
    COutputBuffer publicKey{(char*)vrfPublicKey.data(), vrfPublicKey.size()};
    // derive the public key
    std::cout << "try to wedpr_curve25519_vrf_derive_public_key" << std::endl;
    auto ret = wedpr_curve25519_vrf_derive_public_key(&privateKey, &publicKey);
    BOOST_CHECK_EQUAL(ret, WEDPR_SUCCESS);
    std::cout << "try to wedpr_curve25519_vrf_derive_public_key success" << std::endl;

    // generate proof
    bytes inputBytes = bytes(input.begin(), input.end());
    CInputBuffer inputMsg{(const char*)inputBytes.data(), inputBytes.size()};
    bytes vrfProof;
    size_t proofSize = 96;
    vrfProof.resize(proofSize);
    COutputBuffer proof{(char*)vrfProof.data(), proofSize};
    std::cout << "try to wedpr_curve25519_vrf_prove_utf8" << std::endl;
    ret = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
    BOOST_CHECK_EQUAL(ret, WEDPR_SUCCESS);
    std::cout << "try to wedpr_curve25519_vrf_prove_utf8 success" << std::endl;

    // case1: verify success
    u256 lastRandomValue;
    bool verifySucc;
    u256 randomValue;
    std::cout << "### inputBytes: " << *toHexString(inputBytes) << std::endl;
    std::cout << "### vrfPublicKey: " << *toHexString(vrfPublicKey) << std::endl;
    std::cout << "### vrfProof: " << *toHexString(vrfProof) << std::endl;
    for (int i = 0; i < 10; i++)
    {
        bytes in =
            _fixture.m_abi->abiIn(_fixture.m_vrfVerifyFunction, inputBytes, vrfPublicKey, vrfProof);
        auto parameters = std::make_shared<PrecompiledExecResult>();
        parameters->m_input = bytesConstRef(in.data(), in.size());
        auto execResult = _fixture.m_cryptoPrecompiled->call(_fixture.m_executive, parameters);
        auto out = execResult->execResult();

        _fixture.m_abi->abiOut(bytesConstRef(&out), verifySucc, randomValue);
        BOOST_CHECK(verifySucc == true);
        if (i > 0)
        {
            BOOST_CHECK(lastRandomValue == randomValue);
        }
        lastRandomValue = randomValue;
    }

    // case2: mismatch public key
    std::string fakeData = "mismatchVRFPublicKey";
    auto mismatchVRFPublicKey = bytes(fakeData.begin(), fakeData.end());
    auto in = _fixture.m_abi->abiIn(
        _fixture.m_vrfVerifyFunction, inputBytes, mismatchVRFPublicKey, vrfProof);
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    auto execResult = _fixture.m_cryptoPrecompiled->call(_fixture.m_executive, parameters);
    auto out = execResult->execResult();
    _fixture.m_abi->abiOut(bytesConstRef(&out), verifySucc, randomValue);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(randomValue == 0);

    // case3: mismatch input data
    fakeData = "abc^5%@@bc$";
    bytes mismatchInput = bytes(fakeData.begin(), fakeData.end());
    in = _fixture.m_abi->abiIn(_fixture.m_vrfVerifyFunction, mismatchInput, vrfPublicKey, vrfProof);
    parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    execResult = _fixture.m_cryptoPrecompiled->call(_fixture.m_executive, parameters);
    out = execResult->execResult();
    _fixture.m_abi->abiOut(bytesConstRef(&out), verifySucc, randomValue);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(randomValue == 0);
}

BOOST_AUTO_TEST_CASE(testCurve25519VRFVerify)
{
    VRFPrecompiledFixture fixture(false, (uint32_t)(bcos::protocol::Version::V3_0_VERSION));
    testVRFVerify(fixture);
}
BOOST_AUTO_TEST_CASE(testSMCurve25519VRFVerify)
{
    VRFPrecompiledFixture fixture(true, (uint32_t)(bcos::protocol::Version::V3_0_VERSION));
    testVRFVerify(fixture);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test