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
 * @file CryptoPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-07-05
 */
#include "precompiled/CryptoPrecompiled.h"
#include "../mock/MockLedger.h"
#include "bcos-crypto/signature/codec/SignatureDataWithPub.h"
#include "bcos-executor/src/executive/LedgerCache.h"
#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/sm2.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::codec;

namespace bcos::test
{
class CryptoPrecompiledFixture : public PrecompiledFixture
{
public:
    CryptoPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false);
        cryptoAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~CryptoPrecompiledFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(cryptoBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(cryptoAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(1);
        // --------------------------------
        // Create contract
        // --------------------------------

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->dmcExecuteTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), cryptoAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), cryptoAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        // --------------------------------
        // Create contract twice to avoid address used in wasm
        // --------------------------------

        paramsBak.setSeq(1001);
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::make_unique<decltype(paramsBak)>(paramsBak),
            [&](bcos::Error::UniquePtr&& error,
                bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();
        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(
            result2->status(), (int32_t)TransactionStatus::ContractAddressAlreadyUsed);

        BOOST_CHECK_EQUAL(result2->contextID(), 99);
        commitBlock(1);
    }

    std::string sender;
    std::string cryptoAddress;
    std::string cryptoBin =
        "608060405234801561001057600080fd5b5061100a6000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061090a806100"
        "626000396000f3fe608060405234801561001057600080fd5b50600436106100575760003560e01c80638b0537"
        "581461005c57806393730bbe1461012b578063cbdb3a67146101fa578063eb90f45914610306578063fb34363c"
        "146103d5575b600080fd5b6101156004803603602081101561007257600080fd5b810190808035906020019064"
        "010000000081111561008f57600080fd5b8201836020820111156100a157600080fd5b80359060200191846001"
        "8302840111640100000000831117156100c357600080fd5b91908080601f016020809104026020016040519081"
        "016040528093929190818152602001838380828437600081840152601f19601f82011690508083019250505050"
        "50505091929192905050506104a4565b6040518082815260200191505060405180910390f35b6101e460048036"
        "03602081101561014157600080fd5b810190808035906020019064010000000081111561015e57600080fd5b82"
        "018360208201111561017057600080fd5b80359060200191846001830284011164010000000083111715610192"
        "57600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380"
        "828437600081840152601f19601f82011690508083019250505050505050919291929050505061054b565b6040"
        "518082815260200191505060405180910390f35b6102d16004803603608081101561021057600080fd5b810190"
        "80803590602001909291908035906020019064010000000081111561023757600080fd5b820183602082011115"
        "61024957600080fd5b8035906020019184600183028401116401000000008311171561026b57600080fd5b9190"
        "8080601f0160208091040260200160405190810160405280939291908181526020018383808284376000818401"
        "52601f19601f820116905080830192505050505050509192919290803590602001909291908035906020019092"
        "919050505061055c565b6040518083151581526020018273ffffffffffffffffffffffffffffffffffffffff16"
        "81526020019250505060405180910390f35b6103bf6004803603602081101561031c57600080fd5b8101908080"
        "35906020019064010000000081111561033957600080fd5b82018360208201111561034b57600080fd5b803590"
        "6020019184600183028401116401000000008311171561036d57600080fd5b91908080601f0160208091040260"
        "20016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080"
        "83019250505050505050919291929050505061069e565b6040518082815260200191505060405180910390f35b"
        "61048e600480360360208110156103eb57600080fd5b8101908080359060200190640100000000811115610408"
        "57600080fd5b82018360208201111561041a57600080fd5b803590602001918460018302840111640100000000"
        "8311171561043c57600080fd5b91908080601f0160208091040260200160405190810160405280939291908181"
        "52602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050"
        "6107b9565b6040518082815260200191505060405180910390f35b600060028260405180828051906020019080"
        "83835b602083106104dc57805182526020820191506020810190506020830392506104b9565b60018360200361"
        "01000a038019825116818451168082178552505050505050905001915050602060405180830381855afa158015"
        "61051e573d6000803e3d6000fd5b5050506040513d602081101561053357600080fd5b81019080805190602001"
        "909291905050509050919050565b600081805190602001209050919050565b60008060008054906101000a9004"
        "73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663"
        "cbdb3a67878787876040518563ffffffff1660e01b815260040180858152602001806020018481526020018381"
        "52602001828103825285818151815260200191508051906020019080838360005b838110156106005780820151"
        "818401526020810190506105e5565b50505050905090810190601f16801561062d578082038051600183602003"
        "6101000a031916815260200191505b5095505050505050604080518083038186803b15801561064c57600080fd"
        "5b505afa158015610660573d6000803e3d6000fd5b505050506040513d604081101561067657600080fd5b8101"
        "908080519060200190929190805190602001909291905050509150915094509492505050565b60008060009054"
        "906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffff"
        "ffffffffff1663eb90f459836040518263ffffffff1660e01b8152600401808060200182810382528381815181"
        "5260200191508051906020019080838360005b8381101561072d57808201518184015260208101905061071256"
        "5b50505050905090810190601f16801561075a5780820380516001836020036101000a03191681526020019150"
        "5b509250505060206040518083038186803b15801561077757600080fd5b505afa15801561078b573d6000803e"
        "3d6000fd5b505050506040513d60208110156107a157600080fd5b810190808051906020019092919050505090"
        "50919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffff"
        "ffffffffffffffffffffffffffffffffffff1663fb34363c836040518263ffffffff1660e01b81526004018080"
        "602001828103825283818151815260200191508051906020019080838360005b83811015610848578082015181"
        "84015260208101905061082d565b50505050905090810190601f16801561087557808203805160018360200361"
        "01000a031916815260200191505b509250505060206040518083038186803b15801561089257600080fd5b505a"
        "fa1580156108a6573d6000803e3d6000fd5b505050506040513d60208110156108bc57600080fd5b8101908080"
        "519060200190929190505050905091905056fea2646970667358221220f90675a92f4cd30c9fb6c666bc2a0684"
        "1325900bb12be808a30992090e509c9564736f6c634300060c0033";
};

