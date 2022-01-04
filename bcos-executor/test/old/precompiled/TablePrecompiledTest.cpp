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
 * @file TableFactoryPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-21
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/TableFactoryPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class TableFactoryPrecompiledFixture : public PrecompiledFixture
{
public:
    TableFactoryPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        tableTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~TableFactoryPrecompiledFixture() {}

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
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), tableTestAddress);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        // to table
        result->setSeq(1001);
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result2->contextID(), 99);
        BOOST_CHECK_EQUAL(result2->seq(), 1001);
        BOOST_CHECK_EQUAL(result2->origin(), sender);
        BOOST_CHECK_EQUAL(result2->to(), tableTestAddress);
        BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result2->create(), false);
        BOOST_CHECK_EQUAL(result2->status(), 0);

        // to contract
        result2->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK(result3);
        BOOST_CHECK(result3->data().size() != 0);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);

        // to kv table
        result3->setSeq(1002);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        BOOST_CHECK(result4);
        BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result4->contextID(), 99);
        BOOST_CHECK_EQUAL(result4->seq(), 1002);
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

    ExecutionMessage::UniquePtr creatTable(bool isKV, protocol::BlockNumber _number, int _contextId,
        const std::string& tableName, const std::string& key, const std::string& value,
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig(isKV ? "createKVTableTest(string,string,string)" :
                                               "createTableTest(string,string,string)",
            tableName, key, value);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result3;
    };

    ExecutionMessage::UniquePtr insert(protocol::BlockNumber _number, int _contextId,
        const std::string& key, const std::string& value1, const std::string& value2,
        int _errorCode = 1)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("insert(string,string,string)", key, value1, value2);
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

    ExecutionMessage::UniquePtr select(
        protocol::BlockNumber _number, int _contextId, const std::string& key)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("select(string)", key);
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

    ExecutionMessage::UniquePtr update(protocol::BlockNumber _number, int _contextId,
        const std::string& key, const std::string& value1, const std::string& value2,
        int _errorCode = 1)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("update(string,string,string)", key, value1, value2);
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

    ExecutionMessage::UniquePtr remove(
        protocol::BlockNumber _number, int _contextId, const std::string& key, int _errorCode = 1)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("remove(string)", key);
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
        BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(1)));

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

    std::string tableTestBin =
        "60806040523480156200001157600080fd5b506110016000806101000a81548173ffffffffffffffffffffffff"
        "ffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055506000805490"
        "6101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffff"
        "ffffffff166356004b6a6040518163ffffffff1660e01b8152600401620000ad90620003d8565b602060405180"
        "830381600087803b158015620000c857600080fd5b505af1158015620000dd573d6000803e3d6000fd5b505050"
        "506040513d601f19601f8201168201806040525081019062000103919062000216565b50611009600160006101"
        "000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffff"
        "ffffffffffffff160217905550600160009054906101000a900473ffffffffffffffffffffffffffffffffffff"
        "ffff1673ffffffffffffffffffffffffffffffffffffffff166356004b6a6040518163ffffffff1660e01b8152"
        "600401620001a2906200038c565b602060405180830381600087803b158015620001bd57600080fd5b505af115"
        "8015620001d2573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906200"
        "01f8919062000216565b5062000459565b60008151905062000210816200043f565b92915050565b6000602082"
        "840312156200022957600080fd5b60006200023984828501620001ff565b91505092915050565b600062000251"
        "60098362000424565b91507f745f6b765f74657374000000000000000000000000000000000000000000000060"
        "00830152602082019050919050565b60006200029360148362000424565b91507f6974656d5f70726963652c69"
        "74656d5f6e616d650000000000000000000000006000830152602082019050919050565b6000620002d5600683"
        "62000424565b91507f745f74657374000000000000000000000000000000000000000000000000000060008301"
        "52602082019050919050565b60006200031760028362000424565b91507f696400000000000000000000000000"
        "00000000000000000000000000000000006000830152602082019050919050565b600062000359600883620004"
        "24565b91507f6e616d652c61676500000000000000000000000000000000000000000000000060008301526020"
        "82019050919050565b60006060820190508181036000830152620003a78162000242565b905081810360208301"
        "52620003bc8162000308565b90508181036040830152620003d18162000284565b9050919050565b6000606082"
        "0190508181036000830152620003f381620002c6565b90508181036020830152620004088162000308565b9050"
        "81810360408301526200041d816200034a565b9050919050565b600082825260208201905092915050565b6000"
        "819050919050565b6200044a8162000435565b81146200045657600080fd5b50565b611d828062000469600039"
        "6000f3fe608060405234801561001057600080fd5b50600436106100935760003560e01c806380599e4b116100"
        "6657806380599e4b1461015a578063c1a76c4b1461018a578063da465d74146101bb578063e3c9a6b5146101eb"
        "578063fcd7e3c11461021b57610093565b80632fe99bdc1461009857806331c3e456146100c85780635d52d4d8"
        "146100f8578063693ec85e14610128575b600080fd5b6100b260048036038101906100ad919061154c565b6102"
        "4c565b6040516100bf9190611948565b60405180910390f35b6100e260048036038101906100dd919061154c56"
        "5b6104c5565b6040516100ef9190611948565b60405180910390f35b610112600480360381019061010d919061"
        "154c565b6107bc565b60405161011f9190611948565b60405180910390f35b610142600480360381019061013d"
        "919061149f565b61087d565b60405161015193929190611903565b60405180910390f35b610174600480360381"
        "019061016f919061149f565b6109a0565b6040516101819190611948565b60405180910390f35b6101a4600480"
        "360381019061019f919061149f565b610b45565b6040516101b2929190611985565b60405180910390f35b6101"
        "d560048036038101906101d0919061154c565b610c01565b6040516101e29190611948565b60405180910390f3"
        "5b6102056004803603810190610200919061154c565b610e0f565b6040516102129190611948565b6040518091"
        "0390f35b6102356004803603810190610230919061149f565b610ece565b604051610243929190611985565b60"
        "405180910390f35b60006102566110ef565b604051806040016040528060405180604001604052806002815260"
        "20017f696400000000000000000000000000000000000000000000000000000000000081525081526020018681"
        "525090506102aa6110ef565b60405180604001604052806040518060400160405280600481526020017f6e616d"
        "650000000000000000000000000000000000000000000000000000000081525081526020018681525090506102"
        "fe6110ef565b60405180604001604052806040518060400160405280600381526020017f616765000000000000"
        "000000000000000000000000000000000000000000000081525081526020018681525090506060600367ffffff"
        "ffffffffff8111801561036457600080fd5b5060405190808252806020026020018201604052801561039e5781"
        "6020015b61038b6110ef565b8152602001906001900390816103835790505b50905083816000815181106103af"
        "57fe5b602002602001018190525082816001815181106103c857fe5b6020026020010181905250818160028151"
        "81106103e157fe5b60200260200101819052506103f4611109565b604051806020016040528083815250905060"
        "008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffff"
        "ffffffffffffffffffffff16631b5c585f836040518263ffffffff1660e01b81526004016104619190611abc56"
        "5b602060405180830381600087803b15801561047b57600080fd5b505af115801561048f573d6000803e3d6000"
        "fd5b505050506040513d601f19601f820116820180604052508101906104b39190611476565b90508096505050"
        "505050509392505050565b60006104cf6110ef565b604051806040016040528060405180604001604052806004"
        "81526020017f6e616d650000000000000000000000000000000000000000000000000000000081525081526020"
        "018581525090506105236110ef565b60405180604001604052806040518060400160405280600381526020017f"
        "616765000000000000000000000000000000000000000000000000000000000081525081526020018581525090"
        "506060600267ffffffffffffffff8111801561058957600080fd5b506040519080825280602002602001820160"
        "405280156105c357816020015b6105b06110ef565b8152602001906001900390816105a85790505b5090508281"
        "6000815181106105d457fe5b602002602001018190525081816001815181106105ed57fe5b6020026020010181"
        "905250610600611109565b604051806020016040528083815250905061061961111c565b604051806060016040"
        "52806040518060400160405280600281526020017f696400000000000000000000000000000000000000000000"
        "000000000000000081525081526020018a81526020016000600581111561067257fe5b81525090506060600167"
        "ffffffffffffffff8111801561069157600080fd5b506040519080825280602002602001820160405280156106"
        "cb57816020015b6106b861111c565b8152602001906001900390816106b05790505b5090508181600081518110"
        "6106dc57fe5b60200260200101819052506106ef611148565b8181600001819052506000806000905490610100"
        "0a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffff"
        "ff166351c97b5b86846040518363ffffffff1660e01b8152600401610756929190611af1565b60206040518083"
        "0381600087803b15801561077057600080fd5b505af1158015610784573d6000803e3d6000fd5b505050506040"
        "513d601f19601f820116820180604052508101906107a89190611476565b905080985050505050505050509392"
        "505050565b600080600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ff"
        "ffffffffffffffffffffffffffffffffffffff166356004b6a8686866040518463ffffffff1660e01b81526004"
        "0161081e939291906119bc565b602060405180830381600087803b15801561083857600080fd5b505af1158015"
        "61084c573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906108709190"
        "611476565b9050809150509392505050565b6000606080600061088c611109565b600160009054906101000a90"
        "0473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16"
        "633e10510b876040518263ffffffff1660e01b81526004016108e79190611a08565b6000604051808303818680"
        "3b1580156108ff57600080fd5b505afa158015610913573d6000803e3d6000fd5b505050506040513d6000823e"
        "3d601f19601f8201168201806040525081019061093c9190611422565b8092508193505050606080831561098c"
        "57826000015160008151811061095e57fe5b602002602001015160200151915082600001516001815181106109"
        "7d57fe5b60200260200101516020015190505b838282965096509650505050509193909250565b60006109aa61"
        "111c565b60405180606001604052806040518060400160405280600281526020017f6964000000000000000000"
        "000000000000000000000000000000000000000000815250815260200184815260200160006005811115610a03"
        "57fe5b81525090506060600167ffffffffffffffff81118015610a2257600080fd5b5060405190808252806020"
        "0260200182016040528015610a5c57816020015b610a4961111c565b815260200190600190039081610a415790"
        "505b5090508181600081518110610a6d57fe5b6020026020010181905250610a80611148565b81816000018190"
        "525060008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffff"
        "ffffffffffffffffffffffffffff16633b747bf4836040518263ffffffff1660e01b8152600401610ae5919061"
        "1a87565b602060405180830381600087803b158015610aff57600080fd5b505af1158015610b13573d6000803e"
        "3d6000fd5b505050506040513d601f19601f82011682018060405250810190610b379190611476565b90508094"
        "5050505050919050565b60608060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff"
        "1673ffffffffffffffffffffffffffffffffffffffff16635d0d6d54846040518263ffffffff1660e01b815260"
        "0401610ba19190611963565b600060405180830381600087803b158015610bbb57600080fd5b505af115801561"
        "0bcf573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f8201168201806040525081019061"
        "0bf891906114e0565b91509150915091565b6000610c0b6110ef565b6040518060400160405280604051806040"
        "0160405280600a81526020017f6974656d5f707269636500000000000000000000000000000000000000000000"
        "8152508152602001858152509050610c5f6110ef565b6040518060400160405280604051806040016040528060"
        "0981526020017f6974656d5f6e616d650000000000000000000000000000000000000000000000815250815260"
        "20018581525090506060600267ffffffffffffffff81118015610cc557600080fd5b5060405190808252806020"
        "0260200182016040528015610cff57816020015b610cec6110ef565b815260200190600190039081610ce45790"
        "505b5090508281600081518110610d1057fe5b60200260200101819052508181600181518110610d2957fe5b60"
        "20026020010181905250610d3c611109565b604051806020016040528083815250905060006001600090549061"
        "01000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffff"
        "ffffff1663649a84288a846040518363ffffffff1660e01b8152600401610dac929190611a3d565b6020604051"
        "80830381600087803b158015610dc657600080fd5b505af1158015610dda573d6000803e3d6000fd5b50505050"
        "6040513d601f19601f82011682018060405250810190610dfe9190611476565b90508095505050505050939250"
        "5050565b60008060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffff"
        "ffffffffffffffffffffffffffffffff166356004b6a8686866040518463ffffffff1660e01b8152600401610e"
        "6f939291906119bc565b602060405180830381600087803b158015610e8957600080fd5b505af1158015610e9d"
        "573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610ec19190611476"
        "565b9050809150509392505050565b606080610ed961111c565b60405180606001604052806040518060400160"
        "405280600281526020017f69640000000000000000000000000000000000000000000000000000000000008152"
        "50815260200185815260200160006005811115610f3257fe5b81525090506060600167ffffffffffffffff8111"
        "8015610f5157600080fd5b50604051908082528060200260200182016040528015610f8b57816020015b610f78"
        "61111c565b815260200190600190039081610f705790505b5090508181600081518110610f9c57fe5b60200260"
        "20010181905250610faf611148565b818160000181905250606060008054906101000a900473ffffffffffffff"
        "ffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166345cfc70e83604051"
        "8263ffffffff1660e01b81526004016110139190611a87565b60006040518083038186803b15801561102b5760"
        "0080fd5b505afa15801561103f573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f820116"
        "8201806040525081019061106891906113e1565b90506060806000835111156110de5782600081518110611084"
        "57fe5b60200260200101516000015160008151811061109c57fe5b602002602001015160200151915082600081"
        "5181106110b757fe5b6020026020010151600001516001815181106110cf57fe5b602002602001015160200151"
        "90505b818197509750505050505050915091565b60405180604001604052806060815260200160608152509056"
        "5b6040518060200160405280606081525090565b60405180606001604052806060815260200160608152602001"
        "6000600581111561114257fe5b81525090565b6040518060200160405280606081525090565b600082601f8301"
        "1261116c57600080fd5b815161117f61117a82611b68565b611b3b565b91508181835260208401935060208101"
        "90508360005b838110156111c557815186016111ab8882611315565b8452602084019350602083019250506001"
        "81019050611195565b5050505092915050565b600082601f8301126111e057600080fd5b81516111f36111ee82"
        "611b90565b611b3b565b9150818183526020840193506020810190508360005b83811015611239578151860161"
        "121f8882611365565b845260208401935060208301925050600181019050611209565b5050505092915050565b"
        "60008151905061125281611d1e565b92915050565b60008151905061126781611d35565b92915050565b600082"
        "601f83011261127e57600080fd5b813561129161128c82611bb8565b611b3b565b915080825260208301602083"
        "018583830111156112ad57600080fd5b6112b8838284611cbe565b50505092915050565b600082601f83011261"
        "12d257600080fd5b81516112e56112e082611bb8565b611b3b565b915080825260208301602083018583830111"
        "1561130157600080fd5b61130c838284611ccd565b50505092915050565b600060208284031215611327576000"
        "80fd5b6113316020611b3b565b9050600082015167ffffffffffffffff81111561134d57600080fd5b61135984"
        "8285016111cf565b60008301525092915050565b60006040828403121561137757600080fd5b6113816040611b"
        "3b565b9050600082015167ffffffffffffffff81111561139d57600080fd5b6113a9848285016112c1565b6000"
        "83015250602082015167ffffffffffffffff8111156113c957600080fd5b6113d5848285016112c1565b602083"
        "01525092915050565b6000602082840312156113f357600080fd5b600082015167ffffffffffffffff81111561"
        "140d57600080fd5b6114198482850161115b565b91505092915050565b60008060408385031215611435576000"
        "80fd5b600061144385828601611243565b925050602083015167ffffffffffffffff81111561146057600080fd"
        "5b61146c85828601611315565b9150509250929050565b60006020828403121561148857600080fd5b60006114"
        "9684828501611258565b91505092915050565b6000602082840312156114b157600080fd5b600082013567ffff"
        "ffffffffffff8111156114cb57600080fd5b6114d78482850161126d565b91505092915050565b600080604083"
        "850312156114f357600080fd5b600083015167ffffffffffffffff81111561150d57600080fd5b611519858286"
        "016112c1565b925050602083015167ffffffffffffffff81111561153657600080fd5b611542858286016112c1"
        "565b9150509250929050565b60008060006060848603121561156157600080fd5b600084013567ffffffffffff"
        "ffff81111561157b57600080fd5b6115878682870161126d565b935050602084013567ffffffffffffffff8111"
        "156115a457600080fd5b6115b08682870161126d565b925050604084013567ffffffffffffffff8111156115cd"
        "57600080fd5b6115d98682870161126d565b9150509250925092565b60006115ef8383611814565b9050929150"
        "50565b600061160383836118bf565b905092915050565b600061161682611c04565b6116208185611c3f565b93"
        "508360208202850161163285611be4565b8060005b8581101561166e578484038952815161164f85826115e356"
        "5b945061165a83611c25565b925060208a01995050600181019050611636565b50829750879550505050505092"
        "915050565b600061168b82611c0f565b6116958185611c50565b9350836020820285016116a785611bf4565b80"
        "60005b858110156116e357848403895281516116c485826115f7565b94506116cf83611c32565b925060208a01"
        "9950506001810190506116ab565b50829750879550505050505092915050565b6116fe81611c83565b82525050"
        "565b61170d81611cac565b82525050565b61171c81611ca2565b82525050565b600061172d82611c1a565b6117"
        "378185611c61565b9350611747818560208601611ccd565b61175081611d00565b840191505092915050565b60"
        "0061176682611c1a565b6117708185611c72565b9350611780818560208601611ccd565b61178981611d00565b"
        "840191505092915050565b60006117a1600983611c72565b91507f745f6b765f74657374000000000000000000"
        "00000000000000000000000000006000830152602082019050919050565b60006117e1600683611c72565b9150"
        "7f745f746573740000000000000000000000000000000000000000000000000000600083015260208201905091"
        "9050565b600060608301600083015184820360008601526118318282611722565b915050602083015184820360"
        "2086015261184b8282611722565b91505060408301516118606040860182611704565b50809150509291505056"
        "5b60006020830160008301518482036000860152611888828261160b565b9150508091505092915050565b6000"
        "60208301600083015184820360008601526118b28282611680565b9150508091505092915050565b6000604083"
        "01600083015184820360008601526118dc8282611722565b915050602083015184820360208601526118f68282"
        "611722565b9150508091505092915050565b600060608201905061191860008301866116f5565b818103602083"
        "015261192a818561175b565b9050818103604083015261193e818461175b565b9050949350505050565b600060"
        "208201905061195d6000830184611713565b92915050565b6000602082019050818103600083015261197d8184"
        "61175b565b905092915050565b6000604082019050818103600083015261199f818561175b565b905081810360"
        "208301526119b3818461175b565b90509392505050565b600060608201905081810360008301526119d6818661"
        "175b565b905081810360208301526119ea818561175b565b905081810360408301526119fe818461175b565b90"
        "50949350505050565b60006040820190508181036000830152611a2181611794565b9050818103602083015261"
        "1a35818461175b565b905092915050565b60006060820190508181036000830152611a5681611794565b905081"
        "81036020830152611a6a818561175b565b90508181036040830152611a7e8184611895565b9050939250505056"
        "5b60006040820190508181036000830152611aa0816117d4565b90508181036020830152611ab4818461186b56"
        "5b905092915050565b60006040820190508181036000830152611ad5816117d4565b9050818103602083015261"
        "1ae98184611895565b905092915050565b60006060820190508181036000830152611b0a816117d4565b905081"
        "81036020830152611b1e8185611895565b90508181036040830152611b32818461186b565b9050939250505056"
        "5b6000604051905081810181811067ffffffffffffffff82111715611b5e57600080fd5b806040525091905056"
        "5b600067ffffffffffffffff821115611b7f57600080fd5b602082029050602081019050919050565b600067ff"
        "ffffffffffffff821115611ba757600080fd5b602082029050602081019050919050565b600067ffffffffffff"
        "ffff821115611bcf57600080fd5b601f19601f8301169050602081019050919050565b60008190506020820190"
        "50919050565b6000819050602082019050919050565b600081519050919050565b600081519050919050565b60"
        "0081519050919050565b6000602082019050919050565b6000602082019050919050565b600082825260208201"
        "905092915050565b600082825260208201905092915050565b600082825260208201905092915050565b600082"
        "825260208201905092915050565b60008115159050919050565b6000819050611c9d82611d11565b919050565b"
        "6000819050919050565b6000611cb782611c8f565b9050919050565b82818337600083830152505050565b6000"
        "5b83811015611ceb578082015181840152602081019050611cd0565b83811115611cfa576000848401525b5050"
        "5050565b6000601f19601f8301169050919050565b60068110611d1b57fe5b50565b611d2781611c83565b8114"
        "611d3257600080fd5b50565b611d3e81611ca2565b8114611d4957600080fd5b5056fea2646970667358221220"
        "e2e94eab0a3df9b9c3eddd5e8eb98d046ee320848c3c0abf4b45b6d156cc734864736f6c634300060c0033";
    std::string tableTestAddress;
    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledTableTest, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    deployTest();
    {
        creatTable(false, 1, 100, "t_test", "id", "item_name,item_id");
    }

    // createTable exist
    {
        creatTable(
            false, 2, 101, "t_test", "id", "item_name,item_id", CODE_TABLE_NAME_ALREADY_EXIST);
    }

    // createTable too long tableName, key and filed
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(9);
    }
    {
        auto r1 = creatTable(false, 2, 102, errorStr, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(false, 2, 103, "t_test", errorStr, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(false, 2, 104, "t_test", "id", errorStr);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and filed
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(false, 2, 105, errorStr2, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(false, 2, 106, "t_test", errorStr2, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(false, 2, 107, "t_test", "id", errorStr2);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(createKVTableTest)
{
    deployTest();
    {
        creatTable(true, 1, 100, "t_test1", "id", "item_name,item_id");
    }

    // createTable exist
    {
        creatTable(
            true, 2, 101, "t_test1", "id", "item_name,item_id", CODE_TABLE_NAME_ALREADY_EXIST);
    }

    // createTable too long tableName, key and filed
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(9);
    }
    {
        auto r1 = creatTable(true, 2, 102, errorStr, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(true, 2, 103, "t_test", errorStr, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(true, 2, 104, "t_test", "id", errorStr);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and filed
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(true, 2, 105, errorStr2, "id", "item_name,item_id");
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(true, 2, 106, "t_test", errorStr2, "item_name,item_id");
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(true, 2, 107, "t_test", "id", errorStr2);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(selectTest)
{
    deployTest();

    auto result1 = insert(2, 1000, "id1", "test1", "1");
    auto result2 = select(3, 1000, "id1");
    BOOST_CHECK(result2->data().toBytes() == codec->encode(std::string("test1"), std::string("1")));

    // not exist
    auto result3 = select(3, 1000, "id2");
    BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string(""), std::string("")));

    // TODO: add key not exist in condition
}

BOOST_AUTO_TEST_CASE(insertTest)
{
    deployTest();
    auto result1 = insert(2, 1000, "id1", "test1", "1");

    auto result2 = insert(3, 1000, "id1", "test1", "1", CODE_INSERT_KEY_EXIST);

    // TODO: insert entry without key
}

BOOST_AUTO_TEST_CASE(updateTest)
{
    deployTest();
    auto result1 = insert(2, 1000, "id1", "test1", "1");
    auto result2 = select(3, 1000, "id1");
    BOOST_CHECK(result2->data().toBytes() == codec->encode(std::string("test1"), std::string("1")));

    auto result3 = update(4, 1000, "id1", "test2", "2");
    auto result4 = select(5, 1000, "id1");
    BOOST_CHECK(result4->data().toBytes() == codec->encode(std::string("test2"), std::string("2")));

    // not exist
    auto result5 = update(6, 1000, "id2", "test2", "2", CODE_UPDATE_KEY_NOT_EXIST);
    BOOST_CHECK(result5->data().toBytes() == codec->encode(s256((int)CODE_UPDATE_KEY_NOT_EXIST)));

    // TODO: update condition without key
}

BOOST_AUTO_TEST_CASE(removeTest)
{
    deployTest();
    auto result1 = insert(2, 1000, "id1", "test1", "1");
    auto result2 = select(3, 1000, "id1");
    BOOST_CHECK(result2->data().toBytes() == codec->encode(std::string("test1"), std::string("1")));

    auto result3 = remove(4, 1000, "id1");
    BOOST_CHECK(result3->data().toBytes() == codec->encode(s256(1)));
    auto result4 = select(5, 1000, "id1");
    BOOST_CHECK(result4->data().toBytes() == codec->encode(std::string(""), std::string("")));

    auto result5 = remove(6, 1000, "id2");
    BOOST_CHECK(result5->data().toBytes() == codec->encode(s256(1)));
    // TODO: remove condition without key
}

BOOST_AUTO_TEST_CASE(descTest)
{
    deployTest();
    auto result1 = desc(2, 1000, "t_test");
    BOOST_CHECK(
        result1->data().toBytes() == codec->encode(std::string("id"), std::string("name,age")));
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
