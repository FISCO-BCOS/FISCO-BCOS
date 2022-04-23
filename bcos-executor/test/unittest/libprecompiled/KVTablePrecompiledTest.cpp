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

#include "precompiled/KVTablePrecompiled.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "libprecompiled/PreCompiledFixture.h"
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
    KVTableFactoryPrecompiledFixture() { init(false); }

    void init(bool _isWasm)
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, _isWasm);
        setIsWasm(_isWasm);
    }

    virtual ~KVTableFactoryPrecompiledFixture() {}

    ExecutionMessage::UniquePtr creatKVTable(protocol::BlockNumber _number,
        const std::string& tableName, const std::string& key, const std::string& value,
        const std::string& solidityAddress, int _errorCode = 0, bool errorInTableManager = false)
    {
        nextBlock(_number);
        bytes in =
            codec->encodeWithSig("createKVTable(string,string,string)", tableName, key, value);
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
        result2->setTo(solidityAddress);
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

    ExecutionMessage::UniquePtr get(
        protocol::BlockNumber _number, const std::string& callAddress, const std::string& key)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("get(string)", key);
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

        commitBlock(_number);
        return result2;
    }

    ExecutionMessage::UniquePtr set(protocol::BlockNumber _number, const std::string& callAddress,
        const std::string& key, const std::string& value, int _errorCode = 1)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("set(string,string)", key, value);
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
        commitBlock(_number);
        return result2;
    }

    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledKVTableTest, KVTableFactoryPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createTableTest)
{
    int64_t number = 2;
    //
    {
        creatKVTable(
            number++, "t_test", "id", "item_name", "1234853b49838bd3e9466c85a4cc3428c960dde2");
    }

    // createTable exist
    {
        creatKVTable(number++, "t_test", "id", "item_name",
            "2234853b49838bd3e9466c85a4cc3428c960dde2", CODE_TABLE_NAME_ALREADY_EXIST, true);
    }

    // createTable build
    {
        auto response = creatKVTable(number++, "t_test/t_test2", "id", "item_name",
            "3234853b49838bd3e9466c85a4cc3428c960dde2");
        BOOST_CHECK(response->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(0);
    }
    // createTable too long tableName, key and field
    {
        auto r1 = creatKVTable(number++, errorStr, "id", "item_name",
            "1134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatKVTable(number++, "t_test", errorStr, "item_name",
            "2134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatKVTable(number++, "t_test", "id", errorStr,
            "3134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatKVTable(number++, errorStr2, "id", "item_name,item_id",
            "4134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatKVTable(number++, "t_test", errorStr2, "item_name,item_id",
            "5134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatKVTable(number++, "t_test", "id", errorStr2,
            "6134853b49838bd3e9466c85a4cc3428c960dde1", 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(createTableWasmTest)
{
    init(true);
    int64_t number = 2;
    //
    {
        creatKVTable(number++, "t_test", "id", "item_name", "/tables/t_test");
    }

    // createTable exist
    {
        creatKVTable(number++, "t_test", "id", "item_name", "/tables/t_test",
            CODE_TABLE_NAME_ALREADY_EXIST, true);
    }

    // createTable build
    {
        auto response =
            creatKVTable(number++, "t_test/t_test2", "id", "item_name", "/tables/t_test/t_test2");
        BOOST_CHECK(response->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    std::string errorStr;
    for (int i = 0; i <= SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH; i++)
    {
        errorStr += std::to_string(0);
    }
    // createTable too long tableName, key and field
    {
        auto r1 = creatKVTable(number++, errorStr, "id", "item_name", "/tables/t_test3", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 =
            creatKVTable(number++, "t_test", errorStr, "item_name", "/tables/t_test4", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatKVTable(number++, "t_test", "id", errorStr, "/tables/t_test5", 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // createTable error key and field
    std::string errorStr2 = "/test&";
    {
        auto r1 = creatKVTable(
            number++, errorStr2, "id", "item_name,item_id", "/tables/t_test6", 0, true);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r2 = creatKVTable(
            number++, "t_test", errorStr2, "item_name,item_id", "/tables/t_test7", 0, true);
        BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
        auto r3 = creatKVTable(number++, "t_test", "id", errorStr2, "/tables/t_test8", 0, true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(descTest)
{
    int64_t number = 2;
    std::string address = "1234853b49838bd3e9466c85a4cc3428c960dde2";
    // create
    {
        creatKVTable(number++, "t_kv_test", "id", "item_name", address);
    }

    auto result2 = desc(number++, address);
    TableInfoTuple tableInfo = {"id", {"item_name"}};
    BOOST_CHECK(result2->data().toBytes() == codec->encode(tableInfo));
}

BOOST_AUTO_TEST_CASE(descWasmTest)
{
    init(true);
    int64_t number = 2;
    std::string address = "/tables/t_kv_test";
    // create
    {
        auto result = creatKVTable(number++, "t_kv_test", "id", "item_name", address);
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
    }

    auto result2 = desc(number++, address);
    TableInfoTuple tableInfo = {"id", {"item_name"}};
    BOOST_CHECK(result2->data().toBytes() == codec->encode(tableInfo));
}

BOOST_AUTO_TEST_CASE(setTest)
{
    int64_t number = 2;
    std::string address = "1234853b49838bd3e9466c85a4cc3428c960dde2";
    // create
    {
        creatKVTable(number++, "t_kv_test", "id", "item_name", address);
    }

    // simple set and get
    {
        auto result1 = set(number++, address, "id1", "test1");
        auto result2 = get(number++, address, "id1");
        EntryTuple entry1 = {"id1", {"test1"}};
        BOOST_CHECK(result2->data().toBytes() == codec->encode(true, entry1));
    }

    // cover write and get
    {
        auto result3 = set(number++, address, "id1", "test2");
        auto result4 = get(number++, address, "id1");
        EntryTuple entry2 = {"id1", {"test2"}};
        BOOST_CHECK(result4->data().toBytes() == codec->encode(true, entry2));
    }

    // get not exist
    {
        auto result4 = get(number++, address, "noExist");
        EntryTuple entry2 = {};
        BOOST_CHECK(result4->data().toBytes() == codec->encode(false, entry2));
    }

    boost::log::core::get()->set_logging_enabled(false);
    // set key overflow
    {
        std::string errorKey = "0";
        for (int j = 0; j < USER_TABLE_KEY_VALUE_MAX_LENGTH; ++j)
        {
            errorKey += "0";
        }
        auto result3 = set(number++, address, errorKey, "test2");
        BOOST_CHECK(result3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // set value overflow
    {
        std::string errorValue = "0";
        for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
        {
            errorValue += "0";
        }
        auto result3 = set(number++, address, "1", errorValue);
        BOOST_CHECK(result3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    boost::log::core::get()->set_logging_enabled(true);
}


BOOST_AUTO_TEST_CASE(setWasmTest)
{
    init(true);
    int64_t number = 2;
    std::string address = "/tables/t_kv_test";
    // create
    {
        creatKVTable(number++, "t_kv_test", "id", "item_name", address);
    }

    // simple set and get
    {
        auto result1 = set(number++, address, "id1", "test1");
        auto result2 = get(number++, address, "id1");
        EntryTuple entry1 = {"id1", {"test1"}};
        BOOST_CHECK(result2->data().toBytes() == codec->encode(true, entry1));
    }

    // cover write and get
    {
        auto result3 = set(number++, address, "id1", "test2");
        auto result4 = get(number++, address, "id1");
        EntryTuple entry2 = {"id1", {"test2"}};
        BOOST_CHECK(result4->data().toBytes() == codec->encode(true, entry2));
    }

    // get not exist
    {
        auto result4 = get(number++, address, "noExist");
        EntryTuple entry2 = {};
        BOOST_CHECK(result4->data().toBytes() == codec->encode(false, entry2));
    }

    boost::log::core::get()->set_logging_enabled(false);
    // set key overflow
    {
        std::string errorKey = "0";
        for (int j = 0; j < USER_TABLE_KEY_VALUE_MAX_LENGTH; ++j)
        {
            errorKey += "0";
        }
        auto result3 = set(number++, address, errorKey, "test2");
        BOOST_CHECK(result3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }

    // set value overflow
    {
        std::string errorValue = "0";
        for (int j = 0; j < USER_TABLE_FIELD_VALUE_MAX_LENGTH; ++j)
        {
            errorValue += "0";
        }
        auto result3 = set(number++, address, "1", errorValue);
        BOOST_CHECK(result3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
    boost::log::core::get()->set_logging_enabled(true);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