BOOST_FIXTURE_TEST_SUITE(precompiledCryptoTest, CryptoPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testSM3AndKeccak256)
{
    deployTest();

    // sm3
    {
        nextBlock(2);
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("sm3(bytes)", dataRef.toBytes());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        string32 decodedHash;
        codec->decode(bytesConstRef(&out), decodedHash);
        HashType hash =
            HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
        std::cout << "== testHash-sm3: decodedHash: " << codec::fromString32(decodedHash).hex()
                  << std::endl;
        std::cout << "== testHash-sm3: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));

        commitBlock(2);
    }
    // keccak256Hash
    {
        nextBlock(3);
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("keccak256Hash(bytes)", dataRef.toBytes());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        bytes out = result2->data().toBytes();
        string32 decodedHash;
        codec->decode(bytesConstRef(&out), decodedHash);
        HashType hash =
            HashType("48bed44d1bcd124a28c27f343a817e5f5243190d3c52bf347daf876de1dbbf77");
        std::cout << "== testHash-keccak256Hash: decodedHash: "
                  << codec::fromString32(decodedHash).hex() << std::endl;
        std::cout << "== testHash-keccak256Hash: hash:" << hash.hex() << std::endl;
        BOOST_CHECK(hash == codec::fromString32(decodedHash));
        commitBlock(3);
    }
}

class SM2VerifyPrecompiledFixture
{
public:
    SM2VerifyPrecompiledFixture()
    {
        clearName2SelectCache();
        m_cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr);
        m_cryptoPrecompiled = std::make_shared<CryptoPrecompiled>(m_cryptoSuite->hashImpl());
        m_blockContext =
            std::make_shared<BlockContext>(nullptr, m_ledgerCache, m_cryptoSuite->hashImpl(), 0,
                h256(), utcTime(), (uint32_t)(bcos::protocol::BlockVersion::V3_0_VERSION),
                FiscoBcosScheduleV4, false, false);
        std::shared_ptr<wasm::GasInjector> gasInjector = nullptr;
        m_executive = std::make_shared<TransactionExecutive>(
            std::weak_ptr<BlockContext>(m_blockContext), "", 100, 0, gasInjector);
        m_abi = std::make_shared<bcos::codec::abi::ContractABICodec>(m_cryptoSuite->hashImpl());
    }

    ~SM2VerifyPrecompiledFixture() {}
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    BlockContext::Ptr m_blockContext;
    TransactionExecutive::Ptr m_executive;
    CryptoPrecompiled::Ptr m_cryptoPrecompiled;
    std::string m_sm2VerifyFunction = "sm2Verify(bytes32,bytes,bytes32,bytes32)";
    std::shared_ptr<bcos::codec::abi::ContractABICodec> m_abi;
    LedgerCache::Ptr m_ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
};

BOOST_AUTO_TEST_CASE(testSM2Verify)
{
    // case Verify success
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto secCreated = keyFactory->createKey(fixedSec1.asBytes());

    auto keyPair = std::make_shared<SM2KeyPair>(sec1);
    HashType hash = HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    auto signature = sm2Sign(*keyPair, hash, true);
    h256 mismatchHash = h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    SM2VerifyPrecompiledFixture fixture;

    // verify the signature
    auto signatureStruct = std::make_shared<SignatureDataWithPub>(ref(*signature));
    bytes in = fixture.m_abi->abiIn(fixture.m_sm2VerifyFunction, codec::toString32(hash),
        *signatureStruct->pub(), codec::toString32(signatureStruct->r()),
        codec::toString32(signatureStruct->s()));
    auto parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    auto execResult = fixture.m_cryptoPrecompiled->call(fixture.m_executive, parameters);
    auto out = execResult->execResult();
    bool verifySucc;
    Address accountAddress;
    fixture.m_abi->abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    BOOST_CHECK(verifySucc == true);
    BOOST_CHECK(accountAddress.hex() == keyPair->address(smHashImpl).hex());

    // mismatch case
    in = fixture.m_abi->abiIn(fixture.m_sm2VerifyFunction, codec::toString32(mismatchHash),
        *signatureStruct->pub(), codec::toString32(signatureStruct->r()),
        codec::toString32(signatureStruct->s()));
    parameters = std::make_shared<PrecompiledExecResult>();
    parameters->m_input = bytesConstRef(in.data(), in.size());
    execResult = fixture.m_cryptoPrecompiled->call(fixture.m_executive, parameters);
    out = execResult->execResult();
    fixture.m_abi->abiOut(bytesConstRef(&out), verifySucc, accountAddress);
    BOOST_CHECK(verifySucc == false);
    BOOST_CHECK(accountAddress.hex() == Address().hex());
}

BOOST_AUTO_TEST_CASE(testEVMPrecompiled)
{
    deployTest();

    // sha256
    {
        nextBlock(2);
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("getSha256(bytes)", dataRef.toBytes());

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", encodedData, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(cryptoAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(encodedData));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        auto rData = result2->data().toBytes();
        string32 re;
        codec->decode(ref(rData), re);
        BOOST_CHECK_EQUAL("0x88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
            toHexStringWithPrefix(fromString32(re).asBytes()));
        BOOST_CHECK_EQUAL(result2->status(), (int32_t)TransactionStatus::None);
        commitBlock(2);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
