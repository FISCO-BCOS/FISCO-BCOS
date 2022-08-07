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
#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/sm2.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
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
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506109ef806100"
        "626000396000f3fe608060405234801561001057600080fd5b50600436106100575760003560e01c80634cf2a6"
        "7a1461005c5780638b053758146101f957806393730bbe146102c8578063eb90f45914610397578063fb34363c"
        "14610466575b600080fd5b6101ac6004803603604081101561007257600080fd5b810190808035906020019064"
        "010000000081111561008f57600080fd5b8201836020820111156100a157600080fd5b80359060200191846001"
        "8302840111640100000000831117156100c357600080fd5b91908080601f016020809104026020016040519081"
        "016040528093929190818152602001838380828437600081840152601f19601f82011690508083019250505050"
        "50505091929192908035906020019064010000000081111561012657600080fd5b820183602082011115610138"
        "57600080fd5b8035906020019184600183028401116401000000008311171561015a57600080fd5b9190808060"
        "1f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f"
        "19601f820116905080830192505050505050509192919290505050610535565b60405180831515151581526020"
        "018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff"
        "1681526020019250505060405180910390f35b6102b26004803603602081101561020f57600080fd5b81019080"
        "8035906020019064010000000081111561022c57600080fd5b82018360208201111561023e57600080fd5b8035"
        "906020019184600183028401116401000000008311171561026057600080fd5b91908080601f01602080910402"
        "6020016040519081016040528093929190818152602001838380828437600081840152601f19601f8201169050"
        "808301925050505050505091929192905050506106cb565b6040518082815260200191505060405180910390f3"
        "5b610381600480360360208110156102de57600080fd5b81019080803590602001906401000000008111156102"
        "fb57600080fd5b82018360208201111561030d57600080fd5b8035906020019184600183028401116401000000"
        "008311171561032f57600080fd5b91908080601f01602080910402602001604051908101604052809392919081"
        "8152602001838380828437600081840152601f19601f8201169050808301925050505050505091929192905050"
        "50610772565b6040518082815260200191505060405180910390f35b610450600480360360208110156103ad57"
        "600080fd5b81019080803590602001906401000000008111156103ca57600080fd5b8201836020820111156103"
        "dc57600080fd5b803590602001918460018302840111640100000000831117156103fe57600080fd5b91908080"
        "601f01602080910402602001604051908101604052809392919081815260200183838082843760008184015260"
        "1f19601f820116905080830192505050505050509192919290505050610783565b604051808281526020019150"
        "5060405180910390f35b61051f6004803603602081101561047c57600080fd5b81019080803590602001906401"
        "0000000081111561049957600080fd5b8201836020820111156104ab57600080fd5b8035906020019184600183"
        "02840111640100000000831117156104cd57600080fd5b91908080601f01602080910402602001604051908101"
        "6040528093929190818152602001838380828437600081840152601f19601f8201169050808301925050505050"
        "5050919291929050505061089e565b6040518082815260200191505060405180910390f35b6000806000809054"
        "906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffff"
        "ffffffffff16634cf2a67a85856040518363ffffffff1660e01b81526004018080602001806020018381038352"
        "85818151815260200191508051906020019080838360005b838110156105ca5780820151818401526020810190"
        "506105af565b50505050905090810190601f1680156105f75780820380516001836020036101000a0319168152"
        "60200191505b50838103825284818151815260200191508051906020019080838360005b838110156106305780"
        "82015181840152602081019050610615565b50505050905090810190601f16801561065d578082038051600183"
        "6020036101000a031916815260200191505b50945050505050604080518083038186803b15801561067b576000"
        "80fd5b505afa15801561068f573d6000803e3d6000fd5b505050506040513d60408110156106a557600080fd5b"
        "810190808051906020019092919080519060200190929190505050915091509250929050565b60006002826040"
        "518082805190602001908083835b60208310610703578051825260208201915060208101905060208303925061"
        "06e0565b6001836020036101000a03801982511681845116808217855250505050505090500191505060206040"
        "5180830381855afa158015610745573d6000803e3d6000fd5b5050506040513d602081101561075a57600080fd"
        "5b81019080805190602001909291905050509050919050565b600081805190602001209050919050565b600080"
        "60009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffff"
        "ffffffffffffffffff1663eb90f459836040518263ffffffff1660e01b81526004018080602001828103825283"
        "818151815260200191508051906020019080838360005b83811015610812578082015181840152602081019050"
        "6107f7565b50505050905090810190601f16801561083f5780820380516001836020036101000a031916815260"
        "200191505b509250505060206040518083038186803b15801561085c57600080fd5b505afa158015610870573d"
        "6000803e3d6000fd5b505050506040513d602081101561088657600080fd5b8101908080519060200190929190"
        "5050509050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff"
        "1673ffffffffffffffffffffffffffffffffffffffff1663fb34363c836040518263ffffffff1660e01b815260"
        "04018080602001828103825283818151815260200191508051906020019080838360005b8381101561092d5780"
        "82015181840152602081019050610912565b50505050905090810190601f16801561095a578082038051600183"
        "6020036101000a031916815260200191505b509250505060206040518083038186803b15801561097757600080"
        "fd5b505afa15801561098b573d6000803e3d6000fd5b505050506040513d60208110156109a157600080fd5b81"
        "01908080519060200190929190505050905091905056fea264697066735822122064b43d3bcceb4167412d6944"
        "ac287ffecbc49a0743011c9dee53f4f0671f58fc64736f6c634300060a0033";
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

BOOST_AUTO_TEST_CASE(testSM2Verify)
{
    deployTest();

    // case Verify success
    h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
    auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
    auto keyFactory = std::make_shared<KeyFactoryImpl>();
    auto secCreated = keyFactory->createKey(fixedSec1.asBytes());

    auto keyPair = std::make_shared<SM2KeyPair>(sec1);
    HashType hash = HashType("82ec580fe6d36ae4f81cae3c73f4a5b3b5a09c943172dc9053c69fd8e18dca1e");
    auto signature = sm2Sign(*keyPair, hash, true);
    h256 mismatchHash = h256("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    // verify the signature
    bytes encodedData = codec->encodeWithSig("sm2Verify(bytes,bytes)", hash.asBytes(), *signature);

    // verify
    {
        nextBlock(2);

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
        bool verifySucc;
        Address accountAddress;
        codec->decode(ref(out), verifySucc, accountAddress);
        std::cout << "== testSM2Verify-normalCase, verifySucc: " << verifySucc << std::endl;
        std::cout << "== testSM2Verify-normalCase, accountAddress: " << accountAddress.hex()
                  << std::endl;
        std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
        BOOST_CHECK(verifySucc == true);
        BOOST_CHECK(accountAddress.hex() == keyPair->address(smHashImpl).hex());
        commitBlock(2);
    }

    // mismatch
    {
        nextBlock(3);

        encodedData =
            codec->encodeWithSig("sm2Verify(bytes,bytes)", mismatchHash.asBytes(), *signature);
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
        bool verifySucc;
        Address accountAddress;
        codec->decode(ref(out), verifySucc, accountAddress);
        std::cout << "== testSM2Verify-mismatchHashCase, verifySucc: " << verifySucc << std::endl;
        std::cout << "== testSM2Verify-mismatchHashCase, accountAddress: " << accountAddress.hex()
                  << std::endl;
        std::cout << "== realAccountAddress:" << keyPair->address(smHashImpl).hex() << std::endl;
        BOOST_CHECK(verifySucc == false);
        BOOST_CHECK(accountAddress.hex() == Address().hex());
        commitBlock(3);
    }
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
