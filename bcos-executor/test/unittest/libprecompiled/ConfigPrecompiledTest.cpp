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
 * @file ConfigPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-22
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/ConsensusPrecompiled.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class ConfigPrecompiledFixture : public PrecompiledFixture
{
public:
    ConfigPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        consTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
        sysTestAddress = Address("0x420f853b41234bd3e9466c85a4cc3428c960dde2").hex();
        paraTestAddress = Address("0x420f853b49838bd3e9412385a4cc3428c960dde2").hex();
    }

    virtual ~ConfigPrecompiledFixture() {}
    void deployTest(std::string _bin, std::string _address)
    {
        bytes input;
        boost::algorithm::unhex(_bin, std::back_inserter(input));
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
        params->setTo(_address);
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
        executor->executeTransaction(
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
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), _address);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), _address);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        commitBlock(1);
    }

    std::string consTestBin =
        "608060405234801561001057600080fd5b506110036000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550610791806100"
        "626000396000f300608060405260043610610062576000357c0100000000000000000000000000000000000000"
        "000000000000000000900463ffffffff1680631216492d1461006757806325df91e6146100ee578063c0160bb5"
        "1461016b578063edd56950146101f2575b600080fd5b34801561007357600080fd5b506100d860048036038101"
        "9080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291"
        "9081815260200183838082843782019150505050505091929192908035906020019092919050505061026f565b"
        "6040518082815260200191505060405180910390f35b3480156100fa57600080fd5b5061015560048036038101"
        "9080803590602001908201803590602001908080601f0160208091040260200160405190810160405280939291"
        "9081815260200183838082843782019150505050505091929192905050506103b1565b60405180828152602001"
        "91505060405180910390f35b34801561017757600080fd5b506101dc6004803603810190808035906020019082"
        "01803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380"
        "8284378201915050505050509192919290803590602001909291905050506104ea565b60405180828152602001"
        "91505060405180910390f35b3480156101fe57600080fd5b506102596004803603810190808035906020019082"
        "01803590602001908080601f016020809104026020016040519081016040528093929190818152602001838380"
        "828437820191505050505050919291929050505061062c565b6040518082815260200191505060405180910390"
        "f35b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffff"
        "ffffffffffffffffffffffffffff16633591685684846040518363ffffffff167c010000000000000000000000"
        "000000000000000000000000000000000002815260040180806020018381526020018281038252848181518152"
        "60200191508051906020019080838360005b83811015610321578082015181840152602081019050610306565b"
        "50505050905090810190601f16801561034e5780820380516001836020036101000a031916815260200191505b"
        "509350505050602060405180830381600087803b15801561036e57600080fd5b505af1158015610382573d6000"
        "803e3d6000fd5b505050506040513d602081101561039857600080fd5b81019080805190602001909291905050"
        "50905092915050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16"
        "73ffffffffffffffffffffffffffffffffffffffff16632800efc0836040518263ffffffff167c010000000000"
        "000000000000000000000000000000000000000000000002815260040180806020018281038252838181518152"
        "60200191508051906020019080838360005b8381101561045c578082015181840152602081019050610441565b"
        "50505050905090810190601f1680156104895780820380516001836020036101000a031916815260200191505b"
        "5092505050602060405180830381600087803b1580156104a857600080fd5b505af11580156104bc573d600080"
        "3e3d6000fd5b505050506040513d60208110156104d257600080fd5b8101908080519060200190929190505050"
        "9050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ff"
        "ffffffffffffffffffffffffffffffffffffff1663ce6fa5c584846040518363ffffffff167c01000000000000"
        "000000000000000000000000000000000000000000000281526004018080602001838152602001828103825284"
        "818151815260200191508051906020019080838360005b8381101561059c578082015181840152602081019050"
        "610581565b50505050905090810190601f1680156105c95780820380516001836020036101000a031916815260"
        "200191505b509350505050602060405180830381600087803b1580156105e957600080fd5b505af11580156105"
        "fd573d6000803e3d6000fd5b505050506040513d602081101561061357600080fd5b8101908080519060200190"
        "929190505050905092915050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffff"
        "ffffffff1673ffffffffffffffffffffffffffffffffffffffff166380599e4b836040518263ffffffff167c01"
        "000000000000000000000000000000000000000000000000000000000281526004018080602001828103825283"
        "818151815260200191508051906020019080838360005b838110156106d7578082015181840152602081019050"
        "6106bc565b50505050905090810190601f1680156107045780820380516001836020036101000a031916815260"
        "200191505b5092505050602060405180830381600087803b15801561072357600080fd5b505af1158015610737"
        "573d6000803e3d6000fd5b505050506040513d602081101561074d57600080fd5b810190808051906020019092"
        "919050505090509190505600a165627a7a72305820508188017072d790559725e832a3ef9e1a851ab727e796b1"
        "c1341248c03342440029";
    std::string sysTestBin =
        "608060405234801561001057600080fd5b506110006000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550610570806100"
        "626000396000f30060806040526004361061004c576000357c0100000000000000000000000000000000000000"
        "000000000000000000900463ffffffff1680632d7fd05314610051578063ec9786001461013a575b600080fd5b"
        "34801561005d57600080fd5b506100b8600480360381019080803590602001908201803590602001908080601f"
        "016020809104026020016040519081016040528093929190818152602001838380828437820191505050505050"
        "91929192905050506101fd565b6040518080602001838152602001828103825284818151815260200191508051"
        "906020019080838360005b838110156100fe5780820151818401526020810190506100e3565b50505050905090"
        "810190601f16801561012b5780820380516001836020036101000a031916815260200191505b50935050505060"
        "405180910390f35b34801561014657600080fd5b506101e7600480360381019080803590602001908201803590"
        "602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782"
        "01915050505050509192919290803590602001908201803590602001908080601f016020809104026020016040"
        "519081016040528093929190818152602001838380828437820191505050505050919291929050505061039d56"
        "5b6040518082815260200191505060405180910390f35b606060008060009054906101000a900473ffffffffff"
        "ffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16631258a93a8460"
        "40518263ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401"
        "8080602001828103825283818151815260200191508051906020019080838360005b838110156102aa57808201"
        "518184015260208101905061028f565b50505050905090810190601f1680156102d75780820380516001836020"
        "036101000a031916815260200191505b5092505050600060405180830381600087803b1580156102f657600080"
        "fd5b505af115801561030a573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f8201168201"
        "8060405250604081101561033457600080fd5b81019080805164010000000081111561034c57600080fd5b8281"
        "019050602081018481111561036257600080fd5b815185600182028301116401000000008211171561037f5760"
        "0080fd5b50509291906020018051906020019092919050505091509150915091565b6000806000905490610100"
        "0a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffff"
        "ff1663bd291aef84846040518363ffffffff167c01000000000000000000000000000000000000000000000000"
        "000000000281526004018080602001806020018381038352858181518152602001915080519060200190808383"
        "60005b8381101561044d578082015181840152602081019050610432565b50505050905090810190601f168015"
        "61047a5780820380516001836020036101000a031916815260200191505b508381038252848181518152602001"
        "91508051906020019080838360005b838110156104b3578082015181840152602081019050610498565b505050"
        "50905090810190601f1680156104e05780820380516001836020036101000a031916815260200191505b509450"
        "50505050602060405180830381600087803b15801561050157600080fd5b505af1158015610515573d6000803e"
        "3d6000fd5b505050506040513d602081101561052b57600080fd5b810190808051906020019092919050505090"
        "50929150505600a165627a7a72305820ba33b892d2d03143a7553d42fcb62b0b9067012df1de5ab763424bea55"
        "85175e0029";
    std::string paraTestBin =
        "608060405234801561001057600080fd5b506110066000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506104a6806100"
        "626000396000f30060806040526004361061004c576000357c0100000000000000000000000000000000000000"
        "000000000000000000900463ffffffff1680630553904e1461005157806311e3f2af146100f8575b600080fd5b"
        "34801561005d57600080fd5b506100e2600480360381019080803573ffffffffffffffffffffffffffffffffff"
        "ffffff169060200190929190803590602001908201803590602001908080601f01602080910402602001604051"
        "908101604052809392919081815260200183838082843782019150505050505091929192908035906020019092"
        "9190505050610195565b6040518082815260200191505060405180910390f35b34801561010457600080fd5b50"
        "61017f600480360381019080803573ffffffffffffffffffffffffffffffffffffffff16906020019092919080"
        "3590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181"
        "52602001838380828437820191505050505050919291929050505061030c565b60405180828152602001915050"
        "60405180910390f35b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16"
        "73ffffffffffffffffffffffffffffffffffffffff16630553904e8585856040518463ffffffff167c01000000"
        "00000000000000000000000000000000000000000000000000028152600401808473ffffffffffffffffffffff"
        "ffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018060200183815260"
        "2001828103825284818151815260200191508051906020019080838360005b8381101561027a57808201518184"
        "015260208101905061025f565b50505050905090810190601f1680156102a75780820380516001836020036101"
        "000a031916815260200191505b50945050505050602060405180830381600087803b1580156102c857600080fd"
        "5b505af11580156102dc573d6000803e3d6000fd5b505050506040513d60208110156102f257600080fd5b8101"
        "90808051906020019092919050505090509392505050565b60008060009054906101000a900473ffffffffffff"
        "ffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166311e3f2af848460"
        "40518363ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401"
        "808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff"
        "16815260200180602001828103825283818151815260200191508051906020019080838360005b838110156103"
        "ea5780820151818401526020810190506103cf565b50505050905090810190601f168015610417578082038051"
        "6001836020036101000a031916815260200191505b509350505050602060405180830381600087803b15801561"
        "043757600080fd5b505af115801561044b573d6000803e3d6000fd5b505050506040513d602081101561046157"
        "600080fd5b81019080805190602001909291905050509050929150505600a165627a7a72305820fd4857231ba5"
        "7cb17d47d43e38f1370285cfd965b622af793ee1bd9a3e490d270029";
    std::string consTestAddress;
    std::string sysTestAddress;
    std::string paraTestAddress;
    std::string sender;
    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
};
BOOST_FIXTURE_TEST_SUITE(precompiledConfigTest, ConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(sysConfig_test)
{
    deployTest(sysTestBin, sysTestAddress);

    auto simpleSetFunc = [&](protocol::BlockNumber _number, int _contextId, const std::string& _key,
                             const std::string& _v, int _errorCode = 0) {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("setValueByKeyTest(string,string)", _key, _v);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(sysTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled setValueByKey
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
        commitBlock(_number);
    };
    // simple set SYSTEM_KEY_TX_GAS_LIMIT
    {
        simpleSetFunc(2, 100, ledger::SYSTEM_KEY_TX_GAS_LIMIT, std::string("1000000"));
    }

    // simple get SYSTEM_KEY_TX_GAS_LIMIT
    {
        nextBlock(3);
        bytes in =
            codec->encodeWithSig("getValueByKeyTest(string)", std::string(ledger::SYSTEM_KEY_TX_GAS_LIMIT));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(sysTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled setValueByKey
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string("1000000"), u256(3)));
        commitBlock(3);
    }

    // simple set SYSTEM_KEY_TX_COUNT_LIMIT
    {
        simpleSetFunc(4, 102, ledger::SYSTEM_KEY_TX_COUNT_LIMIT, std::string("1000"));
    }

    // set SYSTEM_KEY_TX_COUNT_LIMIT error
    {
        simpleSetFunc(5, 103, ledger::SYSTEM_KEY_TX_COUNT_LIMIT, std::string("error"),
            CODE_INVALID_CONFIGURATION_VALUES);
    }
    // set error key
    {
        simpleSetFunc(8, 106, std::string("errorKey"), std::string("1000"),
            CODE_INVALID_CONFIGURATION_VALUES);
    }

    // get error key
    {
        nextBlock(9);
        bytes in = codec->encodeWithSig("getValueByKeyTest(string)", std::string("error"));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(107);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(sysTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled setValueByKey
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string(""), s256(-1)));
        commitBlock(9);
    }
}

