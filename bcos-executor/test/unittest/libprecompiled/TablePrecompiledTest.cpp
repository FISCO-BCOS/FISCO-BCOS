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
    TableFactoryPrecompiledFixture()
    {
        setIsWasm(false);
        tableTestAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~TableFactoryPrecompiledFixture() {}

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
                BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
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

    ExecutionMessage::UniquePtr readlink(
        protocol::BlockNumber _number, std::string const& _path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("readlink(string)", _path);
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr desc(protocol::BlockNumber _number, const std::string& callAddress)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("desc()");
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

    // check create
    {
        auto response1 = readlink(number++, "/tables/t_test");
        Address address;
        codec->decode(response1->data(), address);
        BOOST_CHECK(address.hex() == tableTestAddress);
        auto response2 = list(number++, "/tables");
        s256 ret;
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
        auto response1 = readlink(number++, "/tables/t_test");
        Address address;
        codec->decode(response1->data(), address);
        BOOST_CHECK(address.hex() == tableTestAddress);
        auto response2 = list(number++, "/tables");
        s256 ret;
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
        auto r2 = desc(number++, callAddress);
        TableInfoTuple tableInfo = {"id", {"item_name", "item_id", "v1", "v2"}};
        BOOST_CHECK(r2->data().toBytes() == codec->encode(tableInfo));
    }
    // append not exist table
    {
        auto r1 = appendColumns(number++, "t_test2", {"v1", "v2"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int)CODE_TABLE_NOT_EXIST));
    }
    // append duplicate field
    {
        auto r1 = appendColumns(number++, "t_test", {"v1", "v3"});
        BOOST_CHECK(r1->data().toBytes() == codec->encode((int)CODE_TABLE_DUPLICATE_FIELD));
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
