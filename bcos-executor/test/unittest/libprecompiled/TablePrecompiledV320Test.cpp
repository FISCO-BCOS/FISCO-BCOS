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
class TableFactoryPrecompiledFixture : public PrecompiledFixture
{
public:
    TableFactoryPrecompiledFixture() { init(false); }

    void init(bool isWasm)
    {
        setIsWasm(isWasm, false, true);
        tableTestAddress = isWasm ? "/tables/t_test" : "420f853b49838bd3e9466c85a4cc3428c960dde2";
    }

    virtual ~TableFactoryPrecompiledFixture() = default;

    ExecutionMessage::UniquePtr creatTable(protocol::BlockNumber _number,
        const std::string& tableName, const std::string& key, const std::vector<std::string>& value,
        const std::string& callAddress, int _errorCode = 0, bool errorInTableManager = false)
    {
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);

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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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

    ExecutionMessage::UniquePtr selectByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
        bytes in =
            codec->encodeWithSig("select((uint8,uint32,string)[],(uint32,uint32))", keyCond, limit);
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

    ExecutionMessage::UniquePtr count(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
        bytes in = codec->encodeWithSig("count((uint8,uint32,string)[])", keyCond);
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

    ExecutionMessage::UniquePtr updateByKey(protocol::BlockNumber _number, const std::string& key,
        const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
        const std::string& callAddress, bool _isErrorInTable = false)
    {
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
        bytes in = codec->encodeWithSig(
            "update((uint8,uint32,string)[],(uint32,uint32),(string,string)[])", conditions, _limit,
            _updateFields);
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
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
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

    ExecutionMessage::UniquePtr removeByCondition(protocol::BlockNumber _number,
        const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
        const std::string& callAddress)
    {
        nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
        bytes in =
            codec->encodeWithSig("remove((uint8,uint32,string)[],(uint32,uint32))", keyCond, limit);
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

    std::string tableTestAddress;
    std::string sender;
};


std::string _fillZeros(int _num)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(40) << std::right << _num;
    return stream.str();
}

#if 0
static void generateRandomVector(
    uint32_t count, uint32_t _min, uint32_t _max, std::map<std::string, uint32_t>& res)
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
        res.insert({_fillZeros(temp[i]), i + offset});
    }
}
#endif

BOOST_FIXTURE_TEST_SUITE(precompiledTableTestV320, TableFactoryPrecompiledFixture)


