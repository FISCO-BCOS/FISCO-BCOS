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
 * @file TablePrecompiledV320Test.cpp
 * @author: kyonRay
 * @date 2022-04-13
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/TableManagerPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <algorithm>
#include <map>
#include <random>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class TableFactoryPrecompiledV320Fixture : public PrecompiledFixture
{
public:
    TableFactoryPrecompiledV320Fixture() { init(false); }

    void init(bool isWasm)
    {
        setIsWasm(isWasm, false, true);
        tableTestAddress = isWasm ? "/tables/t_test" : "420f853b49838bd3e9466c85a4cc3428c960dde2";
    }

    virtual ~TableFactoryPrecompiledV320Fixture() = default;

    ExecutionMessage::UniquePtr creatTable(protocol::BlockNumber _number,
        const std::string& tableName, const uint8_t keyOrder, const std::string& key,
        const std::vector<std::string>& value, const std::string& callAddress, int _errorCode = 0,
        bool errorInTableManager = false)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        TableInfoTupleV320 tableInfoTuple = std::make_tuple(keyOrder, key, value);
        bytes in = codec->encodeWithSig(
            "createTable(string,(uint8,string,string[]))", tableName, tableInfoTuple);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(100), 10000, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
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
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);
        // external call bfs
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call bfs success, callback to create
        result4->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        // create success, callback to precompiled
        result5->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(std::move(result5),
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("appendColumns(string,string[])", tableName, values);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(100), 10000, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
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

    ExecutionMessage::UniquePtr openTable(
        protocol::BlockNumber _number, std::string const& _path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("openTable(string)", _path);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("descWithKeyOrder(string)", tableName);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        EntryTuple entryTuple = {key, values};
        bytes in = codec->encodeWithSig("insert((string,string[]))", entryTuple);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("select(string)", key);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr selectByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in =
            codec->encodeWithSig("select((uint8,string,string)[],(uint32,uint32))", keyCond, limit);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr count(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("count((uint8,string,string)[])", keyCond);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr updateByKey(protocol::BlockNumber _number, const std::string& key,
        const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
        const std::string& callAddress, bool _isErrorInTable = false)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("update(string,(string,string)[])", key, _updateFields);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
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
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr updateByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& conditions, const LimitTuple& _limit,
        const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
        const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig(
            "update((uint8,string,string)[],(uint32,uint32),(string,string)[])", conditions, _limit,
            _updateFields);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        if (result2->status() != (int32_t)TransactionStatus::None)
        {
            commitBlock(_number);
            return result2;
        }

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
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
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in = codec->encodeWithSig("remove(string)", key);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    ExecutionMessage::UniquePtr removeByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_6_1_VERSION);
        bytes in =
            codec->encodeWithSig("remove((uint8,string,string)[],(uint32,uint32))", keyCond, limit);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        // get desc
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // get desc callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call precompiled
        commitBlock(_number);
        return result4;
    }

    std::string tableTestAddress;
    std::string sender;
};

struct ConditionNative
{
    ConditionNative() = default;
    ~ConditionNative() = default;
    void EQ(const std::string& value) { m_conditions.emplace_back(Comparator::EQ, value); }
    void NE(const std::string& value) { m_conditions.emplace_back(Comparator::NE, value); }
    // string compare, "2" > "12"
    void GT(const std::string& value) { m_conditions.emplace_back(Comparator::GT, value); }
    void GE(const std::string& value) { m_conditions.emplace_back(Comparator::GE, value); }
    // string compare, "12" < "2"
    void LT(const std::string& value) { m_conditions.emplace_back(Comparator::LT, value); }
    void LE(const std::string& value) { m_conditions.emplace_back(Comparator::LE, value); }
    void startsWith(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::STARTS_WITH, value);
    }
    void endsWith(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::ENDS_WITH, value);
    }
    void contains(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::CONTAINS, value);
    }

    bool isValid(const std::string_view& key) const
    {  // all conditions must be satisfied
        for (auto& cond : m_conditions)
        {  // conditions should few, so not parallel check for now
            switch (cond.cmp)
            {
            case Comparator::EQ:
                if (key != cond.value)
                {
                    return false;
                }
                break;
            case Comparator::NE:
                if (key == cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GT:
                if (key <= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GE:
                if (key < cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LT:
                if (key >= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LE:
                if (key > cond.value)
                {
                    return false;
                }
                break;
            case Comparator::STARTS_WITH:
                if (!key.starts_with(cond.value))
                {
                    return false;
                }
                break;
            case Comparator::ENDS_WITH:
                if (!key.ends_with(cond.value))
                {
                    return false;
                }
                break;
            case Comparator::CONTAINS:
                if (key.find(cond.value) == std::string::npos)
                {
                    return false;
                }
                break;
            default:
                // undefined Comparator
                break;
            }
        }
        return true;
    }

    enum class Comparator : uint8_t
    {
        GT = 0,
        GE = 1,
        LT = 2,
        LE = 3,
        EQ = 4,
        NE = 5,
        STARTS_WITH = 6,
        ENDS_WITH = 7,
        CONTAINS = 8
    };

    struct cond
    {
        cond(Comparator _cmp, const std::string& _value) : cmp(_cmp), value(_value) {}
        Comparator cmp;
        std::string value;
    };

    std::vector<cond> m_conditions;
};

class ConditionTest
{
public:
    ConditionTest(const std::string& field,
        std::function<void(
            const std::vector<ConditionTupleV320>& conds, std::vector<EntryTuple>& entries)>
            selectFunc)
      : m_field(field), m_selectFunc(selectFunc)
    {}
    ~ConditionTest() = default;

    void EQ(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::EQ, m_field, value);
        m_condition1.EQ(value);
    }
    void NE(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::NE, m_field, value);
        m_condition1.NE(value);
    }
    void GT(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::GT, m_field, value);
        m_condition1.GT(value);
    }
    void GE(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::GE, m_field, value);
        m_condition1.GE(value);
    }
    void LT(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::LT, m_field, value);
        m_condition1.LT(value);
    }
    void LE(const std::string& value)
    {
        m_condition2.emplace_back((uint8_t)storage::Condition::Comparator::LE, m_field, value);
        m_condition1.LE(value);
    }
    void startsWith(const std::string& value)
    {
        m_condition2.emplace_back(
            (uint8_t)storage::Condition::Comparator::STARTS_WITH, m_field, value);
        m_condition1.startsWith(value);
    }
    void endsWith(const std::string& value)
    {
        m_condition2.emplace_back(
            (uint8_t)storage::Condition::Comparator::ENDS_WITH, m_field, value);
        m_condition1.endsWith(value);
    }
    void contains(const std::string& value)
    {
        m_condition2.emplace_back(
            (uint8_t)storage::Condition::Comparator::CONTAINS, m_field, value);
        m_condition1.contains(value);
    }

    static std::vector<std::string> getKeys(
        const std::vector<std::string>& keys, const ConditionNative& cond)
    {
        std::vector<std::string> ret;
        for (auto& key : keys)
        {
            if (cond.isValid(key))
            {
                ret.push_back(key);
            }
        }
        return ret;
    }

    static bool checkResultEQ(
        const std::vector<std::string>& data1, const std::vector<EntryTuple>& data2)
    {
        if (data1.size() != data2.size())
        {
            return false;
        }
        for (size_t i = 0; i < data1.size(); ++i)
        {
            if (data1[i] != std::get<0>(data2[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool isEquivalent(const std::vector<std::string>& data, bool emptyRes = false)
    {
        auto ret1 = getKeys(data, m_condition1);
        std::vector<EntryTuple> entries;
        m_selectFunc(m_condition2, entries);
        if (emptyRes)
        {
            return checkResultEQ(ret1, entries) && ret1.empty();
        }
        return checkResultEQ(ret1, entries);
    }

private:
    std::string m_field;
    ConditionNative m_condition1;
    std::vector<ConditionTupleV320> m_condition2;
    std::function<void(
        const std::vector<ConditionTupleV320>& conds, std::vector<EntryTuple>& entries)>
        m_selectFunc;
};

static void generateRandomVector(
    uint32_t count, uint32_t _min, uint32_t _max, std::map<uint32_t, uint32_t>& res)
{
    static std::random_device rd;
    if (_min > _max || count > _max - _min + 1)
    {
        return;
    }
    std::vector<uint32_t> temp;
    temp.reserve(_max - _min + 1);
    for (uint32_t i = _min; i <= _max; i++)
    {
        temp.push_back(i);
    }
    std::shuffle(temp.begin(), temp.end(), std::default_random_engine(rd()));
    std::sort(temp.begin(), temp.begin() + count);
    size_t offset = res.size();
    for (uint32_t i = 0; i < count; i++)
    {
        res.insert({temp[i], i + offset});
    }
}

static void countTest(TableFactoryPrecompiledV320Fixture* fixture, const int INSERT_COUNT,
    const int INTERVAL, const int VALID_COUNT, const int VALID_COUNT_PER_SECTION,
    bcos::protocol::BlockNumber& number, const std::map<uint32_t, uint32_t>& randomSet)
{
    auto callAddress = fixture->tableTestAddress;

    // (<= && <= && ==) or (<= && <= && !=)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2);
        std::uniform_int_distribution<uint32_t> distribution2(
            randomSet.size() / 2, randomSet.size());
        uint32_t low = distribution1(gen);
        uint32_t high = distribution2(gen);
        uint32_t validCount = 0;
        std::string lowKey;
        std::string highKey;
        uint32_t counter = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            ++counter;
            if (counter == low)
            {
                validCount = iter->second;
                lowKey = std::to_string(iter->first);
            }
            if (counter == high)
            {
                validCount = iter->second - validCount + 1;
                highKey = std::to_string(iter->first);
                break;
            }
        }
        // lowKey <= key <= highKey && value == "yes"
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GE, "id", lowKey};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LE, "id", highKey};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        ConditionTupleV320 cond4 = {(uint8_t)storage::Condition::Comparator::NE, "value", "yes"};
        auto r1 = fixture->count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == validCount);

        // lowKey <= key <= highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low + 1;
        auto r2 = fixture->count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        fixture->codec->decode(r2->data(), countRes);
        BOOST_CHECK(countRes == total - validCount);
    }

    // (< && < && ==) or (< && < && !=)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2 - 1);
        std::uniform_int_distribution<uint32_t> distribution2(
            randomSet.size() / 2 + 1, randomSet.size());
        uint32_t low = distribution1(gen);
        uint32_t high = distribution2(gen);
        uint32_t validCount = 0;
        std::string lowKey;
        std::string highKey;
        uint32_t counter = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            ++counter;
            if (counter == low)
            {
                validCount = iter->second;
                lowKey = std::to_string(iter->first);
            }
            if (counter == high)
            {
                validCount = iter->second - validCount - 1;
                highKey = std::to_string(iter->first);
                break;
            }
        }

        // lowKey < key < highKey && value == "yes"
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GT, "id", lowKey};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LT, "id", highKey};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        ConditionTupleV320 cond4 = {(uint8_t)storage::Condition::Comparator::NE, "value", "yes"};
        auto r1 = fixture->count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == validCount);

        // lowKey < key < highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low - 1;
        auto r2 = fixture->count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        fixture->codec->decode(r2->data(), countRes);
        BOOST_CHECK(countRes == total - validCount);
    }

    // 0 <= key <= 987
    {
        uint32_t low = 0;
        uint32_t high = 987;
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(low)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LE, "id", std::to_string(high)};
        auto r1 = fixture->count(number++, {cond1, cond2}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == high - low + 1);
    }

    // value == "yes"
    {
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        auto r1 = fixture->count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == VALID_COUNT);
    }

    // value == "no"
    {
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "no"};
        auto r1 = fixture->count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == INSERT_COUNT - VALID_COUNT);
    }

    // The index of condition out of range
    {
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        // index out of range
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "idx", "yes"};
        auto r1 = fixture->count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // empty condition
    {
        auto r1 = fixture->count(number++, {}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {10, "value", "yes"};
        auto r1 = fixture->count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // count, non numeric key
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "01"};
        auto r1 = fixture->count(number++, {cond1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "aa"};
        auto r2 = fixture->count(number++, {cond2}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "9223372036854775808"};
        auto r3 = fixture->count(number++, {cond3}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MIN - 1
        ConditionTupleV320 cond4 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "-9223372036854775809"};
        auto r4 = fixture->count(number++, {cond4}, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // count, negative key
    {
        fixture->insert(number++, "-10", {"no"}, callAddress);
        fixture->insert(number++, "-9223372036854775808", {"no"}, callAddress);

        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GE, "id", "-10"};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", "-9223372036854775808"};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::LT, "id", "50"};

        auto r1 = fixture->count(number++, {cond1, cond3}, callAddress);
        uint32_t countRes = 0;
        fixture->codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == 51);

        auto r2 = fixture->count(number++, {cond2, cond3}, callAddress);
        countRes = 0;
        fixture->codec->decode(r2->data(), countRes);
        BOOST_TEST(countRes == 52);
    }
}


