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
 * @file TablePrecompiledTest.cpp
 * @author: kyonRay
 * @date 2022-04-13
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/TableManagerPrecompiled.h"
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
    TableFactoryPrecompiledFixture() { init(false); }

    void init(bool isWasm)
    {
        setIsWasm(isWasm);
        tableTestAddress = isWasm ? "/tables/t_test" : "420f853b49838bd3e9466c85a4cc3428c960dde2";
    }

    virtual ~TableFactoryPrecompiledFixture() = default;

    ExecutionMessage::UniquePtr creatTable(protocol::BlockNumber _number,
        const std::string& tableName, const std::string& key, const std::vector<std::string>& value,
        const std::string& callAddress, int _errorCode = 0, bool errorInTableManager = false)
    {
        nextBlock(_number);
        TableInfoTuple tableInfoTuple = std::make_tuple(key, value);
        bytes in = codec->encodeWithSig(
            "createTable(string,(string,string[]))", tableName, tableInfoTuple);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 100, 10000, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        if (errorInTableManager)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            commitBlock(_number);
            return result2;
        }

        // set new address
        result2->setTo(callAddress);
        // external create
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);
        // external call bfs
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call bfs success, callback to create
        result4->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        // create success, callback to precompiled
        result5->setSeq(1000);

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

    ExecutionMessage::UniquePtr appendColumns(protocol::BlockNumber _number,
        const std::string& tableName, const std::vector<std::string>& values, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("appendColumns(string,string[])", tableName, values);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 100, 10000, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr list(
        protocol::BlockNumber _number, std::string const& path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("list(string)", path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? BFS_NAME : BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            std::vector<BfsTuple> empty;
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode), empty));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr openTable(
        protocol::BlockNumber _number, std::string const& _path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("openTable(string)", _path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr desc(protocol::BlockNumber _number, std::string const& tableName)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("desc(string)", tableName);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
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
        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr insert(protocol::BlockNumber _number, const std::string& key,
        const std::vector<std::string>& values, const std::string& callAddress)
    {
        nextBlock(_number);
        EntryTuple entryTuple = {key, values};
        bytes in = codec->encodeWithSig("insert((string,string[]))", entryTuple);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr selectByKey(
        protocol::BlockNumber _number, const std::string& key, const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("select(string)", key);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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
        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr selectByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTuple>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("select((uint8,string)[],(uint32,uint32))", keyCond, limit);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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
        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr count(protocol::BlockNumber _number,
        const std::vector<ConditionTuple>& keyCond, const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("count((uint8,string)[])", keyCond);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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
        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr updateByKey(protocol::BlockNumber _number, const std::string& key,
        const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
        const std::string& callAddress, bool _isErrorInTable = false)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("update(string,(string,string)[])", key, _updateFields);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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

        if (_isErrorInTable)
        {
            // call precompiled
            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr updateByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTuple>& conditions, const LimitTuple& _limit,
        const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
        const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in =
            codec->encodeWithSig("update((uint8,string)[],(uint32,uint32),(string,string)[])",
                conditions, _limit, _updateFields);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr removeByKey(
        protocol::BlockNumber _number, const std::string& key, const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("remove(string)", key);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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
        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr removeByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTuple>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("remove((uint8,string)[],(uint32,uint32))", keyCond, limit);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(callAddress);
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
        commitBlock(_number);
        return result2;
    }

    std::string tableTestAddress;
    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledTableTest, TableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // open table not exist
    {
        auto response1 = openTable(number++, "t_test2");
        BOOST_CHECK(response1->data().toBytes() == codec->encode(Address()));

        auto response2 = openTable(number++, tableTestAddress);
        BOOST_CHECK(response2->data().toBytes() == codec->encode(Address()));
    }

    // check create
    {
        auto r1 = openTable(number++, "t_test");
        BOOST_CHECK(r1->data().toBytes() == codec->encode(Address(tableTestAddress)));

        auto response1 = openTable(number++, "/tables/t_test");
        Address address;
        codec->decode(response1->data(), address);
        BOOST_CHECK(address.hex() == tableTestAddress);
        auto response2 = list(number++, "/tables");
        int32_t ret;
        std::vector<BfsTuple> bfsInfos;
        codec->decode(response2->data(), ret, bfsInfos);
        BOOST_CHECK(ret == 0);
        BOOST_CHECK(bfsInfos.size() == 1);
        auto fileInfo = bfsInfos[0];
        BOOST_CHECK(std::get<0>(fileInfo) == "t_test");
        BOOST_CHECK(std::get<1>(fileInfo) == FS_TYPE_LINK);
    }

    // createTable exist
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress,
            CODE_TABLE_NAME_ALREADY_EXIST, true);
    }

    // createTable too long tableName, key and field
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(1);
    }
    {
        auto r1 =
            creatTable(number++, errorStr, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", errorStr, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", "id", {errorStr}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 =
            creatTable(number++, errorStr2, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", errorStr2, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", "id", {errorStr2}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // check trim create table params
    {
        auto r1 = creatTable(number++, "t_test123", " id ", {" item_name ", " item_id "},
            "420f853b49838bd3e9466c85a4cc3428c960dde3");
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));
        auto r2 = desc(number++, "t_test123");
        TableInfoTuple t = {"id", {"item_name", "item_id"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(t));
    }
}

BOOST_AUTO_TEST_CASE(createTableWasmsTest)
{
    init(true);

    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // check create
    {
        auto response2 = list(number++, "/tables");
        int32_t ret;
        std::vector<BfsTuple> bfsInfos;
        codec->decode(response2->data(), ret, bfsInfos);
        BOOST_CHECK(ret == 0);
        BOOST_CHECK(bfsInfos.size() == 1);
        auto fileInfo = bfsInfos[0];
        BOOST_CHECK(std::get<0>(fileInfo) == "t_test");
        BOOST_CHECK(std::get<1>(fileInfo) == executor::FS_TYPE_CONTRACT);
    }

    // createTable exist
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress,
            CODE_TABLE_NAME_ALREADY_EXIST, true);
    }

    // createTable too long tableName, key and field
    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(1);
    }
    {
        auto r1 =
            creatTable(number++, errorStr, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", errorStr, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", "id", {errorStr}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 =
            creatTable(number++, errorStr2, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", errorStr2, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", "id", {errorStr2}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(appendColumnsTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // check create
    {
        auto response1 = openTable(number++, "/tables/t_test");
        Address address;
        codec->decode(response1->data(), address);
        BOOST_CHECK(address.hex() == tableTestAddress);
        auto response2 = list(number++, "/tables");
        int32_t ret;
        std::vector<BfsTuple> bfsInfos;
        codec->decode(response2->data(), ret, bfsInfos);
        BOOST_CHECK(ret == 0);
        BOOST_CHECK(bfsInfos.size() == 1);
        auto fileInfo = bfsInfos[0];
        BOOST_CHECK(std::get<0>(fileInfo) == "t_test");
        BOOST_CHECK(std::get<1>(fileInfo) == FS_TYPE_LINK);
    }
    // simple append
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v2"});
        auto r2 = desc(number++, "t_test");
        TableInfoTuple tableInfo = {"id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));
    }
    // append not exist table
    {
        auto r1 = appendColumns(number++, "t_test2", {"v1", "v2"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int32_t)CODE_TABLE_NOT_EXIST));
    }
    // append duplicate field
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v3"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int32_t)CODE_TABLE_DUPLICATE_FIELD));
    }

    // append too long field
    {
        std::string longField = "0";
        for (int j = 0; j < USER_TABLE_FIELD_NAME_MAX_LENGTH; ++j)
        {
            longField += "0";
        }
        auto r1 = appendColumns(number++, "t_test", {"v3", longField});
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(appendColumnsWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // check create
    {
        auto response2 = list(number++, "/tables");
        int32_t ret;
        std::vector<BfsTuple> bfsInfos;
        codec->decode(response2->data(), ret, bfsInfos);
        BOOST_CHECK(ret == 0);
        BOOST_CHECK(bfsInfos.size() == 1);
        auto fileInfo = bfsInfos[0];
        BOOST_CHECK(std::get<0>(fileInfo) == "t_test");
        BOOST_CHECK(std::get<1>(fileInfo) == executor::FS_TYPE_CONTRACT);
    }
    // simple append
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v2"});
        auto r2 = desc(number++, "t_test");
        TableInfoTuple tableInfo = {"id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));
    }
    // append not exist table
    {
        auto r1 = appendColumns(number++, "t_test2", {"v1", "v2"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int32_t)CODE_TABLE_NOT_EXIST));
    }
    // append duplicate field
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v3"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int32_t)CODE_TABLE_DUPLICATE_FIELD));
    }

    // append too long field
    {
        std::string longField = "0";
        for (int j = 0; j < USER_TABLE_FIELD_NAME_MAX_LENGTH; ++j)
        {
            longField += "0";
        }
        auto r1 = appendColumns(number++, "t_test", {"v3", longField});
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(insertTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "id1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // insert exist
    {
        auto r1 = insert(number++, "id1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_INSERT_KEY_EXIST)));
    }

    // insert too much value
    {
        auto r1 = insert(number++, "id2", {"test1", "test2", "test3"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // insert not enough value
    {
        auto r1 = insert(number++, "id3", {"test1"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string longKey = "0";
        for (int j = 0; j < USER_TABLE_KEY_VALUE_MAX_LENGTH; ++j)
        {
            longKey += "0";
        }
        auto r1 = insert(number++, longKey, {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string longValue = "0";
        for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
        {
            longValue += "0";
        }
        auto r1 = insert(number++, "id111", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert after append
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v2"});
        auto r2 = desc(number++, "t_test");
        TableInfoTuple tableInfo = {"id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));

        // good
        auto r3 = insert(number++, "id4", {"test1", "test2", "test3", "test4"}, callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));

        // insert too much value
        auto r4 =
            insert(number++, "id5", {"test1", "test2", "test3", "test4", "test5"}, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);

        // insert not enough value
        auto r5 = insert(number++, "id3", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(insertWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "id1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // insert exist
    {
        auto r1 = insert(number++, "id1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_INSERT_KEY_EXIST)));
    }

    // insert too much value
    {
        auto r1 = insert(number++, "id1", {"test1", "test2", "test3"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // insert not enough value
    {
        auto r1 = insert(number++, "id1", {"test1"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string longKey = "0";
        for (int j = 0; j < USER_TABLE_KEY_VALUE_MAX_LENGTH; ++j)
        {
            longKey += "0";
        }
        auto r1 = insert(number++, longKey, {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string longValue = "0";
        for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
        {
            longValue += "0";
        }
        auto r1 = insert(number++, "id1", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }
}

/// TODO: check limit
BOOST_AUTO_TEST_CASE(selectTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple select by key
    {
        auto r1 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }

    // select by key not exist
    {
        auto r1 = selectByKey(number++, "2", callAddress);
        EntryTuple entryTuple = {};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }
    for (int j = 3; j < 100; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string index = std::to_string(j);
        insert(number++, index, {"test" + index, "test" + index}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // simple select by condition
    {
        // lexicographical order， 90～99
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        std::vector<EntryTuple> entries;
        codec->decode(r1->data(), entries);
        BOOST_CHECK(entries.size() == 9);
    }

    {
        ConditionTuple cond1 = {0, "90"};
        auto r1 = count(number++, {cond1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(uint32_t(9)));
    }

    // select by condition， condition with undefined cmp
    {
        ConditionTuple cond1 = {5, "90"};
        LimitTuple limit = {0, 10};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = count(number++, {cond1}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select by condition， limit overflow
    {
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10000};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

/// TODO: check limit
BOOST_AUTO_TEST_CASE(selectWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple select by key
    {
        auto r1 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }

    // select by key not exist
    {
        auto r1 = selectByKey(number++, "2", callAddress);
        EntryTuple entryTuple = {};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }
    for (int j = 3; j < 100; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string index = std::to_string(j);
        insert(number++, index, {"test" + index, "test" + index}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // simple select by condition
    {
        // lexicographical order， 90～99
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        std::vector<EntryTuple> entries;
        codec->decode(r1->data(), entries);
        BOOST_CHECK(entries.size() == 9);
    }

    // select by condition， condition with undefined cmp
    {
        ConditionTuple cond1 = {5, "90"};
        LimitTuple limit = {0, 10};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select by condition， limit overflow
    {
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10000};
        auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

/// TODO: check limit
BOOST_AUTO_TEST_CASE(updateTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple update by key, modify 1 column
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"update", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));
    }

    // simple update by key, modify 2 columns
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        UpdateFieldTuple updateFieldTuple2 = {"item_id", "update2"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple2, updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"update1", "update2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));
    }

    // update by key not exist
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress, true);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::string longValue = "0";
    for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
    {
        longValue += "0";
    }

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    for (int j = 2; j < 100; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string index = std::to_string(j);
        insert(number++, index, {"test" + index, "test" + index}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // simple update by condition
    {
        // lexicographical order， 90～99
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10};
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(9)));

        // select
        auto r2 = selectByKey(number++, "98", callAddress);
        EntryTuple entryTuple = {"98", {"update1", "test98"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));

        // update second column
        UpdateFieldTuple updateFieldTuple2 = {"item_id", "update2"};
        auto r3 = updateByCondition(
            number++, {cond1}, limit, {updateFieldTuple1, updateFieldTuple2}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(9)));

        // select
        auto r4 = selectByKey(number++, "96", callAddress);
        EntryTuple entryTuple2 = {"96", {"update1", "update2"}};
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple2));
    }
}

/// TODO: check limit
BOOST_AUTO_TEST_CASE(updateWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple update by key, modify 1 column
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"update", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));
    }

    // simple update by key, modify 2 columns
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        UpdateFieldTuple updateFieldTuple2 = {"item_id", "update2"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple2, updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "1", callAddress);
        EntryTuple entryTuple = {"1", {"update1", "update2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));
    }

    // update by key not exist
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress, true);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::string longValue = "0";
    for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
    {
        longValue += "0";
    }

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    for (int j = 2; j < 100; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string index = std::to_string(j);
        insert(number++, index, {"test" + index, "test" + index}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // simple update by condition
    {
        // lexicographical order， 90～99
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10};
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update1"};
        auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(9)));

        // select
        auto r2 = selectByKey(number++, "98", callAddress);
        EntryTuple entryTuple = {"98", {"update1", "test98"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));

        // update second column
        UpdateFieldTuple updateFieldTuple2 = {"item_id", "update2"};
        auto r3 = updateByCondition(
            number++, {cond1}, limit, {updateFieldTuple1, updateFieldTuple2}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(9)));

        // select
        auto r4 = selectByKey(number++, "96", callAddress);
        EntryTuple entryTuple2 = {"96", {"update1", "update2"}};
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple2));
    }
}

/// TODO: check limit
BOOST_AUTO_TEST_CASE(removeTest)
{
    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test", "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple remove by key
    {
        auto r1 = removeByKey(number++, "1", callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "1", callAddress);
        // empty
        EntryTuple entryTuple = {};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));
    }

    // remove by key not exist
    {
        auto r1 = removeByKey(number++, "1", callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_REMOVE_KEY_NOT_EXIST)));
    }

    for (int j = 1; j < 100; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string index = std::to_string(j);
        insert(number++, index, {"test" + index, "test" + index}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // simple remove by condition
    {
        // lexicographical order， 90～99
        ConditionTuple cond1 = {0, "90"};
        LimitTuple limit = {0, 10};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(9)));

        // select
        auto r2 = selectByKey(number++, "98", callAddress);
        EntryTuple entryTuple = {};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));

        // select
        auto r3 = selectByKey(number++, "89", callAddress);
        EntryTuple entryTuple2 = {"89", {"test89", "test89"}};
        BOOST_CHECK(r3->data().toBytes() == codec->encode(entryTuple2));

        // remove again
        auto r4 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r4->data().toBytes() == codec->encode(int32_t(0)));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
