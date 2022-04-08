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
 * @file KVTablePrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-12-01
 */

#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/KVTableFactoryPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class KVTableFactoryPrecompiledFixture : public PrecompiledFixture
{
public:
    KVTableFactoryPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false);
        tableTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~KVTableFactoryPrecompiledFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(tableTestBin, std::back_inserter(input));
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
        params->setTo(tableTestAddress);
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
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), tableTestAddress);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        // to kv table
        result->setSeq(1001);
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result2->contextID(), 99);
        BOOST_CHECK_EQUAL(result2->seq(), 1001);
        BOOST_CHECK_EQUAL(result2->origin(), sender);
        BOOST_CHECK_EQUAL(result2->to(), precompiled::BFS_ADDRESS);
        BOOST_CHECK_EQUAL(result2->create(), false);
        BOOST_CHECK_EQUAL(result2->status(), 0);

        // to bfs create table file
        result2->setSeq(1002);
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1002);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->to(), precompiled::KV_TABLE_ADDRESS);
        BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result3->create(), false);
        BOOST_CHECK_EQUAL(result3->status(), 0);

        // bfs touch finish callback to kv table
        result3->setSeq(1001);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result4->contextID(), 99);
        BOOST_CHECK_EQUAL(result4->seq(), 1001);
        BOOST_CHECK_EQUAL(result4->origin(), sender);
        BOOST_CHECK_EQUAL(result4->to(), tableTestAddress);
        BOOST_CHECK_EQUAL(result4->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result4->create(), false);
        BOOST_CHECK_EQUAL(result4->status(), 0);

        // to contract
        result4->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        BOOST_CHECK(result5);
        BOOST_CHECK(result5->data().size() == 0);
        BOOST_CHECK_EQUAL(result5->contextID(), 99);
        BOOST_CHECK_EQUAL(result5->seq(), 1000);

        commitBlock(1);
    }

    ExecutionMessage::UniquePtr creatTable(protocol::BlockNumber _number, int _contextId,
        const std::string& tableName, const std::string& key, const std::string& value,
        int _errorCode = 0, bool _errorInKv = false)
    {
        nextBlock(_number);
        bytes in =
            codec->encodeWithSig("createKVTableTest(string,string,string)", tableName, key, value);
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
        params2->setTo(tableTestAddress);
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

        // call kv precompiled
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        if (_errorInKv)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
            }
            commitBlock(_number);
            return result3;
        }

        result3->setSeq(1002);

        // call bfs create
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1001);

        // bfs create finish, callback to kv precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        result5->setSeq(1000);

        // callback to contract
        std::promise<ExecutionMessage::UniquePtr> executePromise6;
        executor->executeTransaction(std::move(result5),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise6.set_value(std::move(result));
            });
        auto result6 = executePromise6.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result6->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result6;
    };

    ExecutionMessage::UniquePtr desc(
        protocol::BlockNumber _number, int _contextId, const std::string& key)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("descTest(string)", key);
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
        params2->setTo(tableTestAddress);
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

        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr get(
        protocol::BlockNumber _number, int _contextId, const std::string& key)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("get(string)", key);
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
        params2->setTo(tableTestAddress);
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

        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr set(protocol::BlockNumber _number, int _contextId,
        const std::string& key, const std::string& value1, const std::string& value2,
        int _errorCode = 1)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("set(string,string,string)", key, value1, value2);
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
        params2->setTo(tableTestAddress);
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

        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        BOOST_CHECK(result4->data().toBytes() == codec->encode(s256(_errorCode)));
        commitBlock(_number);
        return result4;
    }

    // clang-format off
    std::string tableTestBin = "60806040523480156200001157600080fd5b506110096000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166356004b6a6040518163ffffffff1660e01b8152600401620000ae9062000214565b602060405180830381600087803b158015620000c957600080fd5b505af1158015620000de573d6000803e3d6000fd5b505050506040513d601f19601f8201168201806040525081019062000104919062000122565b5062000295565b6000815190506200011c816200027b565b92915050565b6000602082840312156200013557600080fd5b600062000145848285016200010b565b91505092915050565b60006200015d60098362000260565b91507f745f6b765f7465737400000000000000000000000000000000000000000000006000830152602082019050919050565b60006200019f60148362000260565b91507f6974656d5f70726963652c6974656d5f6e616d650000000000000000000000006000830152602082019050919050565b6000620001e160028362000260565b91507f69640000000000000000000000000000000000000000000000000000000000006000830152602082019050919050565b600060608201905081810360008301526200022f816200014e565b905081810360208301526200024481620001d2565b90508181036040830152620002598162000190565b9050919050565b600082825260208201905092915050565b6000819050919050565b620002868162000271565b81146200029257600080fd5b50565b610ec280620002a56000396000f3fe608060405234801561001057600080fd5b506004361061004c5760003560e01c80635d52d4d814610051578063693ec85e14610081578063c1a76c4b146100b3578063da465d74146100e4575b600080fd5b61006b6004803603810190610066919061092c565b610114565b6040516100789190610bcf565b60405180910390f35b61009b6004803603810190610096919061087f565b6101d4565b6040516100aa93929190610b8a565b60405180910390f35b6100cd60048036038101906100c8919061087f565b6102f9565b6040516100db929190610c0c565b60405180910390f35b6100fe60048036038101906100f9919061092c565b6103b6565b60405161010b9190610bcf565b60405180910390f35b6000806000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166356004b6a8686866040518463ffffffff1660e01b815260040161017593929190610c43565b602060405180830381600087803b15801561018f57600080fd5b505af11580156101a3573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906101c79190610856565b9050809150509392505050565b600060608060008090506101e66105c3565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16633e10510b876040518263ffffffff1660e01b81526004016102409190610c8f565b60006040518083038186803b15801561025857600080fd5b505afa15801561026c573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f820116820180604052508101906102959190610802565b809250819350505060608083156102e55782600001516000815181106102b757fe5b602002602001015160200151915082600001516001815181106102d657fe5b60200260200101516020015190505b838282965096509650505050509193909250565b6060806000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16635d0d6d54846040518263ffffffff1660e01b81526004016103569190610bea565b600060405180830381600087803b15801561037057600080fd5b505af1158015610384573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f820116820180604052508101906103ad91906108c0565b91509150915091565b60006103c06105d6565b60405180604001604052806040518060400160405280600a81526020017f6974656d5f70726963650000000000000000000000000000000000000000000081525081526020018581525090506104146105d6565b60405180604001604052806040518060400160405280600981526020017f6974656d5f6e616d65000000000000000000000000000000000000000000000081525081526020018581525090506060600267ffffffffffffffff8111801561047a57600080fd5b506040519080825280602002602001820160405280156104b457816020015b6104a16105d6565b8152602001906001900390816104995790505b50905082816000815181106104c557fe5b602002602001018190525081816001815181106104de57fe5b60200260200101819052506104f16105c3565b604051806020016040528083815250905060008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663649a84288a846040518363ffffffff1660e01b8152600401610560929190610cc4565b602060405180830381600087803b15801561057a57600080fd5b505af115801561058e573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906105b29190610856565b905080955050505050509392505050565b6040518060200160405280606081525090565b604051806040016040528060608152602001606081525090565b600082601f83011261060157600080fd5b815161061461060f82610d3b565b610d0e565b9150818183526020840193506020810190508360005b8381101561065a57815186016106408882610786565b84526020840193506020830192505060018101905061062a565b5050505092915050565b60008151905061067381610e5e565b92915050565b60008151905061068881610e75565b92915050565b600082601f83011261069f57600080fd5b81356106b26106ad82610d63565b610d0e565b915080825260208301602083018583830111156106ce57600080fd5b6106d9838284610e0b565b50505092915050565b600082601f8301126106f357600080fd5b815161070661070182610d63565b610d0e565b9150808252602083016020830185838301111561072257600080fd5b61072d838284610e1a565b50505092915050565b60006020828403121561074857600080fd5b6107526020610d0e565b9050600082015167ffffffffffffffff81111561076e57600080fd5b61077a848285016105f0565b60008301525092915050565b60006040828403121561079857600080fd5b6107a26040610d0e565b9050600082015167ffffffffffffffff8111156107be57600080fd5b6107ca848285016106e2565b600083015250602082015167ffffffffffffffff8111156107ea57600080fd5b6107f6848285016106e2565b60208301525092915050565b6000806040838503121561081557600080fd5b600061082385828601610664565b925050602083015167ffffffffffffffff81111561084057600080fd5b61084c85828601610736565b9150509250929050565b60006020828403121561086857600080fd5b600061087684828501610679565b91505092915050565b60006020828403121561089157600080fd5b600082013567ffffffffffffffff8111156108ab57600080fd5b6108b78482850161068e565b91505092915050565b600080604083850312156108d357600080fd5b600083015167ffffffffffffffff8111156108ed57600080fd5b6108f9858286016106e2565b925050602083015167ffffffffffffffff81111561091657600080fd5b610922858286016106e2565b9150509250929050565b60008060006060848603121561094157600080fd5b600084013567ffffffffffffffff81111561095b57600080fd5b6109678682870161068e565b935050602084013567ffffffffffffffff81111561098457600080fd5b6109908682870161068e565b925050604084013567ffffffffffffffff8111156109ad57600080fd5b6109b98682870161068e565b9150509250925092565b60006109cf8383610b46565b905092915050565b60006109e282610d9f565b6109ec8185610dc2565b9350836020820285016109fe85610d8f565b8060005b85811015610a3a5784840389528151610a1b85826109c3565b9450610a2683610db5565b925060208a01995050600181019050610a02565b50829750879550505050505092915050565b610a5581610df5565b82525050565b610a6481610e01565b82525050565b6000610a7582610daa565b610a7f8185610dd3565b9350610a8f818560208601610e1a565b610a9881610e4d565b840191505092915050565b6000610aae82610daa565b610ab88185610de4565b9350610ac8818560208601610e1a565b610ad181610e4d565b840191505092915050565b6000610ae9600983610de4565b91507f745f6b765f7465737400000000000000000000000000000000000000000000006000830152602082019050919050565b60006020830160008301518482036000860152610b3982826109d7565b9150508091505092915050565b60006040830160008301518482036000860152610b638282610a6a565b91505060208301518482036020860152610b7d8282610a6a565b9150508091505092915050565b6000606082019050610b9f6000830186610a4c565b8181036020830152610bb18185610aa3565b90508181036040830152610bc58184610aa3565b9050949350505050565b6000602082019050610be46000830184610a5b565b92915050565b60006020820190508181036000830152610c048184610aa3565b905092915050565b60006040820190508181036000830152610c268185610aa3565b90508181036020830152610c3a8184610aa3565b90509392505050565b60006060820190508181036000830152610c5d8186610aa3565b90508181036020830152610c718185610aa3565b90508181036040830152610c858184610aa3565b9050949350505050565b60006040820190508181036000830152610ca881610adc565b90508181036020830152610cbc8184610aa3565b905092915050565b60006060820190508181036000830152610cdd81610adc565b90508181036020830152610cf18185610aa3565b90508181036040830152610d058184610b1c565b90509392505050565b6000604051905081810181811067ffffffffffffffff82111715610d3157600080fd5b8060405250919050565b600067ffffffffffffffff821115610d5257600080fd5b602082029050602081019050919050565b600067ffffffffffffffff821115610d7a57600080fd5b601f19601f8301169050602081019050919050565b6000819050602082019050919050565b600081519050919050565b600081519050919050565b6000602082019050919050565b600082825260208201905092915050565b600082825260208201905092915050565b600082825260208201905092915050565b60008115159050919050565b6000819050919050565b82818337600083830152505050565b60005b83811015610e38578082015181840152602081019050610e1d565b83811115610e47576000848401525b50505050565b6000601f19601f8301169050919050565b610e6781610df5565b8114610e7257600080fd5b50565b610e7e81610e01565b8114610e8957600080fd5b5056fea2646970667358221220d1f14fcbfe3a52cfa7f060caf151ac14d0f3de0dbafb6b628c01812592d9b1c464736f6c634300060a0033";
    // clang-format on
    std::string tableTestAddress;
    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledKVTableTest, KVTableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    int64_t number = 2;
    deployTest();
    {
        creatTable(number++, 100, "t_test", "id", "item_name,item_id");
    }

    // createTable exist
    {
        creatTable(number++, 101, "t_test", "id", "item_name,item_id",
            CODE_TABLE_NAME_ALREADY_EXIST, true);
    }

    // createTable build
    {
        creatTable(
            number++, 101, "t_test/t_test2", "id", "item_name,item_id", CODE_FILE_BUILD_DIR_FAILED);
    }

    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(0);
    }
    // createTable too long tableName, key and field
    {
        auto r1 = creatTable(number++, 102, errorStr, "id", "item_name,item_id", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(number++, 103, "t_test", errorStr, "item_name,item_id", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, 104, "t_test", "id", errorStr, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(number++, 105, errorStr2, "id", "item_name,item_id", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(number++, 106, "t_test", errorStr2, "item_name,item_id", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, 107, "t_test", "id", errorStr2, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(descTest)
{
    deployTest();
    // not exist
    auto result1 = desc(2, 1000, "t_test");
    BOOST_CHECK(result1->data().toBytes() == codec->encode(std::string(""), std::string("")));

    auto result2 = desc(2, 1000, "t_kv_test");
    BOOST_CHECK(result2->data().toBytes() ==
                codec->encode(std::string("id"), std::string("item_price,item_name")));
}

BOOST_AUTO_TEST_CASE(setTest)
{
    deployTest();
    auto result1 = set(2, 1000, "id1", "100", "test1");
    auto result2 = get(3, 1000, "id1");
    BOOST_CHECK(
        result2->data().toBytes() == codec->encode(true, std::string("100"), std::string("test1")));
    auto result3 = set(4, 1000, "id1", "101", "test2");
    auto result4 = get(5, 1000, "id1");
    BOOST_CHECK(
        result4->data().toBytes() == codec->encode(true, std::string("101"), std::string("test2")));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