BOOST_AUTO_TEST_CASE(consensus_test)
{
    deployTest(consTestBin, consTestAddress);

    std::string node1;
    std::string node2;
    for (int i = 0; i < 128; ++i)
    {
        node1 += "1";
        node2 += "2";
    }

    auto callFunc = [&](protocol::BlockNumber _number, int _contextId, const std::string& method,
                        const std::string& _nodeId, int _w = -1, int _errorCode = 0) {
        BCOS_LOG(DEBUG) << LOG_BADGE("consensus_test") << LOG_KV("method", method)
                        << LOG_KV("_nodeId", _nodeId) << LOG_KV("_w", _w)
                        << LOG_KV("_errorCode", _errorCode);
        nextBlock(_number);
        bytes in = _w < 0 ? codec->encodeWithSig(method, _nodeId) :
                            codec->encodeWithSig(method, _nodeId, u256(_w));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(consTestAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call precompiled
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
        if (result3->data().toBytes() != codec->encode(s256(_errorCode)))
        {
            PRECOMPILED_LOG(TRACE) << "Mismatch result: " << toHex(result3->data().toBytes())
                                   << " expect: " << toHex(codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
    };

    // node id too short
    {
        callFunc(2, 100, "addSealerTest(string,uint256)", std::string("111111"), 1,
            CODE_INVALID_NODE_ID);

        callFunc(
            2, 100, "addObserverTest(string)", std::string("111111"), -1, CODE_INVALID_NODE_ID);
        callFunc(2, 100, "removeTest(string)", std::string("111111"), -1, CODE_INVALID_NODE_ID);
        callFunc(2, 100, "setWeightTest(string,uint256)", std::string("111111"), 11,
            CODE_INVALID_NODE_ID);
    }

    // add sealer node1
    {
        callFunc(3, 101, "addSealerTest(string,uint256)", node1, 1, 0);
    }

    // add observer node2
    {
        callFunc(4, 105, "addObserverTest(string)", node2, -1, 0);
    }

    // turn last sealer to observer
    {
        callFunc(5, 106, "addObserverTest(string)", node1, -1, CODE_LAST_SEALER);
    }

    // removeTest last sealer
    {
        callFunc(5, 107, "removeTest(string)", node1, -1, CODE_LAST_SEALER);
    }

    // set an invalid weight(0) to node
    {
        callFunc(5, 108, "setWeightTest(string,uint256)", node1, 0, CODE_INVALID_WEIGHT);
    }

    // set a valid weight(2) to node1
    {
        callFunc(5, 108, "setWeightTest(string,uint256)", node1, 2);
    }

    // removeTest observer
    {
        callFunc(6, 108, "removeTest(string)", node2, -1, 0);
    }

    // set weigh to not exist node2
    {
        callFunc(6, 109, "setWeightTest(string,uint256)", node2, 123, CODE_NODE_NOT_EXIST);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test