static void selectByConditionTest(TableFactoryPrecompiledV320Fixture* fixture,
    const int INSERT_COUNT, const int INTERVAL, const int VALID_COUNT,
    const int VALID_COUNT_PER_SECTION, bcos::protocol::BlockNumber& number,
    const std::map<uint32_t, uint32_t>& randomSet)
{
    auto callAddress = fixture->tableTestAddress;
    // select by condition——check limit and count
    {
        uint32_t limitOffset = 0;
        uint32_t limitCount = 50;
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        LimitTuple limit = {limitOffset, limitCount};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        std::vector<EntryTuple> entries;
        fixture->codec->decode(r1->data(), entries);
        uint32_t count = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            EntryTuple& entry = entries[i];
            uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
            auto iter = randomSet.find(key);
            if (iter == randomSet.end() || iter->second != i + limitOffset)
                break;
            ++count;
        }
        BOOST_CHECK(entries.size() == limitCount && count == limitCount);
    }

    {
        uint32_t limitOffset = 10;
        uint32_t limitCount = 75;
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        LimitTuple limit = {limitOffset, limitCount};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        std::vector<EntryTuple> entries;
        fixture->codec->decode(r1->data(), entries);
        uint32_t count = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            EntryTuple& entry = entries[i];
            uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
            auto iter = randomSet.find(key);
            if (iter == randomSet.end() || iter->second != i + limitOffset)
                break;
            ++count;
        }

        BOOST_CHECK(entries.size() == limitCount && count == limitCount);
    }

    {
        uint32_t limitOffset = 461;
        uint32_t limitCount = 75;
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        LimitTuple limit = {limitOffset, limitCount};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        std::vector<EntryTuple> entries;
        fixture->codec->decode(r1->data(), entries);
        uint32_t count = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            EntryTuple& entry = entries[i];
            uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
            auto iter = randomSet.find(key);
            if (iter == randomSet.end() || iter->second != i + limitOffset)
                break;
            ++count;
        }

        BOOST_CHECK(
            entries.size() == (VALID_COUNT - limitOffset) && count == (VALID_COUNT - limitOffset));
    }

    // select by condition limitCount < USER_TABLE_MIN_LIMIT_COUNT
    {
        uint32_t limitOffset = 0;
        uint32_t limitCount = 49;
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        LimitTuple limit = {limitOffset, limitCount};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        std::vector<EntryTuple> entries;
        fixture->codec->decode(r1->data(), entries);
        uint32_t count = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            EntryTuple& entry = entries[i];
            uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
            auto iter = randomSet.find(key);
            if (iter == randomSet.end() || iter->second != i + limitOffset)
                break;
            ++count;
        }

        BOOST_CHECK(entries.size() == limitCount && count == limitCount);
    }

    // select by condition limitCount == 0
    {
        uint32_t limitOffset = 0;
        uint32_t limitCount = 0;
        // lexicographical order， 1～INSERT_COUNT
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
        LimitTuple limit = {limitOffset, limitCount};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        std::vector<EntryTuple> entries;
        fixture->codec->decode(r1->data(), entries);
        uint32_t count = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            EntryTuple& entry = entries[i];
            uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
            auto iter = randomSet.find(key);
            if (iter == randomSet.end() || iter->second != i + limitOffset)
                break;
            ++count;
        }

        BOOST_CHECK(entries.size() == limitCount && count == limitCount);
    }

    {
        // check not use key condition
        uint32_t count1 = 0;
        {
            uint32_t limitOffset = 461;
            uint32_t limitCount = 75;
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
            LimitTuple limit = {limitOffset, limitCount};
            auto r1 =
                fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
            std::vector<EntryTuple> entries;
            fixture->codec->decode(r1->data(), entries);

            for (size_t i = 0; i < entries.size(); ++i)
            {
                EntryTuple& entry = entries[i];
                uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
                auto iter = randomSet.find(key);
                if (iter == randomSet.end() || iter->second != i + limitOffset)
                    break;
                ++count1;
            }
            BOOST_CHECK(entries.size() == (VALID_COUNT - limitOffset) &&
                        count1 == (VALID_COUNT - limitOffset));
        }
        uint32_t count2 = 0;
        {
            uint32_t limitOffset = 461;
            uint32_t limitCount = 75;
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::EQ, "value", "yes"};
            LimitTuple limit = {limitOffset, limitCount};
            auto r1 = fixture->selectByCondition(number++, {cond3}, limit, callAddress);
            std::vector<EntryTuple> entries;
            fixture->codec->decode(r1->data(), entries);

            for (size_t i = 0; i < entries.size(); ++i)
            {
                EntryTuple& entry = entries[i];
                uint32_t key = boost::lexical_cast<uint32_t>(std::get<0>(entry));
                auto iter = randomSet.find(key);
                if (iter == randomSet.end() || iter->second != i + limitOffset)
                    break;
                ++count2;
            }
            BOOST_CHECK(entries.size() == (VALID_COUNT - limitOffset) &&
                        count2 == (VALID_COUNT - limitOffset));
        }
        BOOST_CHECK(count1 == count2);
    }

    // empty condition
    {
        LimitTuple limit = {0, 10};
        auto r1 = fixture->selectByCondition(number++, {}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = fixture->count(number++, {}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {100, "id", "90"};
        LimitTuple limit = {0, 10};
        auto r1 = fixture->selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // limit overflow
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "90"};
        LimitTuple limit = {0, 10000};
        auto r1 = fixture->selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // The index of condition out of range
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        // index out of range  0 <= idx <= 1
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "idx", "yes"};
        auto r1 = fixture->selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select, non numeric key
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "01"};
        auto r1 = fixture->selectByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "aa"};
        auto r2 = fixture->selectByCondition(number++, {cond2}, limit, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "9223372036854775808"};
        auto r3 = fixture->selectByCondition(number++, {cond3}, limit, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MIN - 1
        ConditionTupleV320 cond4 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "-9223372036854775809"};
        auto r4 = fixture->selectByCondition(number++, {cond4}, limit, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select, negative key
    {
        LimitTuple limit = {0, 100};
        fixture->insert(number++, "-10", {"no"}, callAddress);
        fixture->insert(number++, "-9223372036854775808", {"no"}, callAddress);

        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GE, "id", "-10"};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", "-9223372036854775808"};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::LT, "id", "50"};
        ConditionTupleV320 cond4 = {(uint8_t)storage::Condition::Comparator::NE, "value", "xx"};
        std::vector<std::string> target1 = {"-10"};
        std::vector<std::string> target2 = {"-9223372036854775808", "-10"};

        for (int i = 0; i < 50; ++i)
        {
            target1.push_back(std::to_string(i));
            target2.push_back(std::to_string(i));
        }

        auto checkFunc = [](std::vector<std::string>& target, std::vector<EntryTuple> entries) {
            if (target.size() != entries.size())
                return false;
            for (size_t i = 0; i < target.size(); ++i)
            {
                if (target[i] != std::get<0>(entries[i]))
                {
                    return false;
                }
            }
            return true;
        };

        {
            std::vector<EntryTuple> entries1;
            auto r1 = fixture->selectByCondition(number++, {cond1, cond3}, limit, callAddress);
            fixture->codec->decode(r1->data(), entries1);
            BOOST_CHECK(checkFunc(target1, entries1));

            std::vector<EntryTuple> entries2;
            auto r2 = fixture->selectByCondition(number++, {cond2, cond3}, limit, callAddress);
            fixture->codec->decode(r2->data(), entries2);
            BOOST_CHECK(checkFunc(target2, entries2));
        }

        // use value condition
        {
            std::vector<EntryTuple> entries1;
            auto r1 =
                fixture->selectByCondition(number++, {cond1, cond3, cond4}, limit, callAddress);
            fixture->codec->decode(r1->data(), entries1);
            BOOST_CHECK(checkFunc(target1, entries1));

            std::vector<EntryTuple> entries2;
            auto r2 =
                fixture->selectByCondition(number++, {cond2, cond3, cond4}, limit, callAddress);
            fixture->codec->decode(r2->data(), entries2);
            BOOST_CHECK(checkFunc(target2, entries2));
        }
    }
}

