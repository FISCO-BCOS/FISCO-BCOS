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
 * @file FileSystemPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-20
 */

#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class FileSystemPrecompiledFixture : public PrecompiledFixture
{
public:
    FileSystemPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);

        auto result1 = createTable(1, "test1", "id", "item1,item2");
        BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(0)));
        auto result2 = createTable(2, "test2", "id", "item1,item2");
        BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(0)));
    }

    virtual ~FileSystemPrecompiledFixture() {}

    ExecutionMessage::UniquePtr createTable(protocol::BlockNumber _number,
        std::string const& tableName, std::string const& key, std::string const& value,
        int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("createTable(string,string,string)", tableName, key, value);
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
        params2->setTo(precompiled::KV_TABLE_ADDRESS);
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
        // call precompiled
        result2->setSeq(1001);
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
        params2->setTo(precompiled::BFS_ADDRESS);
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

    ExecutionMessage::UniquePtr mkdir(
        protocol::BlockNumber _number, std::string const& path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("mkdir(string)", path);
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
        params2->setTo(precompiled::BFS_ADDRESS);
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
        // call precompiled
        result2->setSeq(1001);
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    std::string sender;
};
BOOST_FIXTURE_TEST_SUITE(precompiledFileSystemTest, FileSystemPrecompiledFixture)

BOOST_AUTO_TEST_CASE(lsTest)
{
    BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        std::string ls;
        codec->decode(result->data(), ls);
        Json::Value retJson;
        Json::Reader reader;
        BOOST_CHECK(reader.parse(ls, retJson) == true);
        BOOST_CHECK(retJson.size() == 2);
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        std::string ls;
        codec->decode(result->data(), ls);
        Json::Value retJson;
        Json::Reader reader;
        BOOST_CHECK(reader.parse(ls, retJson) == true);
        BOOST_CHECK(retJson.size() == 1);
        BOOST_CHECK(retJson[0][FS_KEY_TYPE] == FS_TYPE_CONTRACT);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        std::string ls;
        codec->decode(result->data(), ls);
        s256 errorCode;
        codec->decode(result->data(), errorCode);
        BOOST_CHECK(errorCode == s256((int)CODE_FILE_NOT_EXIST));
    }

    // ls /
    {
        auto result = list(_number++, "/");
        std::string ls;
        codec->decode(result->data(), ls);
        Json::Value retJson;
        Json::Reader reader;
        BOOST_CHECK(reader.parse(ls, retJson) == true);
        BOOST_CHECK(retJson.size() == 3);  // with '/'
    }

    // mkdir invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
        list(_number++, "/tables/test_1.test", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(mkdirTest)
{
    BlockNumber _number = 3;
    // simple mkdir
    {
        auto result = mkdir(_number++, "/tables/temp/test");
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/tables");
        std::string ls;
        codec->decode(lsResult->data(), ls);
        Json::Value retJson;
        Json::Reader reader;
        BOOST_CHECK(reader.parse(ls, retJson) == true);
        BOOST_CHECK(retJson.size() == 3);

        auto lsResult2 = list(_number++, "/tables/temp");
        std::string ls2;
        codec->decode(lsResult2->data(), ls2);
        Json::Value retJson2;
        Json::Reader reader2;
        BOOST_CHECK(reader2.parse(ls2, retJson2) == true);
        BOOST_CHECK(retJson2.size() == 1);
        BOOST_CHECK(retJson2[0][FS_KEY_TYPE] == FS_TYPE_DIR);
    }

    // mkdir /tables/test1/test
    {
        auto result = mkdir(_number++, "/tables/test1/test", CODE_FILE_BUILD_DIR_FAILED);
    }

    // mkdir /tables/test1
    {
        auto result = mkdir(_number++, "/tables/test1", CODE_FILE_ALREADY_EXIST);
    }

    // mkdir /tables
    {
        auto result = mkdir(_number++, "/tables", CODE_FILE_INVALID_PATH);
    }

    // mkdir in wrong path
    {
        auto result = mkdir(_number++, "/sys/test1", CODE_FILE_INVALID_PATH);
        auto result2 = mkdir(_number++, "/user/test1", CODE_FILE_INVALID_PATH);
        auto result3 = mkdir(_number++, "/test1", CODE_FILE_INVALID_PATH);
    }

    // mkdir invalid path
    {
        mkdir(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        mkdir(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        mkdir(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
        mkdir(_number++, "/tables/test_1.test", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
