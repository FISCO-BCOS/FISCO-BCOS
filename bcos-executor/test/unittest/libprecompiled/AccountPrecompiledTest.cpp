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
 * @file AccountPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-11-15
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/extension/AccountManagerPrecompiled.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
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
class AccountPrecompiledFixture : public PrecompiledFixture
{
public:
    AccountPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false, true);
    }

    ~AccountPrecompiledFixture() override {}

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

    ExecutionMessage::UniquePtr createAccount(protocol::BlockNumber _number, Address account,
        int _errorCode = 0, bool errorInAccountManager = false)
    {
        nextBlock(_number, Version::V3_1_VERSION);
        bytes in = codec->encodeWithSig("createAccount(address)", account);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::ACCOUNT_MGR_ADDRESS);
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

        if (errorInAccountManager)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);

        // external create
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

        // external call, set account status
        result6->setSeq(1003);
        std::promise<ExecutionMessage::UniquePtr> executePromise7;
        executor->dmcExecuteTransaction(std::move(result6),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise7.set_value(std::move(result));
            });
        auto result7 = executePromise7.get_future().get();

        // external call back
        result7->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise8;
        executor->dmcExecuteTransaction(std::move(result7),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise8.set_value(std::move(result));
            });
        auto result8 = executePromise8.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result8->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result8;
    };

    ExecutionMessage::UniquePtr setAccountStatus(protocol::BlockNumber _number, Address account,
        uint16_t status, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false)
    {
        nextBlock(_number, Version::V3_1_VERSION);
        bytes in = codec->encodeWithSig("setAccountStatus(address,uint16)", account, status);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::ACCOUNT_MGR_ADDRESS);
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

        if (errorInAccountManager)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);

        // external create
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        if (exist)
        {
            // if account exist, just callback
            result3->setSeq(1000);
            std::promise<ExecutionMessage::UniquePtr> executePromise4;
            executor->dmcExecuteTransaction(std::move(result3),
                [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                    BOOST_CHECK(!error);
                    executePromise4.set_value(std::move(result));
                });
            auto result4 = executePromise4.get_future().get();
            if (_errorCode != 0)
            {
                BOOST_CHECK(result4->data().toBytes() == codec->encode(s256(_errorCode)));
            }

            commitBlock(_number);
            return result4;
        }

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

        // external call, set account status
        result6->setSeq(1003);
        std::promise<ExecutionMessage::UniquePtr> executePromise7;
        executor->dmcExecuteTransaction(std::move(result6),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise7.set_value(std::move(result));
            });
        auto result7 = executePromise7.get_future().get();

        // external call back
        result7->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise8;
        executor->dmcExecuteTransaction(std::move(result7),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise8.set_value(std::move(result));
            });
        auto result8 = executePromise8.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result8->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result8;
    };

    ExecutionMessage::UniquePtr getAccountStatusByManager(protocol::BlockNumber _number,
        Address account, int _errorCode = 0, bool errorInAccountManager = false)
    {
        nextBlock(_number, Version::V3_1_VERSION);
        bytes in = codec->encodeWithSig("getAccountStatus(address)", account);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::ACCOUNT_MGR_ADDRESS);
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

        if (errorInAccountManager)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);

        // external call
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // external call callback
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result4->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result4;
    };

    ExecutionMessage::UniquePtr getAccountStatus(
        protocol::BlockNumber _number, Address account, int _errorCode = 0)
    {
        nextBlock(_number, Version::V3_1_VERSION);
        bytes in = codec->encodeWithSig("getAccountStatus()");
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(account.hex());
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
    std::string sender;
};


BOOST_FIXTURE_TEST_SUITE(AccountPrecompiledTest, AccountPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createAccountTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address errorAccount = Address("17505f128bd4d00c2698441b1f54ef843b837211");
    BlockNumber number = 1;
    {
        auto response = createAccount(number++, newAccount);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);
    }

    // check create
    {
        auto response2 = list(number++, "/usr");
        int32_t ret;
        std::vector<BfsTuple> bfsInfos;
        codec->decode(response2->data(), ret, bfsInfos);
        BOOST_CHECK(ret == 0);
        BOOST_CHECK(bfsInfos.size() == 1);
        auto fileInfo = bfsInfos[0];
        BOOST_CHECK(std::get<0>(fileInfo) == "27505f128bd4d00c2698441b1f54ef843b837215");
        BOOST_CHECK(std::get<1>(fileInfo) == bcos::executor::FS_TYPE_LINK);

        auto response3 = list(number++, "/usr/27505f128bd4d00c2698441b1f54ef843b837215");
        int32_t ret2;
        std::vector<BfsTuple> bfsInfos2;
        codec->decode(response3->data(), ret2, bfsInfos2);
        BOOST_CHECK(ret2 == 0);
        BOOST_CHECK(bfsInfos2.size() == 1);
        BOOST_CHECK(std::get<0>(bfsInfos2[0]) == "27505f128bd4d00c2698441b1f54ef843b837215");
    }

    // createTable exist
    {
        createAccount(number++, newAccount, CODE_ACCOUNT_ALREADY_EXIST, true);
    }

    // check status is 0
    {
        auto response = getAccountStatusByManager(number++, newAccount);
        BOOST_CHECK(response->status() == 0);
        uint16_t status = UINT16_MAX;
        u256 blockNumber;
        codec->decode(response->data(), status, blockNumber);
        BOOST_CHECK(status == 0);
        BOOST_CHECK_GT(blockNumber, 0);

        auto response2 = getAccountStatusByManager(number++, errorAccount);
        BOOST_CHECK(response2->status() == 15);
    }

    // check status by account contract
    {
        auto response = getAccountStatus(number++, newAccount);
        BOOST_CHECK(response->status() == 0);
        uint16_t status = UINT16_MAX;
        u256 blockNumber;
        codec->decode(response->data(), status, blockNumber);
        BOOST_CHECK(status == 0);
        BOOST_CHECK_GT(blockNumber, 0);

        auto response2 = getAccountStatus(number++, errorAccount);
        BOOST_CHECK(response2->status() == (int32_t)TransactionStatus::CallAddressError);
    }
}

BOOST_AUTO_TEST_CASE(setAccountStatusTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address errorAccount = Address("17505f128bd4d00c2698441b1f54ef843b837211");
    BlockNumber number = 1;

    // setAccountStatus account not exist
    {
        auto response = setAccountStatus(number++, newAccount, 0);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);
    }

    // createTable exist
    {
        createAccount(number++, newAccount, CODE_ACCOUNT_ALREADY_EXIST, true);
    }

    // setAccountStatus account exist
    {
        auto response = setAccountStatus(number++, newAccount, 1, 0, true);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);

        auto rsp2 = getAccountStatus(number++, newAccount);
        BOOST_CHECK(rsp2->status() == 0);
        uint16_t status = UINT16_MAX;
        u256 blockNumber;
        codec->decode(rsp2->data(), status, blockNumber);
        BOOST_CHECK(status == 1);
        BOOST_CHECK_GT(blockNumber, 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test