static void updateByConditionTest(TableFactoryPrecompiledV320Fixture* fixture,
    const int INSERT_COUNT, const int INTERVAL, const int VALID_COUNT,
    const int VALID_COUNT_PER_SECTION, bcos::protocol::BlockNumber& number,
    const std::map<uint32_t, uint32_t>& randomSet)
{
    auto callAddress = fixture->tableTestAddress;
    {
        auto updateFunc = [fixture, &number, &callAddress](uint32_t low, uint32_t high,
                              uint32_t offset, uint32_t count, const std::string& target,
                              const std::string& value) {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(low)};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(high)};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::EQ, "value", value};
            LimitTuple limit = {offset, count};
            UpdateFieldTuple updateFieldTuple1 = {"value", target};
            auto r1 = fixture->updateByCondition(
                number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
            int32_t affectRows = 0;
            fixture->codec->decode(r1->data(), affectRows);
            return affectRows;
        };

        auto countFunc = [fixture, &number, &callAddress](const std::string& value) {
            ConditionTupleV320 cond = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
            auto r1 = fixture->count(number++, {cond}, callAddress);
            uint32_t rows = 0;
            fixture->codec->decode(r1->data(), rows);
            return rows;
        };
        int32_t countBeforeUpdate = countFunc("yes");
        // update value = "update" where (key >= 500 && key < 1500) && (value == "yes")
        int32_t affectRows1 = updateFunc(500, 1500, 126, 20, "update", "yes");
        int32_t countAfterUpdate = countFunc("update");
        // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
        int32_t affectRows2 = updateFunc(0, INSERT_COUNT, 0, VALID_COUNT, "yes", "update");
        int32_t countAfterRecover = countFunc("yes");
        BOOST_CHECK(affectRows1 == countAfterUpdate && affectRows1 == affectRows2 &&
                    affectRows1 == 20 && countBeforeUpdate == countAfterRecover &&
                    countBeforeUpdate == VALID_COUNT);
    }

    // limitcount == 0
    {
        auto updateFunc = [fixture, &number, &callAddress](uint32_t low, uint32_t high,
                              uint32_t offset, uint32_t count, const std::string& target,
                              const std::string& value) {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(low)};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(high)};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::EQ, "value", value};
            LimitTuple limit = {offset, count};
            UpdateFieldTuple updateFieldTuple1 = {"value", target};
            auto r1 = fixture->updateByCondition(
                number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
            int32_t affectRows = 0;
            fixture->codec->decode(r1->data(), affectRows);
            return affectRows;
        };

        auto countFunc = [fixture, &number, &callAddress](const std::string& value) {
            ConditionTupleV320 cond = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
            auto r1 = fixture->count(number++, {cond}, callAddress);
            uint32_t rows = 0;
            fixture->codec->decode(r1->data(), rows);
            return rows;
        };
        int32_t countBeforeUpdate = countFunc("yes");
        // update value = "update" where (key >= 5000 && key < 6000) && (value == "yes")
        int32_t affectRows1 = updateFunc(500, 1500, 0, 0, "update", "yes");
        int32_t countAfterUpdate = countFunc("update");
        // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
        int32_t affectRows2 = updateFunc(0, INSERT_COUNT, 0, 0, "yes", "update");
        int32_t countAfterRecover = countFunc("yes");
        BOOST_CHECK(affectRows1 == countAfterUpdate && affectRows1 == affectRows2 &&
                    affectRows1 == 0 && countBeforeUpdate == countAfterRecover &&
                    countBeforeUpdate == VALID_COUNT);
    }

    // empty condition
    {
        LimitTuple limit = {0, 10};
        UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
        auto r1 = fixture->updateByCondition(number++, {}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {100, "id", "90"};
        LimitTuple limit = {0, 10};
        UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
        auto r1 =
            fixture->updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // limit overflow
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "90"};
        LimitTuple limit = {0, 10000};
        UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
        auto r1 =
            fixture->updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // The index of condition out of range
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        // index out of range  0 <= idx <= 1
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "idx", "yes"};
        UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
        auto r1 = fixture->updateByCondition(
            number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // update, non numeric key
    {
        UpdateFieldTuple updateFieldTuple = {"value", "update"};
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "01"};
        auto r1 =
            fixture->updateByCondition(number++, {cond1}, limit, {updateFieldTuple}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "aa"};
        auto r2 =
            fixture->updateByCondition(number++, {cond2}, limit, {updateFieldTuple}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "9223372036854775808"};
        auto r3 =
            fixture->updateByCondition(number++, {cond3}, limit, {updateFieldTuple}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MIN - 1
        ConditionTupleV320 cond4 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "-9223372036854775809"};
        auto r4 =
            fixture->updateByCondition(number++, {cond4}, limit, {updateFieldTuple}, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // update, negative key
    {
        LimitTuple limit = {0, 100};
        UpdateFieldTuple updateFieldTuple = {"value", "updatexx"};
        fixture->insert(number++, "-10", {"no"}, callAddress);
        fixture->insert(number++, "-100", {"no"}, callAddress);
        fixture->insert(number++, "-1000", {"no"}, callAddress);
        fixture->insert(number++, "-9223372036854775808", {"no"}, callAddress);

        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", "-9223372036854775808"};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LE, "id", "-10"};
        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "value", "updatexx"};

        auto r1 = fixture->updateByCondition(
            number++, {cond1, cond2}, limit, {updateFieldTuple}, callAddress);
        uint32_t affectRows = 0;
        fixture->codec->decode(r1->data(), affectRows);
        BOOST_CHECK(affectRows == 4);

        std::vector<EntryTuple> entries;
        auto r2 = fixture->selectByCondition(number++, {cond3}, limit, callAddress);
        fixture->codec->decode(r2->data(), entries);
        BOOST_CHECK(std::get<0>(entries[0]) == "-9223372036854775808");
        BOOST_CHECK(std::get<0>(entries[1]) == "-1000");
        BOOST_CHECK(std::get<0>(entries[2]) == "-100");
        BOOST_CHECK(std::get<0>(entries[3]) == "-10");
    }
}