BOOST_AUTO_TEST_CASE(countTest)
{
#if 0
    const int INSERT_COUNT = 100000;

    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
    }

    std::map<std::string, uint32_t> randomSet;
    int start = 0;
    int end = 499;
    for (int i = 0; i < INSERT_COUNT / 500; i++)
    {
        generateRandomVector(25, start, end, randomSet);
        start += 500;
        end += 500;
    }

    boost::log::core::get()->set_logging_enabled(false);
    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        std::string value = "no";
        if (randomSet.contains(_fillZeros(j)))
        {
            value = "yes";
        }
        insert(number++, _fillZeros(j), {value}, callAddress);
    }
    // boost::log::core::get()->set_logging_enabled(true);

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
                lowKey = iter->first;
            }
            if (counter == high)
            {
                validCount = iter->second - validCount + 1;
                highKey = iter->first;
                break;
            }
        }
        // lowKey <= key <= highKey && value == "yes"
        ConditionTupleV320 cond1 = {3, 0, lowKey};
        ConditionTupleV320 cond2 = {5, 0, highKey};
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        ConditionTupleV320 cond4 = {1, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        // FIXME: the ut will failed
        // BOOST_TEST(countRes == validCount);

        // lowKey <= key <= highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low + 1;
        auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        codec->decode(r2->data(), countRes);
        // FIXME: the ut will failed
        // BOOST_CHECK(countRes == total - validCount);
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
                lowKey = iter->first;
            }
            if (counter == high)
            {
                validCount = iter->second - validCount - 1;
                highKey = iter->first;
                break;
            }
        }

        // lowKey < key < highKey && value == "yes"
        ConditionTupleV320 cond1 = {2, 0, lowKey};
        ConditionTupleV320 cond2 = {4, 0, highKey};
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        ConditionTupleV320 cond4 = {1, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == validCount);

        // lowKey < key < highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low - 1;
        auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        codec->decode(r2->data(), countRes);
        BOOST_CHECK(countRes == total - validCount);
    }

    // 0 <= key <= 1001
    {
        uint32_t low = 0;
        uint32_t high = 1001;
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
        ConditionTupleV320 cond2 = {5, 0, _fillZeros(high)};
        auto r1 = count(number++, {cond1, cond2}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == high - low + 1);
    }

    // value == "yes"
    {
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == 25 * (INSERT_COUNT / 500));
    }

    // value == "no"
    {
        ConditionTupleV320 cond3 = {0, 1, "no"};
        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_TEST(countRes == (500 - 25) * (INSERT_COUNT / 500));
    }

    // The index of condition out of range
    {
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
        ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
        // index out of range
        ConditionTupleV320 cond3 = {0, 5, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // empty condition
    {
        auto r1 = count(number++, {}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
        ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {10, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
#endif
}


BOOST_AUTO_TEST_CASE(countWasmTest)
{
#if 0
    init(true);
    const int INSERT_COUNT = 100000;

    auto callAddress = tableTestAddress;
    BlockNumber number = 1;
    {
        creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
    }

    std::map<std::string, uint32_t> randomSet;
    int start = 0;
    int end = 499;
    for (int i = 0; i < INSERT_COUNT / 500; i++)
    {
        generateRandomVector(25, start, end, randomSet);
        start += 500;
        end += 500;
    }

    for (int j = 0; j < INSERT_COUNT; ++j)
    {
        boost::log::core::get()->set_logging_enabled(false);
        std::string value = "no";
        if (randomSet.contains(_fillZeros(j)))
        {
            value = "yes";
        }
        insert(number++, _fillZeros(j), {value}, callAddress);
        boost::log::core::get()->set_logging_enabled(true);
    }

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
                lowKey = iter->first;
            }
            if (counter == high)
            {
                validCount = iter->second - validCount + 1;
                highKey = iter->first;
                break;
            }
        }
        // lowKey <= key <= highKey && value == "yes"
        ConditionTupleV320 cond1 = {3, 0, lowKey};
        ConditionTupleV320 cond2 = {5, 0, highKey};
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        ConditionTupleV320 cond4 = {1, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        // FIXME: the ut will failed
        // BOOST_TEST(countRes == validCount);

        // lowKey <= key <= highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low + 1;
        auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        codec->decode(r2->data(), countRes);
        // FIXME: the ut will failed
        // BOOST_CHECK(countRes == total - validCount);
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
                lowKey = iter->first;
            }
            if (counter == high)
            {
                validCount = iter->second - validCount - 1;
                highKey = iter->first;
                break;
            }
        }

        // lowKey < key < highKey && value == "yes"
        ConditionTupleV320 cond1 = {2, 0, lowKey};
        ConditionTupleV320 cond2 = {4, 0, highKey};
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        ConditionTupleV320 cond4 = {1, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == validCount);

        // lowKey < key < highKey && value != "yes"
        low = boost::lexical_cast<uint32_t>(lowKey);
        high = boost::lexical_cast<uint32_t>(highKey);
        uint32_t total = high - low - 1;
        auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
        countRes = 0;
        codec->decode(r2->data(), countRes);
        BOOST_CHECK(countRes == total - validCount);
    }

    // 0 <= key <= 1001
    {
        uint32_t low = 0;
        uint32_t high = 1001;
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
        ConditionTupleV320 cond2 = {5, 0, _fillZeros(high)};
        auto r1 = count(number++, {cond1, cond2}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == high - low + 1);
    }

    // value == "yes"
    {
        ConditionTupleV320 cond3 = {0, 1, "yes"};
        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == 25 * (INSERT_COUNT / 500));
    }

    // value == "no"
    {
        ConditionTupleV320 cond3 = {0, 1, "no"};
        auto r1 = count(number++, {cond3}, callAddress);
        uint32_t countRes = 0;
        codec->decode(r1->data(), countRes);
        BOOST_CHECK(countRes == (500 - 25) * (INSERT_COUNT / 500));
    }

    // The index of condition out of range
    {
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
        ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
        // index out of range
        ConditionTupleV320 cond3 = {0, 5, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // empty condition
    {
        auto r1 = count(number++, {}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // condition with undefined cmp
    {
        ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
        ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
        ConditionTupleV320 cond3 = {10, 1, "yes"};
        auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
#endif
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