BOOST_FIXTURE_TEST_SUITE(precompiledTableTestV320, TableFactoryPrecompiledV320Fixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        BOOST_CHECK(std::get<1>(fileInfo) == tool::FS_TYPE_LINK);
    }

    // createTable exist
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress,
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
            creatTable(number++, errorStr, 0, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", 0, errorStr, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", 0, "id", {errorStr}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(
            number++, errorStr2, 0, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", 0, errorStr2, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", 0, "id", {errorStr2}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // check trim create table params
    {
        auto r1 = creatTable(number++, "t_test123", 0, " id ", {" item_name ", " item_id "},
            "420f853b49838bd3e9466c85a4cc3428c960dde3");
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));
        auto r2 = desc(number++, "t_test123");
        TableInfoTupleV320 t = {0, "id", {"item_name", "item_id"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(t));
    }

    // undefined keyOrder
    {
        auto r1 = creatTable(
            number++, "t_test456", 2, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(createTableWasmTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        BOOST_CHECK(std::get<1>(fileInfo) == tool::FS_TYPE_LINK);
    }

    // createTable exist
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress,
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
            creatTable(number++, errorStr, 0, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", 0, errorStr, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", 0, "id", {errorStr}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatTable(
            number++, errorStr2, 0, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatTable(
            number++, "t_test", 0, errorStr2, {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatTable(number++, "t_test", 0, "id", {errorStr2}, callAddress, 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // check trim create table params
    {
        auto r1 = creatTable(number++, "t_test123", 0, " id ", {" item_name ", " item_id "},
            "420f853b49838bd3e9466c85a4cc3428c960dde3");
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));
        auto r2 = desc(number++, "t_test123");
        TableInfoTupleV320 t = {0, "id", {"item_name", "item_id"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(t));
    }

    // undefined keyOrder
    {
        auto r1 = creatTable(
            number++, "t_test456", 2, "id", {"item_name", "item_id"}, callAddress, 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(insertLexicographicOrderTest)
{
    // Lexicographic Order
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        std::stringstream ss;
        ss << std::setw(USER_TABLE_KEY_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longKey = ss.str();
        auto r1 = insert(number++, longKey, {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::stringstream ss;
        ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longValue = ss.str();
        auto r1 = insert(number++, "id111", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert after append
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v2"});
        auto r2 = desc(number++, "t_test");
        TableInfoTupleV320 tableInfo = {0, "id", {"item_name", "item_id", "v1", "v2"}};
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

// Numerical Order
BOOST_AUTO_TEST_CASE(insertNumericalOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_1", 1, "id", {"item_name", "item_id"}, callAddress);
    }
    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }
    // insert exist
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_INSERT_KEY_EXIST)));
    }
    // insert too much value
    {
        auto r1 = insert(number++, "2", {"test1", "test2", "test3"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert not enough value
    {
        auto r1 = insert(number++, "3", {"test1"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert a non numeric key
    {
        auto r1 = insert(number++, "03", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = insert(number++, "aa", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = insert(number++, "9223372036854775808", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert negative key
    {
        auto r1 = insert(number++, "-10", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        // LONG_MIN
        auto r2 = insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r2->data().toBytes() == codec->encode(int32_t(1)));

        // LONG_MIN - 1
        auto r3 = insert(number++, "-9223372036854775809", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert too long value
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::stringstream ss;
        ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longValue = ss.str();
        auto r1 = insert(number++, "3", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }
    // insert after append
    {
        auto r1 = appendColumns(number++, "t_test_1", {"v1", "v2"});
        auto r2 = desc(number++, "t_test_1");
        TableInfoTupleV320 tableInfo = {1, "id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));
        // good
        auto r3 = insert(number++, "4", {"test1", "test2", "test3", "test4"}, callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        // insert too much value
        auto r4 = insert(number++, "5", {"test1", "test2", "test3", "test4", "test5"}, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
        // insert not enough value
        auto r5 = insert(number++, "6", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(insertLexicographicOrderWasmTest)
{
    // Lexicographic Order
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        std::stringstream ss;
        ss << std::setw(USER_TABLE_KEY_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longKey = ss.str();
        auto r1 = insert(number++, longKey, {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert too long key
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::stringstream ss;
        ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longValue = ss.str();
        auto r1 = insert(number++, "id111", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // insert after append
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v2"});
        auto r2 = desc(number++, "t_test");
        TableInfoTupleV320 tableInfo = {0, "id", {"item_name", "item_id", "v1", "v2"}};
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

BOOST_AUTO_TEST_CASE(insertNumericalOrdeWasmTest)
{
    // Numerical Order
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_1", 1, "id", {"item_name", "item_id"}, callAddress);
    }
    // simple insert
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }
    // insert exist
    {
        auto r1 = insert(number++, "1", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_INSERT_KEY_EXIST)));
    }
    // insert too much value
    {
        auto r1 = insert(number++, "2", {"test1", "test2", "test3"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert not enough value
    {
        auto r1 = insert(number++, "3", {"test1"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert a non numeric key
    {
        auto r1 = insert(number++, "03", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = insert(number++, "aa", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = insert(number++, "9223372036854775808", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert negative key
    {
        auto r1 = insert(number++, "-10", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        // LONG_MIN
        auto r2 = insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r2->data().toBytes() == codec->encode(int32_t(1)));

        // LONG_MIN - 1
        auto r3 = insert(number++, "-9223372036854775809", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    // insert too long value
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::stringstream ss;
        ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << '0';
        std::string longValue = ss.str();
        auto r1 = insert(number++, "3", {"test1", longValue}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }
    // insert after append
    {
        auto r1 = appendColumns(number++, "t_test_1", {"v1", "v2"});
        auto r2 = desc(number++, "t_test_1");
        TableInfoTupleV320 tableInfo = {1, "id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));
        // good
        auto r3 = insert(number++, "4", {"test1", "test2", "test3", "test4"}, callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        // insert too much value
        auto r4 = insert(number++, "5", {"test1", "test2", "test3", "test4", "test5"}, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
        // insert not enough value
        auto r5 = insert(number++, "6", {"test1", "test2"}, callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(selectLexicographicOrderTest)
{
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::right << _num;
        return stream.str();
    };
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, fillZero(1), {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple select by key
    {
        auto r1 = selectByKey(number++, fillZero(1), callAddress);
        EntryTuple entryTuple = {fillZero(1), {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }

    // select by key not exist
    {
        auto r1 = selectByKey(number++, fillZero(2), callAddress);
        EntryTuple entryTuple = {};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }
}

BOOST_AUTO_TEST_CASE(selectNumericalOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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

    // select a non numeric key
    {
        auto r1 = selectByKey(number++, "01", callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = selectByKey(number++, "aa", callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = selectByKey(number++, "9223372036854775808", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);

        auto r1 = selectByKey(number++, "-10", callAddress);
        EntryTuple entryTuple1 = {"-10", {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple1));

        auto r2 = selectByKey(number++, "-9223372036854775808", callAddress);
        EntryTuple entryTuple2 = {"-9223372036854775808", {"test1", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple2));

        // LONG_MIN - 1
        auto r3 = selectByKey(number++, "-9223372036854775809", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(selectLexicographicOrderWasmTest)
{
    init(true);
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::right << _num;
        return stream.str();
    };
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
    }

    // simple insert
    {
        auto r1 = insert(number++, fillZero(1), {"test1", "test2"}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
    }

    // simple select by key
    {
        auto r1 = selectByKey(number++, fillZero(1), callAddress);
        EntryTuple entryTuple = {fillZero(1), {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }

    // select by key not exist
    {
        auto r1 = selectByKey(number++, fillZero(2), callAddress);
        EntryTuple entryTuple = {};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple));
    }
}

BOOST_AUTO_TEST_CASE(selectNumericalOrderWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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

    // select a non numeric key
    {
        auto r1 = selectByKey(number++, "01", callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = selectByKey(number++, "aa", callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = selectByKey(number++, "9223372036854775808", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // select negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);

        auto r1 = selectByKey(number++, "-10", callAddress);
        EntryTuple entryTuple1 = {"-10", {"test1", "test2"}};
        BOOST_CHECK(r1->data().toBytes() == codec->encode(entryTuple1));

        auto r2 = selectByKey(number++, "-9223372036854775808", callAddress);
        EntryTuple entryTuple2 = {"-9223372036854775808", {"test1", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple2));

        // LONG_MIN - 1
        auto r3 = selectByKey(number++, "-9223372036854775809", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(updateLexicographicOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::stringstream ss;
    ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << "0";
    std::string longValue = ss.str();

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }
}

BOOST_AUTO_TEST_CASE(updateNumericalOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::stringstream ss;
    ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << "0";
    std::string longValue = ss.str();

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // update, non numeric key
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "01", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = updateByKey(number++, "aa", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = updateByKey(number++, "9223372036854775808", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // update, negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);

        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "-10", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "-10", callAddress);
        EntryTuple entryTuple1 = {"-10", {"update", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple1));

        auto r3 = updateByKey(number++, "-9223372036854775808", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        auto r4 = selectByKey(number++, "-9223372036854775808", callAddress);
        EntryTuple entryTuple2 = {"-9223372036854775808", {"update", "test2"}};
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple2));

        // LONG_MIN - 1
        auto r5 = updateByKey(number++, "-9223372036854775809", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(updateLexicographicOrderWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::stringstream ss;
    ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << "0";
    std::string longValue = ss.str();

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }
}

BOOST_AUTO_TEST_CASE(updateNumericalOrderWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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
        auto r1 = updateByKey(number++, "2", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
    }

    // update by key, index overflow
    {
        UpdateFieldTuple updateFieldTuple1 = {"errorField", "update1"};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::stringstream ss;
    ss << std::setw(USER_TABLE_FIELD_VALUE_MAX_LENGTH + 1) << std::setfill('0') << "0";
    std::string longValue = ss.str();

    // update by key, value overflow
    {
        boost::log::core::get()->set_logging_enabled(false);
        UpdateFieldTuple updateFieldTuple1 = {"item_name", longValue};
        auto r1 = updateByKey(number++, "1", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // update, non numeric key
    {
        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "01", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = updateByKey(number++, "aa", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = updateByKey(number++, "9223372036854775808", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // update, negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);

        UpdateFieldTuple updateFieldTuple1 = {"item_name", "update"};
        auto r1 = updateByKey(number++, "-10", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "-10", callAddress);
        EntryTuple entryTuple1 = {"-10", {"update", "test2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple1));

        auto r3 = updateByKey(number++, "-9223372036854775808", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        auto r4 = selectByKey(number++, "-9223372036854775808", callAddress);
        EntryTuple entryTuple2 = {"-9223372036854775808", {"update", "test2"}};
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple2));

        // LONG_MIN - 1
        auto r5 = updateByKey(number++, "-9223372036854775809", {updateFieldTuple1}, callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(removeLexicographicOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
}

BOOST_AUTO_TEST_CASE(removeNumericalOrderTest)
{
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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

    // remove, non numeric key
    {
        auto r1 = removeByKey(number++, "01", callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = removeByKey(number++, "aa", callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = removeByKey(number++, "9223372036854775808", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);
        EntryTuple entryTuple = {};

        auto r1 = removeByKey(number++, "-10", callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "-10", callAddress);
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));

        auto r3 = removeByKey(number++, "-9223372036854775808", callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        auto r4 = selectByKey(number++, "-9223372036854775808", callAddress);
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple));

        // LONG_MIN - 1
        auto r5 = removeByKey(number++, "-9223372036854775809", callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(removeLexicographicOrderWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 0, "id", {"item_name", "item_id"}, callAddress);
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
}

BOOST_AUTO_TEST_CASE(removeNumericalOrderWasmTest)
{
    init(true);
    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test", 1, "id", {"item_name", "item_id"}, callAddress);
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

    // remove, non numeric key
    {
        auto r1 = removeByKey(number++, "01", callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = removeByKey(number++, "aa", callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MAX + 1
        auto r3 = removeByKey(number++, "9223372036854775808", callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, negative key
    {
        insert(number++, "-10", {"test1", "test2"}, callAddress);
        insert(number++, "-9223372036854775808", {"test1", "test2"}, callAddress);
        EntryTuple entryTuple = {};

        auto r1 = removeByKey(number++, "-10", callAddress);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(1)));
        auto r2 = selectByKey(number++, "-10", callAddress);
        BOOST_CHECK(r2->data().toBytes() == codec->encode(entryTuple));

        auto r3 = removeByKey(number++, "-9223372036854775808", callAddress);
        BOOST_CHECK(r3->data().toBytes() == codec->encode(int32_t(1)));
        auto r4 = selectByKey(number++, "-9223372036854775808", callAddress);
        BOOST_CHECK(r4->data().toBytes() == codec->encode(entryTuple));

        // LONG_MIN - 1
        auto r5 = removeByKey(number++, "-9223372036854775809", callAddress);
        BOOST_CHECK(r5->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(tableConditionOP)
{
    const int INSERT_COUNT = 2000;
    const int INTERVAL = 500;
    const int VALID_COUNT = 500;
    const int VALID_COUNT_PER_SECTION = VALID_COUNT / (INSERT_COUNT / INTERVAL);

    bcos::protocol::BlockNumber number = 1;
    auto callAddress = tableTestAddress;
    {
        creatTable(number++, "t_test_condv320", 1, "id", {"value"}, callAddress);
    }

    // prepare data
    std::map<uint32_t, uint32_t> randomSet;
    int start = 0;
    int end = INTERVAL - 1;
    for (int i = 0; i < INSERT_COUNT / INTERVAL; i++)
    {
        generateRandomVector(VALID_COUNT_PER_SECTION, start, end, randomSet);
        start += INTERVAL;
        end += INTERVAL;
    }

    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string value = "no";
        if (randomSet.contains(j))
        {
            value = "yes";
        }
        insert(number++, std::to_string(j), {value}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    countTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
    selectByConditionTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
    updateByConditionTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
}

BOOST_AUTO_TEST_CASE(tableWasmConditionOP)
{
    init(true);
    const int INSERT_COUNT = 2000;
    const int INTERVAL = 500;
    const int VALID_COUNT = 500;
    const int VALID_COUNT_PER_SECTION = VALID_COUNT / (INSERT_COUNT / INTERVAL);

    bcos::protocol::BlockNumber number = 1;
    auto callAddress = tableTestAddress;
    {
        creatTable(number++, "t_test_condv320", 1, "id", {"value"}, callAddress);
    }

    // prepare data
    std::map<uint32_t, uint32_t> randomSet;
    int start = 0;
    int end = INTERVAL - 1;
    for (int i = 0; i < INSERT_COUNT / INTERVAL; i++)
    {
        generateRandomVector(VALID_COUNT_PER_SECTION, start, end, randomSet);
        start += INTERVAL;
        end += INTERVAL;
    }

    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string value = "no";
        if (randomSet.contains(j))
        {
            value = "yes";
        }
        insert(number++, std::to_string(j), {value}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    countTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
    selectByConditionTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
    updateByConditionTest(
        this, INSERT_COUNT, INTERVAL, VALID_COUNT, VALID_COUNT_PER_SECTION, number, randomSet);
}

BOOST_AUTO_TEST_CASE(removeByCondTest)
{
    const int INSERT_COUNT = 1000;
    const int INTERVAL = 500;
    const int VALID_COUNT = 500;
    const int VALID_COUNT_PER_SECTION = VALID_COUNT / (INSERT_COUNT / INTERVAL);

    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320_remove", 1, "id", {"value"}, callAddress);
    }

    // prepare data
    std::map<uint32_t, uint32_t> randomSet;
    int start = 0;
    int end = INTERVAL - 1;
    for (int i = 0; i < INSERT_COUNT / INTERVAL; i++)
    {
        generateRandomVector(VALID_COUNT_PER_SECTION, start, end, randomSet);
        start += INTERVAL;
        end += INTERVAL;
    }

    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string value = "no";
        if (randomSet.contains(j))
        {
            value = "yes";
        }
        insert(number++, std::to_string(j), {value}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    auto recoverFunc = [this, &number, &callAddress](const std::set<std::string>& removed) {
        for (auto& iter : removed)
        {
            boost::log::core::get()->set_logging_enabled(false);
            insert(number++, iter, {"yes"}, callAddress);
            boost::log::core::get()->set_logging_enabled(true);
        }
    };

    auto removeFunc = [this, &number, &callAddress](const std::string& low, const std::string& high,
                          uint32_t offset, uint32_t count, const std::string& value) {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GE, "id", low};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LT, "id", high};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
        LimitTuple limit = {offset, count};
        auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        int32_t removedRows = 0;
        codec->decode(r1->data(), removedRows);
        return removedRows;
    };

    auto selectFunc = [this, &number, &callAddress](
                          const std::string& value, std::vector<EntryTuple>& entries) {
        ConditionTupleV320 cond = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
        LimitTuple limit = {0, 500};
        auto r1 = selectByCondition(number++, {cond}, limit, callAddress);
        codec->decode(r1->data(), entries);
    };

    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 100;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
        // recover data
        recoverFunc(removedSet);
    }

    // limitCount == 0
    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 0;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
    }

    // limitCount < USER_TABLE_MIN_LIMIT_COUNT
    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 49;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
    }

    // empty condition
    {
        LimitTuple limit = {0, 10};
        auto r1 = removeByCondition(number++, {}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {100, "id", "90"};
        LimitTuple limit = {0, 10};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // limit overflow
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "90"};
        LimitTuple limit = {0, 10000};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // The index of condition out of range
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        // index out of range  0 <= idx <= 1
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "idx", "yes"};
        auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, non numeric key
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "01"};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "aa"};
        auto r2 = removeByCondition(number++, {cond2}, limit, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "9223372036854775808"};
        auto r3 = removeByCondition(number++, {cond3}, limit, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MIN - 1
        ConditionTupleV320 cond4 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "-9223372036854775809"};
        auto r4 = removeByCondition(number++, {cond4}, limit, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, negative key
    {
        LimitTuple limit = {0, 100};
        insert(number++, "-10", {"removexx"}, callAddress);
        insert(number++, "-100", {"removexx"}, callAddress);
        insert(number++, "-1000", {"removexx"}, callAddress);
        insert(number++, "-9223372036854775808", {"removexx"}, callAddress);

        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", "-9223372036854775808"};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LE, "id", "-10"};
        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "value", "removexx"};

        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == 4);

        auto r2 = removeByCondition(number++, {cond1, cond2}, limit, callAddress);
        uint32_t removedRows = 0;
        codec->decode(r2->data(), removedRows);
        BOOST_CHECK(removedRows == 4);

        auto r3 = count(number++, {cond3}, callAddress);
        countRes = 0;
        codec->decode(r3->data(), countRes);
        BOOST_CHECK(countRes == 0);
    }
}

BOOST_AUTO_TEST_CASE(removeByCondWasmTest)
{
    init(true);
    const int INSERT_COUNT = 1000;
    const int INTERVAL = 500;
    const int VALID_COUNT = 500;
    const int VALID_COUNT_PER_SECTION = VALID_COUNT / (INSERT_COUNT / INTERVAL);

    auto callAddress = tableTestAddress;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320_remove", 1, "id", {"value"}, callAddress);
    }

    // prepare data
    std::map<uint32_t, uint32_t> randomSet;
    int start = 0;
    int end = INTERVAL - 1;
    for (int i = 0; i < INSERT_COUNT / INTERVAL; i++)
    {
        generateRandomVector(VALID_COUNT_PER_SECTION, start, end, randomSet);
        start += INTERVAL;
        end += INTERVAL;
    }

    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string value = "no";
        if (randomSet.contains(j))
        {
            value = "yes";
        }
        insert(number++, std::to_string(j), {value}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    auto recoverFunc = [this, &number, &callAddress](const std::set<std::string>& removed) {
        for (auto& iter : removed)
        {
            boost::log::core::get()->set_logging_enabled(false);
            insert(number++, iter, {"yes"}, callAddress);
            boost::log::core::get()->set_logging_enabled(true);
        }
    };

    auto removeFunc = [this, &number, &callAddress](const std::string& low, const std::string& high,
                          uint32_t offset, uint32_t count, const std::string& value) {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::GE, "id", low};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LT, "id", high};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
        LimitTuple limit = {offset, count};
        auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        int32_t removedRows = 0;
        codec->decode(r1->data(), removedRows);
        return removedRows;
    };

    auto selectFunc = [this, &number, &callAddress](
                          const std::string& value, std::vector<EntryTuple>& entries) {
        ConditionTupleV320 cond = {(uint8_t)storage::Condition::Comparator::EQ, "value", value};
        LimitTuple limit = {0, 500};
        auto r1 = selectByCondition(number++, {cond}, limit, callAddress);
        codec->decode(r1->data(), entries);
    };

    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 100;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
        // recover data
        recoverFunc(removedSet);
    }

    // limitCount == 0
    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 0;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
    }

    // limitCount < USER_TABLE_MIN_LIMIT_COUNT
    {
        std::default_random_engine generator;
        std::uniform_int_distribution<int> distribution(50, 100);
        int _start = distribution(generator);
        int limitCount = 49;
        uint32_t low = 0;
        uint32_t high = 0;
        int i = 0;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
        {
            if (i == _start)
            {
                low = iter->first;
            }
            if (i - _start == limitCount)
            {
                high = iter->first;
                break;
            }
        }

        std::set<std::string> savedSet;
        std::set<std::string> removedSet;
        for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
        {
            if (iter->first >= low && iter->first < high)
            {
                removedSet.insert(std::to_string(iter->first));
                continue;
            }
            savedSet.insert(std::to_string(iter->first));
        }

        uint32_t removedRows1 =
            removeFunc(std::to_string(low), std::to_string(INSERT_COUNT), 0, limitCount, "yes");
        std::vector<EntryTuple> entries;
        selectFunc("yes", entries);
        BOOST_CHECK(removedRows1 == VALID_COUNT - entries.size());

        for (auto& entry : entries)
        {
            std::string key = std::get<0>(entry);
            if (!savedSet.contains(key))
            {
                BOOST_CHECK(false);
                break;
            }
            savedSet.erase(key);
        }
        BOOST_CHECK(savedSet.empty());
    }

    // empty condition
    {
        LimitTuple limit = {0, 10};
        auto r1 = removeByCondition(number++, {}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {100, "id", "90"};
        LimitTuple limit = {0, 10};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // limit overflow
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "90"};
        LimitTuple limit = {0, 10000};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // The index of condition out of range
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", std::to_string(0)};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::LT, "id", std::to_string(INSERT_COUNT)};
        // index out of range  0 <= idx <= 1
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::EQ, "idx", "yes"};
        auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, non numeric key
    {
        LimitTuple limit = {0, 50};
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "01"};
        auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::EQ, "id", "aa"};
        auto r2 = removeByCondition(number++, {cond2}, limit, callAddress);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);

        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "9223372036854775808"};
        auto r3 = removeByCondition(number++, {cond3}, limit, callAddress);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);

        // LONG_MIN - 1
        ConditionTupleV320 cond4 = {
            (uint8_t)storage::Condition::Comparator::EQ, "id", "-9223372036854775809"};
        auto r4 = removeByCondition(number++, {cond4}, limit, callAddress);
        BOOST_CHECK(r4->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // remove, negative key
    {
        LimitTuple limit = {0, 100};
        insert(number++, "-10", {"removexx"}, callAddress);
        insert(number++, "-100", {"removexx"}, callAddress);
        insert(number++, "-1000", {"removexx"}, callAddress);
        insert(number++, "-9223372036854775808", {"removexx"}, callAddress);

        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::GE, "id", "-9223372036854775808"};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::LE, "id", "-10"};
        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::EQ, "value", "removexx"};

        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == 4);

        auto r2 = removeByCondition(number++, {cond1, cond2}, limit, callAddress);
        uint32_t removedRows = 0;
        codec->decode(r2->data(), removedRows);
        BOOST_CHECK(removedRows == 4);

        auto r3 = count(number++, {cond3}, callAddress);
        countRes = 0;
        codec->decode(r3->data(), countRes);
        BOOST_CHECK(countRes == 0);
    }
}

BOOST_AUTO_TEST_CASE(containsTest)
{
    auto callAddress = tableTestAddress;
    const int INSERT_COUNT = 500;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320_contains", 0, "id", {"v1", "v2"}, callAddress);
    }

    auto _fillZeros = [](int _num) {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::right << _num;
        return stream.str();
    };

    for (int j = 0; j < INSERT_COUNT; j += 2)
    {
        boost::log::core::get()->set_logging_enabled(false);
        {
            std::string value = _fillZeros(j);
            std::string key = "abc_" + value;
            insert(number++, key, {value, key}, callAddress);
        }
        {
            std::string value = _fillZeros(j + 1);
            std::string key = value + "_abc";
            insert(number++, key, {value, key}, callAddress);
        }
        boost::log::core::get()->set_logging_enabled(true);
    }

    // STARTS_WITH ENDS_WITH CONTAINS
    {
        LimitTuple limit = {0, 500};
        {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", "abc"};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", "abc"};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::CONTAINS, "id", "abc"};
            auto r1 = count(number++, {cond1}, callAddress);
            auto r2 = count(number++, {cond2}, callAddress);
            auto r3 = count(number++, {cond3}, callAddress);
            uint32_t countPrefix = 0;
            uint32_t countSuffix = 0;
            uint32_t countContains = 0;
            codec->decode(r1->data(), countPrefix);
            codec->decode(r2->data(), countSuffix);
            codec->decode(r3->data(), countContains);
            BOOST_CHECK(countPrefix == INSERT_COUNT / 2);
            BOOST_CHECK(countSuffix == INSERT_COUNT - INSERT_COUNT / 2);
            BOOST_CHECK(countContains == INSERT_COUNT);

            auto r4 = selectByCondition(number++, {cond1}, limit, callAddress);
            auto r5 = selectByCondition(number++, {cond2}, limit, callAddress);
            auto r6 = selectByCondition(number++, {cond3}, limit, callAddress);
            std::vector<EntryTuple> entries1;
            codec->decode(r4->data(), entries1);
            std::vector<EntryTuple> entries2;
            codec->decode(r5->data(), entries2);
            std::vector<EntryTuple> entries3;
            codec->decode(r6->data(), entries3);
            size_t count1 = 0;
            size_t count2 = 0;
            size_t count3 = 0;
            for (uint32_t j = 0; j < 500; j += 2)
            {
                if (std::get<1>(entries1[j / 2])[0] == _fillZeros(j))
                    ++count1;
                if (std::get<1>(entries2[j / 2])[0] == _fillZeros(j + 1))
                    ++count2;
                if (std::get<1>(entries3[j / 2 + 250])[0] == _fillZeros(j))
                    ++count3;
                if (std::get<1>(entries3[j / 2])[0] == _fillZeros(j + 1))
                    ++count3;
            }
            BOOST_CHECK(count1 == entries1.size());
            BOOST_CHECK(count2 == entries2.size());
            BOOST_CHECK(count3 == entries3.size());
        }

        {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::STARTS_WITH, "v2", "abc"};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::ENDS_WITH, "v2", "abc"};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::CONTAINS, "v2", "abc"};
            auto r1 = count(number++, {cond1}, callAddress);
            auto r2 = count(number++, {cond2}, callAddress);
            auto r3 = count(number++, {cond3}, callAddress);
            uint32_t countPrefix = 0;
            uint32_t countSuffix = 0;
            uint32_t countContains = 0;
            codec->decode(r1->data(), countPrefix);
            codec->decode(r2->data(), countSuffix);
            codec->decode(r3->data(), countContains);
            BOOST_CHECK(countPrefix == INSERT_COUNT / 2);
            BOOST_CHECK(countSuffix == INSERT_COUNT - INSERT_COUNT / 2);
            BOOST_CHECK(countContains == INSERT_COUNT);

            auto r4 = selectByCondition(number++, {cond1}, limit, callAddress);
            auto r5 = selectByCondition(number++, {cond2}, limit, callAddress);
            auto r6 = selectByCondition(number++, {cond3}, limit, callAddress);
            std::vector<EntryTuple> entries1;
            codec->decode(r4->data(), entries1);
            std::vector<EntryTuple> entries2;
            codec->decode(r5->data(), entries2);
            std::vector<EntryTuple> entries3;
            codec->decode(r6->data(), entries3);
            size_t count1 = 0;
            size_t count2 = 0;
            size_t count3 = 0;
            for (uint32_t j = 0; j < 500; j += 2)
            {
                if (std::get<1>(entries1[j / 2])[0] == _fillZeros(j))
                    ++count1;
                if (std::get<1>(entries2[j / 2])[0] == _fillZeros(j + 1))
                    ++count2;
                if (std::get<1>(entries3[j / 2 + 250])[0] == _fillZeros(j))
                    ++count3;
                if (std::get<1>(entries3[j / 2])[0] == _fillZeros(j + 1))
                    ++count3;
            }
            BOOST_CHECK(count1 == entries1.size());
            BOOST_CHECK(count2 == entries2.size());
            BOOST_CHECK(count3 == entries3.size());
        }
    }

    // empty key
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", ""};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", ""};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::CONTAINS, "id", ""};

        auto r1 = count(number++, {cond1}, callAddress);
        auto r2 = count(number++, {cond2}, callAddress);
        auto r3 = count(number++, {cond3}, callAddress);
        uint32_t countPrefix = 0;
        uint32_t countSuffix = 0;
        uint32_t countContains = 0;
        codec->decode(r1->data(), countPrefix);
        codec->decode(r2->data(), countSuffix);
        codec->decode(r3->data(), countContains);

        BOOST_CHECK(countPrefix == INSERT_COUNT);
        BOOST_CHECK(countSuffix == INSERT_COUNT);
        BOOST_CHECK(countContains == INSERT_COUNT);
    }

    // error key
    {
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", "abcd"};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", "abcd"};
        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::CONTAINS, "id", "abcd"};

        auto r1 = count(number++, {cond1}, callAddress);
        auto r2 = count(number++, {cond2}, callAddress);
        auto r3 = count(number++, {cond3}, callAddress);
        uint32_t countPrefix = 0;
        uint32_t countSuffix = 0;
        uint32_t countContains = 0;
        codec->decode(r1->data(), countPrefix);
        codec->decode(r2->data(), countSuffix);
        codec->decode(r3->data(), countContains);

        BOOST_CHECK(countPrefix == 0);
        BOOST_CHECK(countSuffix == 0);
        BOOST_CHECK(countContains == 0);
    }
}

BOOST_AUTO_TEST_CASE(containsWasmTest)
{
    init(true);

    auto callAddress = tableTestAddress;
    const int INSERT_COUNT = 500;
    bcos::protocol::BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320_contains", 0, "id", {"v1", "v2"}, callAddress);
    }

    auto _fillZeros = [](int _num) {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::right << _num;
        return stream.str();
    };

    for (int j = 0; j < INSERT_COUNT; j += 2)
    {
        boost::log::core::get()->set_logging_enabled(false);
        {
            std::string value = _fillZeros(j);
            std::string key = "abc_" + value;
            insert(number++, key, {value, key}, callAddress);
        }
        {
            std::string value = _fillZeros(j + 1);
            std::string key = value + "_abc";
            insert(number++, key, {value, key}, callAddress);
        }
        boost::log::core::get()->set_logging_enabled(true);
    }

    // STARTS_WITH ENDS_WITH CONTAINS
    {
        LimitTuple limit = {0, 500};
        {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", "abc"};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", "abc"};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::CONTAINS, "id", "abc"};
            auto r1 = count(number++, {cond1}, callAddress);
            auto r2 = count(number++, {cond2}, callAddress);
            auto r3 = count(number++, {cond3}, callAddress);
            uint32_t countPrefix = 0;
            uint32_t countSuffix = 0;
            uint32_t countContains = 0;
            codec->decode(r1->data(), countPrefix);
            codec->decode(r2->data(), countSuffix);
            codec->decode(r3->data(), countContains);
            BOOST_CHECK(countPrefix == INSERT_COUNT / 2);
            BOOST_CHECK(countSuffix == INSERT_COUNT - INSERT_COUNT / 2);
            BOOST_CHECK(countContains == INSERT_COUNT);

            auto r4 = selectByCondition(number++, {cond1}, limit, callAddress);
            auto r5 = selectByCondition(number++, {cond2}, limit, callAddress);
            auto r6 = selectByCondition(number++, {cond3}, limit, callAddress);
            std::vector<EntryTuple> entries1;
            codec->decode(r4->data(), entries1);
            std::vector<EntryTuple> entries2;
            codec->decode(r5->data(), entries2);
            std::vector<EntryTuple> entries3;
            codec->decode(r6->data(), entries3);
            size_t count1 = 0;
            size_t count2 = 0;
            size_t count3 = 0;
            for (uint32_t j = 0; j < 500; j += 2)
            {
                if (std::get<1>(entries1[j / 2])[0] == _fillZeros(j))
                    ++count1;
                if (std::get<1>(entries2[j / 2])[0] == _fillZeros(j + 1))
                    ++count2;
                if (std::get<1>(entries3[j / 2 + 250])[0] == _fillZeros(j))
                    ++count3;
                if (std::get<1>(entries3[j / 2])[0] == _fillZeros(j + 1))
                    ++count3;
            }
            BOOST_CHECK(count1 == entries1.size());
            BOOST_CHECK(count2 == entries2.size());
            BOOST_CHECK(count3 == entries3.size());
        }

        {
            ConditionTupleV320 cond1 = {
                (uint8_t)storage::Condition::Comparator::STARTS_WITH, "v2", "abc"};
            ConditionTupleV320 cond2 = {
                (uint8_t)storage::Condition::Comparator::ENDS_WITH, "v2", "abc"};
            ConditionTupleV320 cond3 = {
                (uint8_t)storage::Condition::Comparator::CONTAINS, "v2", "abc"};
            auto r1 = count(number++, {cond1}, callAddress);
            auto r2 = count(number++, {cond2}, callAddress);
            auto r3 = count(number++, {cond3}, callAddress);
            uint32_t countPrefix = 0;
            uint32_t countSuffix = 0;
            uint32_t countContains = 0;
            codec->decode(r1->data(), countPrefix);
            codec->decode(r2->data(), countSuffix);
            codec->decode(r3->data(), countContains);
            BOOST_CHECK(countPrefix == INSERT_COUNT / 2);
            BOOST_CHECK(countSuffix == INSERT_COUNT - INSERT_COUNT / 2);
            BOOST_CHECK(countContains == INSERT_COUNT);

            auto r4 = selectByCondition(number++, {cond1}, limit, callAddress);
            auto r5 = selectByCondition(number++, {cond2}, limit, callAddress);
            auto r6 = selectByCondition(number++, {cond3}, limit, callAddress);
            std::vector<EntryTuple> entries1;
            codec->decode(r4->data(), entries1);
            std::vector<EntryTuple> entries2;
            codec->decode(r5->data(), entries2);
            std::vector<EntryTuple> entries3;
            codec->decode(r6->data(), entries3);
            size_t count1 = 0;
            size_t count2 = 0;
            size_t count3 = 0;
            for (uint32_t j = 0; j < 500; j += 2)
            {
                if (std::get<1>(entries1[j / 2])[0] == _fillZeros(j))
                    ++count1;
                if (std::get<1>(entries2[j / 2])[0] == _fillZeros(j + 1))
                    ++count2;
                if (std::get<1>(entries3[j / 2 + 250])[0] == _fillZeros(j))
                    ++count3;
                if (std::get<1>(entries3[j / 2])[0] == _fillZeros(j + 1))
                    ++count3;
            }
            BOOST_CHECK(count1 == entries1.size());
            BOOST_CHECK(count2 == entries2.size());
            BOOST_CHECK(count3 == entries3.size());
        }
    }

    // empty key
    {
        ConditionTupleV320 cond1 = {(uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", ""};
        ConditionTupleV320 cond2 = {(uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", ""};
        ConditionTupleV320 cond3 = {(uint8_t)storage::Condition::Comparator::CONTAINS, "id", ""};

        auto r1 = count(number++, {cond1}, callAddress);
        auto r2 = count(number++, {cond2}, callAddress);
        auto r3 = count(number++, {cond3}, callAddress);
        uint32_t countPrefix = 0;
        uint32_t countSuffix = 0;
        uint32_t countContains = 0;
        codec->decode(r1->data(), countPrefix);
        codec->decode(r2->data(), countSuffix);
        codec->decode(r3->data(), countContains);

        BOOST_CHECK(countPrefix == INSERT_COUNT);
        BOOST_CHECK(countSuffix == INSERT_COUNT);
        BOOST_CHECK(countContains == INSERT_COUNT);
    }

    // error key
    {
        ConditionTupleV320 cond1 = {
            (uint8_t)storage::Condition::Comparator::STARTS_WITH, "id", "abcd"};
        ConditionTupleV320 cond2 = {
            (uint8_t)storage::Condition::Comparator::ENDS_WITH, "id", "abcd"};
        ConditionTupleV320 cond3 = {
            (uint8_t)storage::Condition::Comparator::CONTAINS, "id", "abcd"};

        auto r1 = count(number++, {cond1}, callAddress);
        auto r2 = count(number++, {cond2}, callAddress);
        auto r3 = count(number++, {cond3}, callAddress);
        uint32_t countPrefix = 0;
        uint32_t countSuffix = 0;
        uint32_t countContains = 0;
        codec->decode(r1->data(), countPrefix);
        codec->decode(r2->data(), countSuffix);
        codec->decode(r3->data(), countContains);

        BOOST_CHECK(countPrefix == 0);
        BOOST_CHECK(countSuffix == 0);
        BOOST_CHECK(countContains == 0);
    }
}

BOOST_AUTO_TEST_CASE(mixConditionTest)
{
    init(false);
    const int START = 0;
    const int END = 20;
    bcos::protocol::BlockNumber number = 1;
    auto callAddress = tableTestAddress;

    auto _fillZeros = [](int _num) {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::right << _num;
        return stream.str();
    };

    auto selectFunc = [this, &number, &callAddress](const std::vector<ConditionTupleV320>& conds,
                          std::vector<EntryTuple>& entries) {
        LimitTuple limit = {0, 500};
        auto r1 = selectByCondition(number++, conds, limit, callAddress);
        codec->decode(r1->data(), entries);
    };

    {
        creatTable(number++, "t_test_condv320_mix1", 0, "id", {"value"}, callAddress);
    }

    // prepare data
    std::vector<std::string> record;
    record.reserve(END - START);
    for (int j = START; j < END; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        record.push_back(_fillZeros(j));
        insert(number++, record.back(), {"0"}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // test GE
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(10));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.LT(_fillZeros(10));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // update condition
        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(5));
            cond.GE(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(10));
            cond.GE(_fillZeros(5));
            cond.GE(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.GE(_fillZeros(5));
            cond.GE(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.LT(_fillZeros(10));
            cond.GE(_fillZeros(5));
            cond.GE(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // conflict condition
        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(5));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(4));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(4));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(5));
            cond.LE(_fillZeros(4));
            cond.GE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }
    }

    // TestGT
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(10));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.LT(_fillZeros(10));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // update condition
        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.GT(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(10));
            cond.GT(_fillZeros(5));
            cond.GT(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.GT(_fillZeros(5));
            cond.GT(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(10));
            cond.LT(_fillZeros(10));
            cond.GT(_fillZeros(5));
            cond.GT(_fillZeros(6));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // conflict condition
        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(5));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(4));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(5));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(4));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(5));
            cond.LE(_fillZeros(5));
            cond.GT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }
    }

    // test LT
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(10));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.GT(_fillZeros(10));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // update condition
        {
            ConditionTest cond("id", selectFunc);
            cond.LT(_fillZeros(16));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(10));
            cond.LT(_fillZeros(16));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.LT(_fillZeros(16));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.GT(_fillZeros(10));
            cond.LT(_fillZeros(16));
            cond.LT(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // conflict condition
        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.LT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.LT(_fillZeros(4));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(5));
            cond.LT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(5));
            cond.LT(_fillZeros(4));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.GE(_fillZeros(5));
            cond.LT(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }
    }

    // test LE
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(10));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.GT(_fillZeros(10));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // update condition
        {
            ConditionTest cond("id", selectFunc);
            cond.LE(_fillZeros(16));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(10));
            cond.LE(_fillZeros(16));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.LE(_fillZeros(16));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(10));
            cond.GT(_fillZeros(10));
            cond.LE(_fillZeros(16));
            cond.LE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        // conflict condition
        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.LE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.LE(_fillZeros(4));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GE(_fillZeros(5));
            cond.LE(_fillZeros(4));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.GT(_fillZeros(5));
            cond.GE(_fillZeros(5));
            cond.LE(_fillZeros(5));
            BOOST_CHECK(cond.isEquivalent(record, true));
        }
    }

    // test NE
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.NE(_fillZeros(15));
            cond.NE(_fillZeros(16));
            cond.NE(_fillZeros(17));
            cond.NE(_fillZeros(100));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.NE(_fillZeros(15));
            cond.NE(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }
    }

    // test EQ
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.EQ(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.EQ(_fillZeros(15));
            cond.EQ(_fillZeros(15));
            BOOST_CHECK(cond.isEquivalent(record));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.EQ(_fillZeros(15));
            cond.EQ(_fillZeros(16));
            BOOST_CHECK(cond.isEquivalent(record));
        }
    }

    // prepare data
    std::vector<std::string> recordStartsWith = {"abc", "abcd", "abcdef", "abce"};
    for (size_t j = 0; j < recordStartsWith.size(); ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        insert(number++, recordStartsWith[j], {"0"}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // test startsWith
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.startsWith("abcd");
            BOOST_CHECK(cond.isEquivalent(recordStartsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.startsWith("abc");
            cond.startsWith("abcd");
            BOOST_CHECK(cond.isEquivalent(recordStartsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.startsWith("abcd");
            cond.startsWith("abc");
            BOOST_CHECK(cond.isEquivalent(recordStartsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.startsWith("abcd");
            cond.startsWith("abce");
            BOOST_CHECK(cond.isEquivalent(recordStartsWith, true));
        }
    }


    std::vector<std::string> recordContainsWith = {"abc", "abcd", "abcdef", "abceabcf"};
    for (size_t j = 0; j < recordContainsWith.size(); ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        insert(number++, recordContainsWith[j], {"0"}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // test contains
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.contains("abcd");
            BOOST_CHECK(cond.isEquivalent(recordContainsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.contains("abc");
            cond.contains("abcd");
            BOOST_CHECK(cond.isEquivalent(recordContainsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.contains("abc");
            cond.contains("bcf");
            BOOST_CHECK(cond.isEquivalent(recordContainsWith));
        }
    }


    std::vector<std::string> recordEndsWith = {"cba", "dcba", "fedcba", "ecba"};
    for (size_t j = 0; j < recordEndsWith.size(); ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        insert(number++, recordEndsWith[j], {"0"}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

    // test ends_with
    {
        {
            ConditionTest cond("id", selectFunc);
            cond.endsWith("dcba");
            BOOST_CHECK(cond.isEquivalent(recordEndsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.endsWith("cba");
            cond.endsWith("dcba");
            BOOST_CHECK(cond.isEquivalent(recordEndsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.endsWith("dcba");
            cond.endsWith("cba");
            BOOST_CHECK(cond.isEquivalent(recordEndsWith));
        }

        {
            ConditionTest cond("id", selectFunc);
            cond.endsWith("dcba");
            cond.endsWith("ecba");
            BOOST_CHECK(cond.isEquivalent(recordEndsWith));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
