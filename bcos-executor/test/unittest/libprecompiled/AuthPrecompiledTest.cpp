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
 * @file AuthPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-11-15
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/extension/AuthManagerPrecompiled.h"
#include "precompiled/extension/ContractAuthMgrPrecompiled.h"
#include <bcos-framework//executor/PrecompiledTypeDef.h>
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
class PermissionPrecompiledFixture : public PrecompiledFixture
{
public:
    PermissionPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false, true);
        helloAddress = Address("0x1234654b49838bd3e9466c85a4cc3428c9601234").hex();
        hello2Address = Address("0x0987654b49838bd3e9466c85a4cc3428c9601234").hex();
    }

    ~PermissionPrecompiledFixture() override {}

    void deployHello()
    {
        bytes input;
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
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

        params->setTo(helloAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(2);
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

        /// call Auth manager to check deploy auth
        result->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        /// callback to create context
        result2->setSeq(1000);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->create(), false);
        BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), helloAddress);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->from(), helloAddress);
        BOOST_CHECK(result3->to() == sender);
        BOOST_CHECK_LT(result3->gasAvailable(), gas);

        commitBlock(2);
    }

    ExecutionMessage::UniquePtr deployHelloInAuthCheck(std::string newAddress, BlockNumber _number,
        Address _address = Address(), bool _noAuth = false)
    {
        bytes input;
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(newAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(_number);
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

        /// call Auth manager to check deploy auth
        result->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        /// callback to create context
        result2->setSeq(1000);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        if (_noAuth)
        {
            BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::REVERT);
            BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), "");
        }
        else
        {
            BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
            BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), newAddress);
            BOOST_CHECK_LT(result3->gasAvailable(), gas);
        }
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->create(), false);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->from(), newAddress);
        BOOST_CHECK(result3->to() == sender);

        commitBlock(_number);

        return result3;
    }

    ExecutionMessage::UniquePtr deployHello2InAuthCheck(
        std::string newAddress, BlockNumber _number, Address _address = Address())
    {
        bytes input;
        boost::algorithm::unhex(h2, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(newAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        nextBlock(_number);
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

        /// call Auth manager to check deploy auth
        result->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        /// callback to create context
        result2->setSeq(1000);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);
        result3->setType(NativeExecutionMessage::MESSAGE);
        result3->setTransactionHash(hash);
        result3->setKeyLocks({});
        result3->setNewEVMContractAddress(hello2Address);
        result3->setTo(hello2Address);

        /// create new hello
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });

        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1003);
        /// call to check deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });

        auto result5 = executePromise5.get_future().get();

        result5->setSeq(1002);
        /// callback to deploy hello2 context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise6;
        executor->executeTransaction(
            std::move(result5), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise6.set_value(std::move(result));
            });

        auto result6 = executePromise6.get_future().get();

        commitBlock(_number);

        return result6;
    }

    ExecutionMessage::UniquePtr helloGet(protocol::BlockNumber _number, int _contextId,
        int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("get()");
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(helloAddress);
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);

        return result2;
    };

    ExecutionMessage::UniquePtr helloSet(protocol::BlockNumber _number, int _contextId,
        const std::string& _value, int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("set(string)", _value);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        if (_address != Address())
        {
            tx->forceSender(_address.asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(helloAddress);
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);

        return result2;
    };

    ExecutionMessage::UniquePtr setMethodType(protocol::BlockNumber _number, int _contextId,
        Address const& _path, std::string const& helloMethod, precompiled::AuthType _type,
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes func = codec->encodeWithSig(helloMethod);
        auto fun = toString32(h256(func, FixedBytes<32>::AlignLeft));
        uint8_t type = (_type == AuthType::WHITE_LIST_MODE) ? 1 : 2;
        auto t = toString32(h256(type));
        bytes in = codec->encodeWithSig("setMethodAuthType(address,bytes4,uint8)", _path, fun, t);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        result->setSeq(1001);

        /// internal call get admin
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        /// callback to precompiled
        result2->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);

        /// internal call set contract auth type
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1000);

        /// callback to precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result5->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result5;
    };

    ExecutionMessage::UniquePtr modifyMethodAuth(protocol::BlockNumber _number, int _contextId,
        std::string const& authMethod, Address const& _path, std::string const& helloMethod,
        Address const& _account, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig(authMethod, _path, func, _account);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        result->setSeq(1001);

        /// internal call get admin
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        /// callback to precompiled
        result2->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);

        /// internal call set contract auth type
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1000);

        /// callback to precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        // call precompiled
        if (_errorCode != 0)
        {
            BOOST_CHECK(result5->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);

        return result5;
    };

    ExecutionMessage::UniquePtr getMethodAuth(protocol::BlockNumber _number, Address const& _path,
        std::string const& helloMethod, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig("getMethodAuth(address,bytes4)", _path, func);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result1 = executePromise.get_future().get();

        result1->setSeq(1001);

        /// internal call
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        /// internal callback
        result2->setSeq(1000);

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

    ExecutionMessage::UniquePtr resetAdmin(protocol::BlockNumber _number, int _contextId,
        Address const& _path, Address const& _newAdmin, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("resetAdmin(address,address)", _path, _newAdmin);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result1 = executePromise.get_future().get();

        result1->setSeq(1001);

        /// internal call
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1000);
        /// internal callback
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

    ExecutionMessage::UniquePtr setContractStatus(
        protocol::BlockNumber _number, Address const& _path, bool _isFreeze, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("setContractStatus(address,bool)", _path, _isFreeze);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        result->setSeq(1001);

        /// internal call get admin
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        /// callback to precompiled
        result2->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);

        /// internal call set contract status
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1000);

        /// callback to precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result5->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result5;
    };

    ExecutionMessage::UniquePtr contractAvailable(
        protocol::BlockNumber _number, Address const& _path, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("contractAvailable(address)", _path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        result->setSeq(1001);

        /// internal call get contract status
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1000);

        /// callback to precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result5->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result5;
    };

    ExecutionMessage::UniquePtr getAdmin(
        protocol::BlockNumber _number, int _contextId, Address const& _path, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("getAdmin(address)", _path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::AUTH_MANAGER_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result1 = executePromise.get_future().get();

        result1->setSeq(1001);

        /// internal call
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(result1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        /// internal callback
        result2->setSeq(1000);

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

    ExecutionMessage::UniquePtr setDeployType(
        protocol::BlockNumber _number, int _contextId, AuthType _authType, int _errorCode = 0)
    {
        nextBlock(_number);
        uint8_t type = (_authType == AuthType::WHITE_LIST_MODE) ? 1 : 2;
        auto t = toString32(h256(type));
        bytes in = codec->encodeWithSig("setDeployAuthType(uint8)", t);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_MANAGER_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr getDeployType(
        protocol::BlockNumber _number, int _contextId, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("deployType()");
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_MANAGER_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr openDeployAuth(
        protocol::BlockNumber _number, int _contextId, Address const& _address, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("openDeployAuth(address)", _address);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_MANAGER_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr closeDeployAuth(
        protocol::BlockNumber _number, int _contextId, Address const& _address, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("closeDeployAuth(address)", _address);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_MANAGER_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr hasDeployAuth(
        protocol::BlockNumber _number, int _contextId, Address const& _address, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("hasDeployAuth(address)", _address);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        tx->forceSender(newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_MANAGER_ADDRESS);
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
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };


    std::string sender;
    std::string helloAddress;
    std::string hello2Address;

    // clang-format off
    std::string helloBin =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4dba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
    std::string h2 =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c9291906100cb565b506040516100699061014b565b604051809103906000f080158015610085573d6000803e3d6000fd5b50600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550610175565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061010c57805160ff191683800117855561013a565b8280016001018555821561013a579182015b8281111561013957825182559160200191906001019061011e565b5b5090506101479190610158565b5090565b6104168061060f83390190565b5b80821115610171576000816000905550600101610159565b5090565b61048b806101846000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c8063a7ef43291461003b578063d2178b081461006f575b600080fd5b61004361015e565b604051808273ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b610077610188565b604051808060200180602001838103835285818151815260200191508051906020019080838360005b838110156100bb5780820151818401526020810190506100a0565b50505050905090810190601f1680156100e85780820380516001836020036101000a031916815260200191505b50838103825284818151815260200191508051906020019080838360005b83811015610121578082015181840152602081019050610106565b50505050905090810190601f16801561014e5780820380516001836020036101000a031916815260200191505b5094505050505060405180910390f35b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905090565b606080600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff1660e01b815260040160006040518083038186803b1580156101f357600080fd5b505afa158015610207573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f82011682018060405250602081101561023157600080fd5b810190808051604051939291908464010000000082111561025157600080fd5b8382019150602082018581111561026757600080fd5b825186600182028301116401000000008211171561028457600080fd5b8083526020830192505050908051906020019080838360005b838110156102b857808201518184015260208101905061029d565b50505050905090810190601f1680156102e55780820380516001836020036101000a031916815260200191505b50604052505050600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff1660e01b815260040160006040518083038186803b15801561035457600080fd5b505afa158015610368573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f82011682018060405250602081101561039257600080fd5b81019080805160405193929190846401000000008211156103b257600080fd5b838201915060208201858111156103c857600080fd5b82518660018202830111640100000000821117156103e557600080fd5b8083526020830192505050908051906020019080838360005b838110156104195780820151818401526020810190506103fe565b50505050905090810190601f1680156104465780820380516001836020036101000a031916815260200191505b5060405250505091509150909156fea2646970667358221220004a7724bbd7d9ee1d4c00b949283850a69ddc538be408fa1c79fce561994b0a64736f6c634300060c0033608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b506100ff565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b5b808211156100fb5760008160009055506001016100e3565b5090565b6103088061010e6000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b5b808211156102ce5760008160009055506001016102b6565b509056fea26469706673582212200374a2bdb89cd0187ff6b1f551739ca53a44f456bd10ea4052e069475292e45b64736f6c634300060c0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(precompiledPermissionTest, PermissionPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testMethodWhiteList)
{
    deployHello();
    BlockNumber number = 2;
    // simple get
    {
        auto result = helloGet(number++, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 3;

        // not found
        auto r1 = getMethodAuth(number++, Address(helloAddress), "set(string)");
        BOOST_CHECK(
            r1->data().toBytes() == codec->encode((uint8_t)(0), std::vector<std::string>({}),
                                        std::vector<std::string>({})));

        auto r2 = getMethodAuth(number++, Address(helloAddress), "get()");
        BOOST_CHECK(
            r2->data().toBytes() == codec->encode((uint8_t)(0), std::vector<std::string>({}),
                                        std::vector<std::string>({})));

        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::WHITE_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            // row not found
            auto result2 = getMethodAuth(number++, Address(helloAddress), "get()");
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(1),
                                                          std::vector<std::string>({}),
                                                          std::vector<std::string>({})));
        }

        // can't get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test1");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // open white list, only 0x1234567890123456789012345678901234567890 address can use
        {
            auto result4 = modifyMethodAuth(_number++, 1000,
                "openMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(number++, Address(helloAddress), "get()");
            BOOST_CHECK(result2->data().toBytes() ==
                        codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE),
                            std::vector<std::string>({"1234567890123456789012345678901234567890"}),
                            std::vector<std::string>({})));
        }

        // get permission denied
        {
            auto result5 = helloGet(_number++, 1000);
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }

        // can still set
        {
            auto result6 = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result6->status() == (int32_t)TransactionStatus::None);
        }

        // use address 0x1234567890123456789012345678901234567890, success get
        {
            auto result7 =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result7->data().toBytes() == codec->encode(std::string("test2")));
        }

        // close white list, 0x1234567890123456789012345678901234567890 address can not use
        {
            auto result4 = modifyMethodAuth(_number++, 1000,
                "closeMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(number++, Address(helloAddress), "get()");
            BOOST_CHECK(
                result2->data().toBytes() ==
                codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE), std::vector<std::string>({}),
                    std::vector<std::string>({"1234567890123456789012345678901234567890"})));
        }

        // use address 0x1234567890123456789012345678901234567890 get permission denied
        {
            auto result5 =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }

        // use address 0x1234567890123456789012345678901234567890 still can set
        {
            auto result = helloSet(
                _number++, 1000, "test2", 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }
    }
}

BOOST_AUTO_TEST_CASE(testMethodBlackList)
{
    deployHello();
    // simple get
    {
        auto result = helloGet(3, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 4;
        // not found
        auto r1 = getMethodAuth(_number++, Address(helloAddress), "set(string)");
        BOOST_CHECK(
            r1->data().toBytes() == codec->encode((uint8_t)(0), std::vector<std::string>({}),
                                        std::vector<std::string>({})));

        auto r2 = getMethodAuth(_number++, Address(helloAddress), "get()");
        BOOST_CHECK(
            r2->data().toBytes() == codec->encode((uint8_t)(0), std::vector<std::string>({}),
                                        std::vector<std::string>({})));

        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(_number++, Address(helloAddress), "get()");
            BOOST_CHECK(
                result2->data().toBytes() == codec->encode((uint8_t)(2), std::vector<std::string>({}),
                                            std::vector<std::string>({})));
        }

        // still can get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
        }

        // still can set, even if not set any acl
        {
            auto result = helloSet(_number++, 1000, "test1");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // still can get now, even if not set any acl
        {
            auto result = helloGet(_number++, 1000, 0);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test1")));
        }

        // open black list, block 0x1234567890123456789012345678901234567890 address usage
        {
            auto result = modifyMethodAuth(_number++, 1000,
                "openMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(_number++, Address(helloAddress), "get()");
            BOOST_CHECK(result2->data().toBytes() ==
                        codec->encode((uint8_t)(AuthType::BLACK_LIST_MODE),
                            std::vector<std::string>({"1234567890123456789012345678901234567890"}),
                            std::vector<std::string>({})));
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // can still get with default address
        {
            auto result = helloGet(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }

        // use address 0x1234567890123456789012345678901234567890, still can get
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }

        // close black list, 0x1234567890123456789012345678901234567890 address block
        {
            auto result4 = modifyMethodAuth(_number++, 1000,
                "closeMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(_number++, Address(helloAddress), "get()");
            BOOST_CHECK(
                result2->data().toBytes() ==
                codec->encode((uint8_t)(AuthType::BLACK_LIST_MODE), std::vector<std::string>({}),
                    std::vector<std::string>({"1234567890123456789012345678901234567890"})));
        }

        // use address 0x1234567890123456789012345678901234567890, get success
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
    }
}

BOOST_AUTO_TEST_CASE(testResetAdmin)
{
    deployHello();
    // simple get
    {
        auto result = helloGet(2, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        BlockNumber _number = 3;
        // get admin
        {
            auto result = getAdmin(_number++, 1000, Address(helloAddress));
            BOOST_CHECK(result->data().toBytes() ==
                        codec->encode(Address("11ac3ca85a307ae2aff614e83949ab691ba019c5")));
        }
        // get admin in wrong address
        {
            auto result =
                getAdmin(_number++, 1000, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PrecompiledError);
        }
        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // close black list, block 0x1234567890123456789012345678901234567890 address usage
        {
            auto result = modifyMethodAuth(_number++, 1000,
                "closeMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // can still set
        {
            auto result = helloSet(_number++, 1000, "test2");
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }

        // can still get with default address
        {
            auto result = helloGet(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("test2")));
        }

        // use address 0x1234567890123456789012345678901234567890, get permission denied
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // reset admin in error contract address
        {
            auto result =
                resetAdmin(_number++, 1000, Address("0x1234567890123456789012345678901234567890"),
                    Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PrecompiledError);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // reset admin
        {
            auto result = resetAdmin(_number++, 1000, Address(helloAddress),
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));
        }

        // get admin
        {
            auto result = getAdmin(_number++, 1000, Address(helloAddress));
            BOOST_CHECK(result->data().toBytes() ==
                        codec->encode(Address("0x1234567890123456789012345678901234567890")));
        }

        // open black list, permission denied, new admin effect
        {
            auto result = modifyMethodAuth(_number++, 1000,
                "openMethodAuth(address,bytes4,address)", Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256((int)CODE_NO_AUTHORIZED)));
        }

        // use address 0x1234567890123456789012345678901234567890, still permission denied
        {
            auto result =
                helloGet(_number++, 1000, 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
    }
}

BOOST_AUTO_TEST_CASE(testDeployWhiteList)
{
    // simple deploy
    deployHello();
    Address admin = Address("0x1234567890123456789012345678901234567890");

    // add deploy acl type
    {
        BlockNumber _number = 3;
        // set deploy acl type
        {
            auto result = setDeployType(_number++, 1000, AuthType::WHITE_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // cannot deploy
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9601235", _number++, admin, true);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
        // has auth? no
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(false));
        }
        // open deploy auth
        {
            auto result = openDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // has auth? yes
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(true));
        }
        // deploy ok
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9605431", _number++, admin, false);
            BOOST_CHECK(
                result->newEVMContractAddress() == "1234654b49838bd3e9466c85a4cc3428c9605431");
        }
        // close auth
        {
            auto result = closeDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // has auth? no
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(false));
        }
        // cannot deploy
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9605430", _number++, admin, true);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
        // get deploy type
        {
            auto result = getDeployType(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(1)));
        }
    }
}

BOOST_AUTO_TEST_CASE(testDeployBlackList)
{
    // simple deploy
    deployHello();
    Address admin = Address("0x1234567890123456789012345678901234567890");

    // add deploy acl type
    {
        BlockNumber _number = 3;
        // set deploy acl type
        {
            auto result = setDeployType(_number++, 1000, AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // can still deploy
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9601235", _number++, admin, false);
            BOOST_CHECK(
                result->newEVMContractAddress() == "1234654b49838bd3e9466c85a4cc3428c9601235");
        }
        // has auth? yes
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(true));
        }
        // close deploy auth
        {
            auto result = closeDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // has auth? no
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(false));
        }
        // deploy permission denied
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9605431", _number++, admin, true);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }
        // open auth
        {
            auto result = openDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(0)));
        }
        // has auth? yes
        {
            auto result = hasDeployAuth(_number++, 1000, admin);
            BOOST_CHECK(result->data().toBytes() == codec->encode(true));
        }
        // deploy ok
        {
            auto result = deployHelloInAuthCheck(
                "1234654b49838bd3e9466c85a4cc3428c9605430", _number++, admin, false);
            BOOST_CHECK(
                result->newEVMContractAddress() == "1234654b49838bd3e9466c85a4cc3428c9605430");
        }
        // get deploy type
        {
            auto result = getDeployType(_number++, 1000);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(2)));
        }
    }
}

BOOST_AUTO_TEST_CASE(testDeployCommitteeManagerAndCall)
{
    // clang-format off
    static const char* committeeBin = "60806040523480156200001157600080fd5b506040516200588a3803806200588a83398101604081905262000034916200022c565b83838383604051620000469062000103565b6200005594939291906200032f565b604051809103906000f08015801562000072573d6000803e3d6000fd5b50600080546001600160a01b0319166001600160a01b03929092169182179055604051309190620000a39062000111565b6001600160a01b03928316815291166020820152604001604051809103906000f080158015620000d7573d6000803e3d6000fd5b50600180546001600160a01b0319166001600160a01b039290921691909117905550620003d892505050565b611584806200247783390190565b611e8f80620039fb83390190565b634e487b7160e01b600052604160045260246000fd5b604051601f8201601f191681016001600160401b03811182821017156200016057620001606200011f565b604052919050565b60006001600160401b038211156200018457620001846200011f565b5060051b60200190565b600082601f830112620001a057600080fd5b81516020620001b9620001b38362000168565b62000135565b82815260059290921b84018101918181019086841115620001d957600080fd5b8286015b848110156200020a57805163ffffffff81168114620001fc5760008081fd5b8352918301918301620001dd565b509695505050505050565b805160ff811681146200022757600080fd5b919050565b600080600080608085870312156200024357600080fd5b84516001600160401b03808211156200025b57600080fd5b818701915087601f8301126200027057600080fd5b8151602062000283620001b38362000168565b82815260059290921b8401810191818101908b841115620002a357600080fd5b948201945b83861015620002da5785516001600160a01b0381168114620002ca5760008081fd5b82529482019490820190620002a8565b918a0151919850909350505080821115620002f457600080fd5b5062000303878288016200018e565b935050620003146040860162000215565b9150620003246060860162000215565b905092959194509250565b6080808252855190820181905260009060209060a0840190828901845b82811015620003735781516001600160a01b0316845292840192908401906001016200034c565b5050508381038285015286518082528783019183019060005b81811015620003b057835163ffffffff16835292840192918401916001016200038c565b505060ff871660408601529250620003c6915050565b60ff8316606083015295945050505050565b61208f80620003e86000396000f3fe608060405234801561001057600080fd5b50600436106100f55760003560e01c80637475f00f11610097578063bcfb9b6111610066578063bcfb9b61146101f8578063d978ffba1461020b578063e43581b814610240578063f675fdaa1461026357600080fd5b80637475f00f146101aa5780637a25132d146101bd57806385a6a091146101d05780639e3f4f4e146101e557600080fd5b8063614235f3116100d3578063614235f31461015e57806365012582146101715780636ba4790c146101845780637222b4a81461019757600080fd5b806303f19159146100fa578063185c1587146101205780633234f0e61461014b575b600080fd5b61010d610108366004611b32565b610276565b6040519081526020015b60405180910390f35b600054610133906001600160a01b031681565b6040516001600160a01b039091168152602001610117565b61010d610159366004611ba1565b610419565b61010d61016c366004611bdf565b6104db565b61010d61017f366004611cae565b610679565b61010d610192366004611d01565b61077a565b61010d6101a5366004611d31565b610851565b61010d6101b8366004611d4f565b610967565b61010d6101cb366004611d7f565b610b62565b6101e36101de366004611dec565b610c87565b005b61010d6101f3366004611e05565b610d13565b6101e3610206366004611e6d565b610e32565b61022e610219366004611dec565b60009081526002602052604090205460ff1690565b60405160ff9091168152602001610117565b61025361024e366004611e9d565b611705565b6040519015158152602001610117565b600154610133906001600160a01b031681565b600061028133611705565b6102a65760405162461bcd60e51b815260040161029d90611eba565b60405180910390fd5b60648460ff1611156103045760405162461bcd60e51b815260206004820152602160248201527f696e76616c69642072616e6765206f66207061727469636970617465735261746044820152606560f81b606482015260840161029d565b60648360ff1611156103585760405162461bcd60e51b815260206004820152601860248201527f696e76616c69642072616e6765206f662077696e526174650000000000000000604482015260640161029d565b604080516002808252606080830184529260009291906020830190803683370190505090506060868260008151811061039357610393611ee8565b602002602001019060ff16908160ff168152505085826001815181106103bb576103bb611ee8565b60ff9092166020928302919091018201526040805160e081018252600c8152309281019290925281018390526060810182905260006080820181905260a0820185905260c082015261040d818761177a565b98975050505050505050565b600061042433611705565b6104405760405162461bcd60e51b815260040161029d90611eba565b60408051600180825281830190925260009160208083019080368337019050509050606080868360008151811061047957610479611ee8565b6001600160a01b039283166020918202929092018101919091526040805160e081018252600b8152928a169183019190915281018390526060810182905263ffffffff8716608082015260a08101849052600160c082015261040d818761177a565b60006104e633611705565b6105025760405162461bcd60e51b815260040161029d90611eba565b8260ff166110056001600160a01b0316631749bea96040518163ffffffff1660e01b8152600401602060405180830381865afa158015610546573d6000803e3d6000fd5b505050506040513d601f19601f8201168201806040525081019061056a9190611efe565b14156105de5760405162461bcd60e51b815260206004820152603b60248201527f7468652063757272656e74206465706c6f79206175746820747970652069732060448201527f7468652073616d6520617320796f752077616e7420746f207365740000000000606482015260840161029d565b60408051600180825281830190925260609160009190602080830190803683370190505090506060858260008151811061061a5761061a611ee8565b60ff9092166020928302919091018201526040805160e081018252601581526110059281019290925281018390526060810182905260006080820181905260a0820185905260c082015261066e818761177a565b979650505050505050565b600061068433611705565b6106a05760405162461bcd60e51b815260040161029d90611eba565b60018351116106e15760405162461bcd60e51b815260206004820152600d60248201526c34b73b30b634b2103737b2329760991b604482015260640161029d565b6040805160018082528183019092526060918291600091816020015b60608152602001906001900390816106fd579050509050858160008151811061072857610728611ee8565b6020908102919091018101919091526040805160e08101825260348152611003928101929092528101839052606081018290526000608082015260a08101849052600160c082015261066e818761177a565b600061078533611705565b6107a15760405162461bcd60e51b815260040161029d90611eba565b6040805160018082528183019092526000916020808301908036833701905050905084816000815181106107d7576107d7611ee8565b60200260200101906001600160a01b031690816001600160a01b03168152505060608060006040518060e00160405280601660ff168152602001896001600160a01b03168152602001848152602001838152602001600063ffffffff168152602001858152602001881515815250905061040d818761177a565b600061085c33611705565b6108785760405162461bcd60e51b815260040161029d90611eba565b6001600160a01b0383166108ce5760405162461bcd60e51b815260206004820152601c60248201527f636f6e74726163742061646472657373206e6f74206578697374732e00000000604482015260640161029d565b60408051600180825281830190925260009160208083019080368337019050509050606080858360008151811061090757610907611ee8565b6001600160a01b039283166020918202929092018101919091526040805160e081018252600d81526001549093169183019190915281018390526060810182905260006080820181905260a0820185905260c082015261066e818761177a565b600061097233611705565b61098e5760405162461bcd60e51b815260040161029d90611eba565b6001600160a01b0383166109e45760405162461bcd60e51b815260206004820152601c60248201527f636f6e74726163742061646472657373206e6f74206578697374732e00000000604482015260640161029d565b6040516364efb22b60e01b81526001600160a01b0384166004820152611005906364efb22b90602401602060405180830381865afa158015610a2a573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610a4e9190611f17565b6001600160a01b0316846001600160a01b03161415610acc5760405162461bcd60e51b815260206004820152603460248201527f746865206163636f756e7420686173206265656e207468652061646d696e206f604482015273331031b7b731bab9393a1031b7b73a3930b1ba1760611b606482015260840161029d565b604080516001808252818301909252600091602080830190803683370190505090506060808683600081518110610b0557610b05611ee8565b6001600160a01b039283166020918202929092018101919091526040805160e081018252601f81529289169183019190915281018390526060810182905260006080820181905260a0820185905260c082015261040d818761177a565b6000610b6d33611705565b610b895760405162461bcd60e51b815260040161029d90611eba565b6001845111610bd05760405162461bcd60e51b815260206004820152601360248201527234b73b30b634b21035b2bc903632b733ba341760691b604482015260640161029d565b60408051600280825260608281019093528291600091816020015b6060815260200190600190039081610beb5790505090508681600081518110610c1657610c16611ee8565b60200260200101819052508581600181518110610c3557610c35611ee8565b6020908102919091018101919091526040805160e081018252602981526110009281019290925281018390526060810182905260006080820181905260a0820185905260c082015261040d818761177a565b610c9033611705565b610cac5760405162461bcd60e51b815260040161029d90611eba565b600154604051631068aa6d60e11b8152600481018390523360248201526001600160a01b03909116906320d154da90604401600060405180830381600087803b158015610cf857600080fd5b505af1158015610d0c573d6000803e3d6000fd5b5050505050565b6000610d1e33611705565b610d3a5760405162461bcd60e51b815260040161029d90611eba565b6001855111610d7b5760405162461bcd60e51b815260206004820152600d60248201526c34b73b30b634b2103737b2329760991b604482015260640161029d565b6040805160018082528183019092526060918291600091816020015b6060815260200190600190039081610d975790505090508781600081518110610dc257610dc2611ee8565b602002602001018190525060006040518060e00160405280603360ff1681526020016110036001600160a01b031681526020018481526020018381526020018963ffffffff1681526020018581526020018815158152509050610e25818761177a565b9998505050505050505050565b610e3b33611705565b610e575760405162461bcd60e51b815260040161029d90611eba565b6001546040516318ae72f160e11b81526004810184905282151560248201523360448201526000916001600160a01b03169063315ce5e2906064016020604051808303816000875af1158015610eb1573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610ed59190611f34565b9050610f2b6040518060e00160405280600060ff16815260200160006001600160a01b031681526020016060815260200160608152602001600063ffffffff168152602001606081526020016000151581525090565b8160ff16600214156116ff576000848152600260209081526040918290208054835160e08101855260ff82168082526101009092046001600160a01b0316818501526001830180548651818702810187018852818152939692959286019392830182828015610fd757602002820191906000526020600020906000905b825461010083900a900460ff16815260206001928301818104948501949093039092029101808411610fa85790505b5050505050815260200160028201805480602002602001604051908101604052809291908181526020016000905b828210156110b157838290600052602060002001805461102490611f51565b80601f016020809104026020016040519081016040528092919081815260200182805461105090611f51565b801561109d5780601f106110725761010080835404028352916020019161109d565b820191906000526020600020905b81548152906001019060200180831161108057829003601f168201915b505050505081526020019060010190611005565b50505090825250600382015463ffffffff16602080830191909152600483018054604080518285028101850182528281529401939283018282801561111f57602002820191906000526020600020905b81546001600160a01b03168152600190910190602001808311611101575b50505091835250506005919091015460ff90811615156020909201919091529092508116600b141561127e57608082015163ffffffff166111da57336001600160a01b03168260a0015160008151811061117b5761117b611ee8565b60200260200101516001600160a01b031614156111da5760405162461bcd60e51b815260206004820152601c60248201527f596f752063616e206e6f742072656d6f766520796f757273656c662100000000604482015260640161029d565b6000805460a084015180516001600160a01b039092169263f437695a9261120357611203611ee8565b602002602001015184608001516040518363ffffffff1660e01b81526004016112479291906001600160a01b0392909216825263ffffffff16602082015260400190565b600060405180830381600087803b15801561126157600080fd5b505af1158015611275573d6000803e3d6000fd5b50505050610d0c565b8060ff16600c14156113065760008054604084015180516001600160a01b03909216926399bc9c1b926112b3576112b3611ee8565b602002602001015184604001516001815181106112d2576112d2611ee8565b60200260200101516040518363ffffffff1660e01b815260040161124792919060ff92831681529116602082015260400190565b8060ff16600d14156113715760015460a083015180516001600160a01b039092169163290bc797919060009061133e5761133e611ee8565b60200260200101516040518263ffffffff1660e01b815260040161124791906001600160a01b0391909116815260200190565b8060ff166015141561141b576110056001600160a01b031663bb0aa40c83604001516000815181106113a5576113a5611ee8565b60200260200101516040518263ffffffff1660e01b81526004016113d2919060ff91909116815260200190565b6020604051808303816000875af11580156113f1573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906114159190611efe565b50610d0c565b8060ff16601614156114b4578160c001511561148c576110056001600160a01b031663615480998360a0015160008151811061145957611459611ee8565b60200260200101516040518263ffffffff1660e01b81526004016113d291906001600160a01b0391909116815260200190565b6110056001600160a01b03166356bd70848360a0015160008151811061145957611459611ee8565b8060ff16601f1415611527576110056001600160a01b031663c53057b483602001518460a001516000815181106114ed576114ed611ee8565b60200260200101516040518363ffffffff1660e01b81526004016113d29291906001600160a01b0392831681529116602082015260400190565b8060ff166029141561159f576110006001600160a01b031663bd291aef836060015160008151811061155b5761155b611ee8565b6020026020010151846060015160018151811061157a5761157a611ee8565b60200260200101516040518363ffffffff1660e01b81526004016113d2929190611fd9565b8060ff1660331415611690578160c001511561166857608082015163ffffffff16611610576110036001600160a01b0316632800efc083606001516000815181106115ec576115ec611ee8565b60200260200101516040518263ffffffff1660e01b81526004016113d29190612007565b6110036001600160a01b03166335916856836060015160008151811061163857611638611ee8565b6020026020010151846080015163ffffffff166040518363ffffffff1660e01b81526004016113d292919061201a565b6110036001600160a01b031663ce6fa5c5836060015160008151811061163857611638611ee8565b8060ff16603414156116c4576110036001600160a01b03166380599e4b83606001516000815181106115ec576115ec611ee8565b60405162461bcd60e51b815260206004820152601060248201526f3b37ba32903a3cb8329032b93937b91760811b604482015260640161029d565b50505050565b60008054604051631c86b03760e31b81526001600160a01b0384811660048301529091169063e43581b890602401602060405180830381865afa158015611750573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190611774919061203c565b92915050565b6001548251602084015160405163161cab7f60e11b815233600482015260ff90921660248301526001600160a01b039081166044830152606482018490526000928392911690632c3956fe906084016020604051808303816000875af11580156117e8573d6000803e3d6000fd5b505050506040513d601f19601f8201168201806040525081019061180c9190611efe565b60008181526002602090815260409182902087518154838a01516001600160a01b0316610100026001600160a81b031990911660ff90921691909117178155918701518051939450879361186692600185019201906118ec565b5060608201518051611882916002840191602090910190611992565b50608082015160038201805463ffffffff191663ffffffff90921691909117905560a082015180516118be9160048401916020909101906119eb565b5060c091909101516005909101805460ff19169115159190911790556118e5816001610e32565b9392505050565b82805482825590600052602060002090601f016020900481019282156119825791602002820160005b8382111561195357835183826101000a81548160ff021916908360ff1602179055509260200192600101602081600001049283019260010302611915565b80156119805782816101000a81549060ff0219169055600101602081600001049283019260010302611953565b505b5061198e929150611a40565b5090565b8280548282559060005260206000209081019282156119df579160200282015b828111156119df57825180516119cf918491602090910190611a55565b50916020019190600101906119b2565b5061198e929150611ac9565b828054828255906000526020600020908101928215611982579160200282015b8281111561198257825182546001600160a01b0319166001600160a01b03909116178255602090920191600190910190611a0b565b5b8082111561198e5760008155600101611a41565b828054611a6190611f51565b90600052602060002090601f016020900481019282611a835760008555611982565b82601f10611a9c57805160ff1916838001178555611982565b82800160010185558215611982579182015b82811115611982578251825591602001919060010190611aae565b8082111561198e576000611add8282611ae6565b50600101611ac9565b508054611af290611f51565b6000825580601f10611b02575050565b601f016020900490600052602060002090810190611b209190611a40565b50565b60ff81168114611b2057600080fd5b600080600060608486031215611b4757600080fd5b8335611b5281611b23565b92506020840135611b6281611b23565b929592945050506040919091013590565b6001600160a01b0381168114611b2057600080fd5b803563ffffffff81168114611b9c57600080fd5b919050565b600080600060608486031215611bb657600080fd5b8335611bc181611b73565b9250611bcf60208501611b88565b9150604084013590509250925092565b60008060408385031215611bf257600080fd5b8235611bfd81611b23565b946020939093013593505050565b634e487b7160e01b600052604160045260246000fd5b600082601f830112611c3257600080fd5b813567ffffffffffffffff80821115611c4d57611c4d611c0b565b604051601f8301601f19908116603f01168101908282118183101715611c7557611c75611c0b565b81604052838152866020858801011115611c8e57600080fd5b836020870160208301376000602085830101528094505050505092915050565b60008060408385031215611cc157600080fd5b823567ffffffffffffffff811115611cd857600080fd5b611ce485828601611c21565b95602094909401359450505050565b8015158114611b2057600080fd5b600080600060608486031215611d1657600080fd5b8335611d2181611b73565b92506020840135611b6281611cf3565b60008060408385031215611d4457600080fd5b8235611bfd81611b73565b600080600060608486031215611d6457600080fd5b8335611d6f81611b73565b92506020840135611b6281611b73565b600080600060608486031215611d9457600080fd5b833567ffffffffffffffff80821115611dac57600080fd5b611db887838801611c21565b94506020860135915080821115611dce57600080fd5b50611ddb86828701611c21565b925050604084013590509250925092565b600060208284031215611dfe57600080fd5b5035919050565b60008060008060808587031215611e1b57600080fd5b843567ffffffffffffffff811115611e3257600080fd5b611e3e87828801611c21565b945050611e4d60208601611b88565b92506040850135611e5d81611cf3565b9396929550929360600135925050565b60008060408385031215611e8057600080fd5b823591506020830135611e9281611cf3565b809150509250929050565b600060208284031215611eaf57600080fd5b81356118e581611b73565b6020808252601490820152733cb7ba9036bab9ba1031329033b7bb32b93737b960611b604082015260600190565b634e487b7160e01b600052603260045260246000fd5b600060208284031215611f1057600080fd5b5051919050565b600060208284031215611f2957600080fd5b81516118e581611b73565b600060208284031215611f4657600080fd5b81516118e581611b23565b600181811c90821680611f6557607f821691505b60208210811415611f8657634e487b7160e01b600052602260045260246000fd5b50919050565b6000815180845260005b81811015611fb257602081850181015186830182015201611f96565b81811115611fc4576000602083870101525b50601f01601f19169290920160200192915050565b604081526000611fec6040830185611f8c565b8281036020840152611ffe8185611f8c565b95945050505050565b6020815260006118e56020830184611f8c565b60408152600061202d6040830185611f8c565b90508260208301529392505050565b60006020828403121561204e57600080fd5b81516118e581611cf356fea2646970667358221220d0edd12095a8bc05990a2a662f476c242bd5efcccb398f03de1ea663a60d9e7664736f6c634300080b003360806040523480156200001157600080fd5b506040516200158438038062001584833981016040819052620000349162000724565b600080546001600160a01b031916331781555b84518163ffffffff161015620000c257620000ad858263ffffffff168151811062000076576200007662000827565b6020026020010151858363ffffffff168151811062000099576200009962000827565b6020026020010151620000fb60201b60201c565b80620000b98162000853565b91505062000047565b506004805461ffff191661010060ff8481169190910260ff191691909117908416179055620000f13362000287565b50505050620008c5565b6200010633620002f0565b620001465760405162461bcd60e51b815260206004820152600b60248201526a4f6e6c79206f776e65722160a81b60448201526064015b60405180910390fd5b63ffffffff8116620001f0576001600160a01b038216321415620001ad5760405162461bcd60e51b815260206004820152601c60248201527f596f752063616e206e6f742072656d6f766520796f757273656c66210000000060448201526064016200013d565b6001600160a01b0382166000908152600360209081526040909120805463ffffffff19169055620001ec906001908490620005c462000338821b17901c565b5050565b6200020b826001620004d660201b620007421790919060201c565b1562000240576001600160a01b0382166000908152600360205260409020805463ffffffff191663ffffffff83161790555050565b6001600160a01b0382166000908152600360209081526040909120805463ffffffff191663ffffffff8416179055620001ec906001908490620004f4811b6200076017901c565b6200029233620002f0565b620002ce5760405162461bcd60e51b815260206004820152600b60248201526a4f6e6c79206f776e65722160a81b60448201526064016200013d565b600080546001600160a01b0319166001600160a01b0392909216919091179055565b60006001600160a01b0382163014156200030c57506001919050565b6000546001600160a01b03838116911614156200032b57506001919050565b506000919050565b919050565b6001600160a01b038116600090815260208390526040902054620003ab5760405162461bcd60e51b815260206004820152602360248201527f4c6962416464726573735365743a2076616c756520646f65736e27742065786960448201526239ba1760e91b60648201526084016200013d565b6001600160a01b038116600090815260208390526040812054620003d2906001906200087a565b600184810154919250600091620003ea91906200087a565b9050600084600101828154811062000406576200040662000827565b6000918252602090912001546001860180546001600160a01b0390921692508291859081106200043a576200043a62000827565b600091825260209091200180546001600160a01b0319166001600160a01b03929092169190911790556200047083600162000894565b6001600160a01b038083166000908152602088905260408082209390935590861681529081205560018501805480620004ad57620004ad620008af565b600082815260209020810160001990810180546001600160a01b03191690550190555050505050565b6001600160a01b031660009081526020919091526040902054151590565b6001600160a01b038116620005565760405162461bcd60e51b815260206004820152602160248201527f4c6962416464726573735365743a2076616c75652063616e27742062652030786044820152600360fc1b60648201526084016200013d565b6001600160a01b03811660009081526020839052604090205415620005d65760405162461bcd60e51b815260206004820152602f60248201527f4c6962416464726573735365743a2076616c756520616c72656164792065786960448201526e39ba399034b7103a34329039b2ba1760891b60648201526084016200013d565b6001808301805491820181556000818152602080822090930180546001600160a01b039095166001600160a01b0319909516851790559054928152929052604090912055565b634e487b7160e01b600052604160045260246000fd5b604051601f8201601f191681016001600160401b03811182821017156200065d576200065d6200061c565b604052919050565b60006001600160401b038211156200068157620006816200061c565b5060051b60200190565b600082601f8301126200069d57600080fd5b81516020620006b6620006b08362000665565b62000632565b82815260059290921b84018101918181019086841115620006d657600080fd5b8286015b848110156200070757805163ffffffff81168114620006f95760008081fd5b8352918301918301620006da565b509695505050505050565b805160ff811681146200033357600080fd5b600080600080608085870312156200073b57600080fd5b84516001600160401b03808211156200075357600080fd5b818701915087601f8301126200076857600080fd5b815160206200077b620006b08362000665565b82815260059290921b8401810191818101908b8411156200079b57600080fd5b948201945b83861015620007d25785516001600160a01b0381168114620007c25760008081fd5b82529482019490820190620007a0565b918a0151919850909350505080821115620007ec57600080fd5b50620007fb878288016200068b565b9350506200080c6040860162000712565b91506200081c6060860162000712565b905092959194509250565b634e487b7160e01b600052603260045260246000fd5b634e487b7160e01b600052601160045260246000fd5b600063ffffffff808316818114156200087057620008706200083d565b6001019392505050565b6000828210156200088f576200088f6200083d565b500390565b60008219821115620008aa57620008aa6200083d565b500190565b634e487b7160e01b600052603160045260246000fd5b610caf80620008d56000396000f3fe608060405234801561001057600080fd5b50600436106100b45760003560e01c8063ac6c525111610071578063ac6c52511461014d578063b2bdfa7b1461017c578063b6fd9067146101a7578063cd5d2118146101b9578063e43581b8146101dc578063f437695a146101ef57600080fd5b806313af4035146100b957806322acb867146100ce5780635615696f146100f05780635e77fe201461010f578063965b9ff11461012757806399bc9c1b1461013a575b600080fd5b6100cc6100c7366004610959565b610202565b005b6100d6610252565b60405163ffffffff90911681526020015b60405180910390f35b6004546100fd9060ff1681565b60405160ff90911681526020016100e7565b610117610266565b6040516100e7949392919061097b565b6100d6610135366004610a33565b61036b565b6100cc610148366004610b09565b6103ea565b6100d661015b366004610959565b6001600160a01b031660009081526003602052604090205463ffffffff1690565b60005461018f906001600160a01b031681565b6040516001600160a01b0390911681526020016100e7565b6004546100fd90610100900460ff1681565b6101cc6101c7366004610959565b610432565b60405190151581526020016100e7565b6101cc6101ea366004610959565b610478565b6100cc6101fd366004610b3c565b61048b565b61020b33610432565b6102305760405162461bcd60e51b815260040161022790610b7c565b60405180910390fd5b600080546001600160a01b0319166001600160a01b0392909216919091179055565b60006102616101356001610875565b905090565b6000806060806102766001610875565b9150815167ffffffffffffffff81111561029257610292610a1d565b6040519080825280602002602001820160405280156102bb578160200160208202803683370190505b50905060005b825181101561035257600360008483815181106102e0576102e0610ba1565b60200260200101516001600160a01b03166001600160a01b0316815260200190815260200160002060009054906101000a900463ffffffff1682828151811061032b5761032b610ba1565b63ffffffff909216602092830291909101909101528061034a81610bcd565b9150506102c1565b5060045460ff8082169661010090920416945091925090565b600080805b83518163ffffffff1610156103e35760036000858363ffffffff168151811061039b5761039b610ba1565b6020908102919091018101516001600160a01b03168252810191909152604001600020546103cf9063ffffffff1683610be8565b9150806103db81610c10565b915050610370565b5092915050565b6103f333610432565b61040f5760405162461bcd60e51b815260040161022790610b7c565b6004805461ffff191661010060ff9384160260ff19161792909116919091179055565b60006001600160a01b03821630141561044d57506001919050565b6000546001600160a01b038381169116141561046b57506001919050565b506000919050565b919050565b6000610485600183610742565b92915050565b61049433610432565b6104b05760405162461bcd60e51b815260040161022790610b7c565b63ffffffff8116610546576001600160a01b0382163214156105145760405162461bcd60e51b815260206004820152601c60248201527f596f752063616e206e6f742072656d6f766520796f757273656c6621000000006044820152606401610227565b6001600160a01b0382166000908152600360205260409020805463ffffffff191690556105426001836105c4565b5050565b610551600183610742565b15610585576001600160a01b0382166000908152600360205260409020805463ffffffff191663ffffffff83161790555050565b6001600160a01b0382166000908152600360205260409020805463ffffffff191663ffffffff8381169190911790915561054290600190849061076016565b6105ce8282610742565b6106265760405162461bcd60e51b815260206004820152602360248201527f4c6962416464726573735365743a2076616c756520646f65736e27742065786960448201526239ba1760e91b6064820152608401610227565b6001600160a01b03811660009081526020839052604081205461064b90600190610c34565b6001848101549192506000916106619190610c34565b9050600084600101828154811061067a5761067a610ba1565b6000918252602090912001546001860180546001600160a01b0390921692508291859081106106ab576106ab610ba1565b600091825260209091200180546001600160a01b0319166001600160a01b03929092169190911790556106df836001610c4b565b6001600160a01b03808316600090815260208890526040808220939093559086168152908120556001850180548061071957610719610c63565b600082815260209020810160001990810180546001600160a01b03191690550190555050505050565b6001600160a01b031660009081526020919091526040902054151590565b6001600160a01b0381166107c05760405162461bcd60e51b815260206004820152602160248201527f4c6962416464726573735365743a2076616c75652063616e27742062652030786044820152600360fc1b6064820152608401610227565b6107ca8282610742565b1561082f5760405162461bcd60e51b815260206004820152602f60248201527f4c6962416464726573735365743a2076616c756520616c72656164792065786960448201526e39ba399034b7103a34329039b2ba1760891b6064820152608401610227565b6001808301805491820181556000818152602080822090930180546001600160a01b039095166001600160a01b0319909516851790559054928152929052604090912055565b600181015460609060009067ffffffffffffffff81111561089857610898610a1d565b6040519080825280602002602001820160405280156108c1578160200160208202803683370190505b50905060005b60018401548110156103e3578360010181815481106108e8576108e8610ba1565b9060005260206000200160009054906101000a90046001600160a01b031682828151811061091857610918610ba1565b6001600160a01b03909216602092830291909101909101528061093a81610bcd565b9150506108c7565b80356001600160a01b038116811461047357600080fd5b60006020828403121561096b57600080fd5b61097482610942565b9392505050565b60006080820160ff87168352602060ff8716818501526080604085015281865180845260a086019150828801935060005b818110156109d15784516001600160a01b0316835293830193918301916001016109ac565b50508481036060860152855180825290820192508186019060005b81811015610a0e57825163ffffffff16855293830193918301916001016109ec565b50929998505050505050505050565b634e487b7160e01b600052604160045260246000fd5b60006020808385031215610a4657600080fd5b823567ffffffffffffffff80821115610a5e57600080fd5b818501915085601f830112610a7257600080fd5b813581811115610a8457610a84610a1d565b8060051b604051601f19603f83011681018181108582111715610aa957610aa9610a1d565b604052918252848201925083810185019188831115610ac757600080fd5b938501935b82851015610aec57610add85610942565b84529385019392850192610acc565b98975050505050505050565b803560ff8116811461047357600080fd5b60008060408385031215610b1c57600080fd5b610b2583610af8565b9150610b3360208401610af8565b90509250929050565b60008060408385031215610b4f57600080fd5b610b5883610942565b9150602083013563ffffffff81168114610b7157600080fd5b809150509250929050565b6020808252600b908201526a4f6e6c79206f776e65722160a81b604082015260600190565b634e487b7160e01b600052603260045260246000fd5b634e487b7160e01b600052601160045260246000fd5b6000600019821415610be157610be1610bb7565b5060010190565b600063ffffffff808316818516808303821115610c0757610c07610bb7565b01949350505050565b600063ffffffff80831681811415610c2a57610c2a610bb7565b6001019392505050565b600082821015610c4657610c46610bb7565b500390565b60008219821115610c5e57610c5e610bb7565b500190565b634e487b7160e01b600052603160045260246000fdfea264697066735822122085fafc65a9ded246b2c31779d206cec20aefb591774f8d42cc273420eb860a2664736f6c634300080b003360806040523480156200001157600080fd5b5060405162001e8f38038062001e8f8339810160408190526200003491620000e1565b600080546001600160a01b03191633179055604051829082906200005890620000b6565b6001600160a01b03928316815291166020820152604001604051809103906000f0801580156200008c573d6000803e3d6000fd5b50600180546001600160a01b0319166001600160a01b039290921691909117905550620001199050565b61087f806200161083390190565b80516001600160a01b0381168114620000dc57600080fd5b919050565b60008060408385031215620000f557600080fd5b6200010083620000c4565b91506200011060208401620000c4565b90509250929050565b6114e780620001296000396000f3fe608060405234801561001057600080fd5b50600436106101005760003560e01c8063401853b711610097578063bc903cb811610066578063bc903cb8146102e0578063cd5d211814610306578063dde248e314610329578063fcf81c141461034957600080fd5b8063401853b7146102725780636d23cd58146102995780636f2904cc146102ac578063b2bdfa7b146102b557600080fd5b806320d154da116100d357806320d154da14610209578063290bc7971461021c5780632c3956fe1461024c578063315ce5e21461025f57600080fd5b80630a4948401461010557806313af40351461019657806319dcd07e146101ab5780631cc05cbc146101d0575b600080fd5b610158610113366004611089565b600360208190526000918252604090912080546001820154600283015492909301546001600160a01b03918216939182169260ff600160a01b90930483169290911685565b604080516001600160a01b03968716815295909416602086015260ff92831693850193909352606084015216608082015260a0015b60405180910390f35b6101a96101a43660046110b9565b61035c565b005b6101be6101b9366004611089565b6103ac565b60405160ff909116815260200161018d565b6101fb6101de3660046110ed565b600460209081526000928352604080842090915290825290205481565b60405190815260200161018d565b6101a9610217366004611122565b610458565b6101a961022a3660046110b9565b600180546001600160a01b0319166001600160a01b0392909216919091179055565b6101fb61025a366004611145565b61056a565b6101be61026d366004611192565b6107ac565b6101be610280366004611089565b6000908152600360208190526040909120015460ff1690565b6101fb6102a73660046110ed565b610afc565b6101fb60025481565b6000546102c8906001600160a01b031681565b6040516001600160a01b03909116815260200161018d565b6102f36102ee366004611089565b610b2a565b60405161018d9796959493929190611219565b6103196103143660046110b9565b610c38565b604051901515815260200161018d565b61033c61033736600461127d565b610c7e565b60405161018d919061129f565b6001546102c8906001600160a01b031681565b61036533610c38565b61038a5760405162461bcd60e51b815260040161038190611377565b60405180910390fd5b600080546001600160a01b0319166001600160a01b0392909216919091179055565b600081815260036020819052604082200154829060ff166104045760405162461bcd60e51b8152602060048201526012602482015271141c9bdc1bdcd85b081b9bdd08195e1a5cdd60721b6044820152606401610381565b60008381526003602081905260409091209081015460ff166001141561044857806002015443111561044857600301805460ff191660059081179091559150610452565b6003015460ff1691505b50919050565b61046133610c38565b61047d5760405162461bcd60e51b815260040161038190611377565b6000828152600360205260409020610494836103ac565b60ff166001146104f95760405162461bcd60e51b815260206004820152602a60248201527f4f6e6c79206e65776c7920637265617465642070726f706f73616c2063616e206044820152691899481c995d9bdad95960b21b6064820152608401610381565b60018101546001600160a01b038381169116146105585760405162461bcd60e51b815260206004820152601860248201527f4f6e6c792070726f706f7365722063616e207265766f6b6500000000000000006044820152606401610381565b600301805460ff191660041790555050565b600061057533610c38565b6105915760405162461bcd60e51b815260040161038190611377565b60ff80851660009081526004602090815260408083206001600160a01b038816845282528083205480845260039283905292200154909116600114156105dc576105da816103ac565b505b6000818152600360208190526040909120015460ff16600114156106425760405162461bcd60e51b815260206004820152601860248201527f43757272656e742070726f706f73616c206e6f7420656e6400000000000000006044820152606401610381565b60028054906000610652836113b2565b91905055506000600254905060608060006040518060e00160405280896001600160a01b031681526020018b6001600160a01b031681526020018a60ff16815260200188436106a191906113cd565b815260016020808301829052604080840188905260609384018790526000898152600380845290829020865181546001600160a01b0319166001600160a01b0391821617825587850151958201805494890151969091166001600160a81b031990941693909317600160a01b60ff96871602179092559385015160028201556080850151938101805460ff1916949093169390931790915560a083015180519394508493610755926004850192019061100f565b5060c0820151805161077191600584019160209091019061100f565b50505060ff891660009081526004602090815260408083206001600160a01b038c168452909152902084905550919350505050949350505050565b60006107b733610c38565b6107d35760405162461bcd60e51b815260040161038190611377565b60008481526003602081905260409091200154849060ff1661082c5760405162461bcd60e51b8152602060048201526012602482015271141c9bdc1bdcd85b081b9bdd08195e1a5cdd60721b6044820152606401610381565b60008581526003602081905260409091200154859060ff166001146108935760405162461bcd60e51b815260206004820152601760248201527f50726f706f73616c206973206e6f7420766f7461626c650000000000000000006044820152606401610381565b600086815260036020818152604092839020835160e08101855281546001600160a01b03908116825260018301549081168285015260ff600160a01b9091048116828701526002830154606083015293820154909316608084015260048101805485518185028101850190965280865291946109bc9493869360a08601939183018282801561094b57602002820191906000526020600020905b81546001600160a01b0316815260019091019060200180831161092d575b50505050508152602001600582018054806020026020016040519081016040528092919081815260200182805480156109ad57602002820191906000526020600020905b81546001600160a01b0316815260019091019060200180831161098f575b50505050508152505086610f6f565b156109f95760405162461bcd60e51b815260206004820152600d60248201526c105b1c9958591e481d9bdd1959609a1b6044820152606401610381565b8515610a3457600481018054600181018255600091825260209091200180546001600160a01b0319166001600160a01b038716179055610a65565b600581018054600181018255600091825260209091200180546001600160a01b0319166001600160a01b0387161790555b6001546040516353bfcf2f60e01b81526000916001600160a01b0316906353bfcf2f90610a9d90600480870191600588019101611423565b602060405180830381865afa158015610aba573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610ade9190611451565b600392909201805460ff191660ff8416179055509695505050505050565b60ff821660009081526004602090815260408083206001600160a01b03851684529091529020545b92915050565b600081815260036020818152604092839020805460018201546002830154948301546004840180548851818802810188019099528089526001600160a01b039485169894841697600160a01b90940460ff908116979496931694606094859493919290830182828015610bc657602002820191906000526020600020905b81546001600160a01b03168152600190910190602001808311610ba8575b5050505050925080600501805480602002602001604051908101604052809291908181526020018280548015610c2557602002820191906000526020600020905b81546001600160a01b03168152600190910190602001808311610c07575b5050505050915050919395979092949650565b60006001600160a01b038216301415610c5357506001919050565b6000546001600160a01b0383811691161415610c7157506001919050565b506000919050565b919050565b6060600254831115610ce15760405162461bcd60e51b815260206004820152602660248201527f2766726f6d272069732067726561746572207468616e202770726f706f73616c604482015265436f756e742760d01b6064820152608401610381565b81831115610d315760405162461bcd60e51b815260206004820152601b60248201527f2766726f6d272069732067726561746572207468616e2027746f2700000000006044820152606401610381565b600254821115610d415760025491505b6000610d4d848461146e565b610d589060016113cd565b67ffffffffffffffff811115610d7057610d70611485565b604051908082528060200260200182016040528015610df957816020015b610de66040518060e0016040528060006001600160a01b0316815260200160006001600160a01b03168152602001600060ff16815260200160008152602001600060ff16815260200160608152602001606081525090565b815260200190600190039081610d8e5790505b5090506000845b848111610f6557600081815260036020818152604092839020835160e08101855281546001600160a01b03908116825260018301549081168285015260ff600160a01b909104811682870152600283015460608301529382015490931660808401526004810180548551818502810185019096528086529194859360a086019391929190830182828015610ebd57602002820191906000526020600020905b81546001600160a01b03168152600190910190602001808311610e9f575b5050505050815260200160058201805480602002602001604051908101604052809291908181526020018280548015610f1f57602002820191906000526020600020905b81546001600160a01b03168152600190910190602001808311610f01575b505050505081525050848480610f34906113b2565b955081518110610f4657610f4661149b565b6020026020010181905250508080610f5d906113b2565b915050610e00565b5090949350505050565b6000610f7f8360a0015183610fa9565b80610f935750610f938360c0015183610fa9565b15610fa057506001610b24565b50600092915050565b6000805b835181101561100557838181518110610fc857610fc861149b565b60200260200101516001600160a01b0316836001600160a01b03161415610ff3576001915050610b24565b80610ffd816113b2565b915050610fad565b5060009392505050565b828054828255906000526020600020908101928215611064579160200282015b8281111561106457825182546001600160a01b0319166001600160a01b0390911617825560209092019160019091019061102f565b50611070929150611074565b5090565b5b808211156110705760008155600101611075565b60006020828403121561109b57600080fd5b5035919050565b80356001600160a01b0381168114610c7957600080fd5b6000602082840312156110cb57600080fd5b6110d4826110a2565b9392505050565b60ff811681146110ea57600080fd5b50565b6000806040838503121561110057600080fd5b823561110b816110db565b9150611119602084016110a2565b90509250929050565b6000806040838503121561113557600080fd5b82359150611119602084016110a2565b6000806000806080858703121561115b57600080fd5b611164856110a2565b93506020850135611174816110db565b9250611182604086016110a2565b9396929550929360600135925050565b6000806000606084860312156111a757600080fd5b83359250602084013580151581146111be57600080fd5b91506111cc604085016110a2565b90509250925092565b600081518084526020808501945080840160005b8381101561120e5781516001600160a01b0316875295820195908201906001016111e9565b509495945050505050565b6001600160a01b0388811682528716602082015260ff8681166040830152606082018690528416608082015260e060a0820181905260009061125d908301856111d5565b82810360c084015261126f81856111d5565b9a9950505050505050505050565b6000806040838503121561129057600080fd5b50508035926020909101359150565b60006020808301818452808551808352604092508286019150828160051b87010184880160005b8381101561136957888303603f19018552815180516001600160a01b0390811685528882015116888501528681015160ff16878501526060808201519085015260808082015160e0919061131e8288018260ff169052565b505060a0808301518282880152611337838801826111d5565b9250505060c0808301519250858203818701525061135581836111d5565b9689019694505050908601906001016112c6565b509098975050505050505050565b6020808252600b908201526a4f6e6c79206f776e65722160a81b604082015260600190565b634e487b7160e01b600052601160045260246000fd5b60006000198214156113c6576113c661139c565b5060010190565b600082198211156113e0576113e061139c565b500190565b6000815480845260208085019450836000528060002060005b8381101561120e5781546001600160a01b0316875295820195600191820191016113fe565b60408152600061143660408301856113e5565b828103602084015261144881856113e5565b95945050505050565b60006020828403121561146357600080fd5b81516110d4816110db565b6000828210156114805761148061139c565b500390565b634e487b7160e01b600052604160045260246000fd5b634e487b7160e01b600052603260045260246000fdfea2646970667358221220cc70688aadc13b5bad54a84a87497145e3edb9633d0f038136d88fb0da095e3b64736f6c634300080b0033608060405234801561001057600080fd5b5060405161087f38038061087f83398101604081905261002f91610136565b600080546001600160a01b0319163317905561004a82610070565b600180546001600160a01b0319166001600160a01b039290921691909117905550610169565b610079336100d9565b6100b75760405162461bcd60e51b815260206004820152600b60248201526a4f6e6c79206f776e65722160a81b604482015260640160405180910390fd5b600080546001600160a01b0319166001600160a01b0392909216919091179055565b60006001600160a01b0382163014156100f457506001919050565b6000546001600160a01b038381169116141561011257506001919050565b506000919050565b919050565b80516001600160a01b038116811461011a57600080fd5b6000806040838503121561014957600080fd5b6101528361011f565b91506101606020840161011f565b90509250929050565b610707806101786000396000f3fe608060405234801561001057600080fd5b50600436106100575760003560e01c806313af40351461005c578063185c15871461007157806353bfcf2f146100a1578063b2bdfa7b146100c6578063cd5d2118146100d9575b600080fd5b61006f61006a366004610493565b6100fc565b005b600154610084906001600160a01b031681565b6040516001600160a01b0390911681526020015b60405180910390f35b6100b46100af36600461056d565b610165565b60405160ff9091168152602001610098565b600054610084906001600160a01b031681565b6100ec6100e7366004610493565b610436565b6040519015158152602001610098565b61010533610436565b6101435760405162461bcd60e51b815260206004820152600b60248201526a4f6e6c79206f776e65722160a81b604482015260640160405180910390fd5b600080546001600160a01b0319166001600160a01b0392909216919091179055565b60015460405163965b9ff160e01b815260009182916001600160a01b039091169063965b9ff19061019a9087906004016105d1565b602060405180830381865afa1580156101b7573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906101db919061061e565b60015460405163965b9ff160e01b81529192506000916001600160a01b039091169063965b9ff1906102119087906004016105d1565b602060405180830381865afa15801561022e573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610252919061061e565b61025c908361065a565b90506000600160009054906101000a90046001600160a01b03166001600160a01b03166322acb8676040518163ffffffff1660e01b8152600401602060405180830381865afa1580156102b3573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906102d7919061061e565b9050600160009054906101000a90046001600160a01b03166001600160a01b0316635615696f6040518163ffffffff1660e01b8152600401602060405180830381865afa15801561032c573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906103509190610682565b61035d9060ff16826106a5565b63ffffffff1661036e8360646106a5565b63ffffffff1610156103865760019350505050610430565b6001546040805163b6fd906760e01b8152905184926001600160a01b03169163b6fd90679160048083019260209291908290030181865afa1580156103cf573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906103f39190610682565b60ff1661040091906106a5565b63ffffffff166104118460646106a5565b63ffffffff16106104285760029350505050610430565b600393505050505b92915050565b60006001600160a01b03821630141561045157506001919050565b6000546001600160a01b038381169116141561046f57506001919050565b506000919050565b919050565b80356001600160a01b038116811461047757600080fd5b6000602082840312156104a557600080fd5b6104ae8261047c565b9392505050565b634e487b7160e01b600052604160045260246000fd5b600082601f8301126104dc57600080fd5b8135602067ffffffffffffffff808311156104f9576104f96104b5565b8260051b604051601f19603f8301168101818110848211171561051e5761051e6104b5565b60405293845285810183019383810192508785111561053c57600080fd5b83870191505b84821015610562576105538261047c565b83529183019190830190610542565b979650505050505050565b6000806040838503121561058057600080fd5b823567ffffffffffffffff8082111561059857600080fd5b6105a4868387016104cb565b935060208501359150808211156105ba57600080fd5b506105c7858286016104cb565b9150509250929050565b6020808252825182820181905260009190848201906040850190845b818110156106125783516001600160a01b0316835292840192918401916001016105ed565b50909695505050505050565b60006020828403121561063057600080fd5b815163ffffffff811681146104ae57600080fd5b634e487b7160e01b600052601160045260246000fd5b600063ffffffff80831681851680830382111561067957610679610644565b01949350505050565b60006020828403121561069457600080fd5b815160ff811681146104ae57600080fd5b600063ffffffff808316818516818304811182151516156106c8576106c8610644565b0294935050505056fea26469706673582212205c6a73943b75ecf9355fe7924a4efe36d01f7730bed74a2f2d507c93026ece1b64736f6c634300080b0033";
    // clang-format on

    Address admin = Address("0x1111654b49838bd3e9466c85a4cc3428c9601111");

    // deploy CommitteeManager
    {
        // hex bin code to bytes
        bytes code;
        boost::algorithm::unhex(committeeBin, std::back_inserter(code));

        // constructor (address[] initGovernors,    = [authAdminAddress]
        //        uint32[] memory weights,          = [0]
        //        uint8 participatesRate,           = 0
        //        uint8 winRate)                    = 0
        std::vector<Address> initGovernors({admin});
        std::vector<string32> weights({bcos::codec::toString32(h256(uint8_t(1)))});
        // bytes code + abi encode constructor params
        codec::abi::ContractABICodec abi(hashImpl);
        bytes constructor = abi.abiIn("", initGovernors, weights,
            codec::toString32(h256(uint8_t(0))), codec::toString32(h256(uint8_t(0))));
        bytes input = code + constructor;

        std::string cs = *toHexString(constructor);

        sender = admin.hex();

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(precompiled::AUTH_COMMITTEE_ADDRESS);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::MESSAGE);
        params->setCreate(true);
        params->setData(input);

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

        result->setSeq(1001);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1000);
        /// callback get deploy committeeManager context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->create(), true);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result3->setSeq(1002);
        result3->setTo("1111111111111111111111111111111111111111");
        result3->setKeyLocks({});
        /// new committee
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });

        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1003);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });

        auto result5 = executePromise5.get_future().get();

        result5->setSeq(1002);
        /// callback get deploy committee context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise6;
        executor->executeTransaction(
            std::move(result5), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise6.set_value(std::move(result));
            });

        auto result6 = executePromise6.get_future().get();

        BOOST_CHECK_EQUAL(result6->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result6->contextID(), 99);
        BOOST_CHECK_EQUAL(result6->seq(), 1002);
        BOOST_CHECK_EQUAL(result6->create(), false);
        BOOST_CHECK_EQUAL(
            result6->newEVMContractAddress(), "1111111111111111111111111111111111111111");
        BOOST_CHECK_EQUAL(result6->origin(), sender);
        BOOST_CHECK_EQUAL(result6->to(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result6->setSeq(1000);
        // new committee address => committeeManager
        // new proposalManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise7;
        executor->executeTransaction(
            std::move(result6), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise7.set_value(std::move(result));
            });

        auto result7 = executePromise7.get_future().get();

        BOOST_CHECK_EQUAL(result7->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result7->contextID(), 99);
        BOOST_CHECK_EQUAL(result7->seq(), 1000);
        BOOST_CHECK_EQUAL(result7->create(), true);
        BOOST_CHECK_EQUAL(result7->origin(), sender);
        BOOST_CHECK_EQUAL(result7->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result7->setSeq(1004);
        result7->setTo("2222222222222222222222222222222222222222");
        result7->setKeyLocks({});

        // new proposalManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise8;
        executor->executeTransaction(
            std::move(result7), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise8.set_value(std::move(result));
            });

        auto result8 = executePromise8.get_future().get();

        result8->setSeq(1005);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise9;
        executor->executeTransaction(
            std::move(result8), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise9.set_value(std::move(result));
            });

        auto result9 = executePromise9.get_future().get();

        result9->setSeq(1004);
        /// callback get deploy committee context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise10;
        executor->executeTransaction(
            std::move(result9), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise10.set_value(std::move(result));
            });

        auto result10 = executePromise10.get_future().get();

        result10->setSeq(1006);
        result10->setTo("3333333333333333333333333333333333333333");
        result10->setKeyLocks({});

        /// new voteComputer
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise11;
        executor->executeTransaction(
            std::move(result10), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise11.set_value(std::move(result));
            });

        auto result11 = executePromise11.get_future().get();

        result11->setSeq(1007);

        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise12;
        executor->executeTransaction(
            std::move(result11), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise12.set_value(std::move(result));
            });

        auto result12 = executePromise12.get_future().get();

        result12->setSeq(1006);

        /// callback get deploy committee context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise13;
        executor->executeTransaction(
            std::move(result12), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise13.set_value(std::move(result));
            });

        auto result13 = executePromise13.get_future().get();

        BOOST_CHECK_EQUAL(result13->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result13->contextID(), 99);
        BOOST_CHECK_EQUAL(result13->seq(), 1006);
        BOOST_CHECK_EQUAL(result13->create(), false);
        BOOST_CHECK_EQUAL(result13->origin(), sender);
        BOOST_CHECK_EQUAL(
            result13->newEVMContractAddress(), "3333333333333333333333333333333333333333");
        BOOST_CHECK_EQUAL(result13->to(), "2222222222222222222222222222222222222222");

        result13->setSeq(1004);

        // new voteCompute => new ProposalMgr context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise14;
        executor->executeTransaction(
            std::move(result13), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise14.set_value(std::move(result));
            });

        auto result14 = executePromise14.get_future().get();

        BOOST_CHECK_EQUAL(result14->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result14->contextID(), 99);
        BOOST_CHECK_EQUAL(result14->seq(), 1004);
        BOOST_CHECK_EQUAL(result14->create(), false);
        BOOST_CHECK_EQUAL(result14->origin(), sender);
        BOOST_CHECK_EQUAL(
            result14->newEVMContractAddress(), "2222222222222222222222222222222222222222");
        BOOST_CHECK_EQUAL(result14->to(), AUTH_COMMITTEE_ADDRESS);

        result14->setSeq(1000);
        // new proposalManager address => committeeManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise15;
        executor->executeTransaction(
            std::move(result14), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise15.set_value(std::move(result));
            });

        auto result15 = executePromise15.get_future().get();
        BOOST_CHECK_EQUAL(result15->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result15->contextID(), 99);
        BOOST_CHECK_EQUAL(result15->seq(), 1000);
        BOOST_CHECK_EQUAL(result15->create(), false);
        BOOST_CHECK_EQUAL(result15->origin(), sender);
        BOOST_CHECK_EQUAL(result15->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        commitBlock(1);
    }

    // call CommitteeManager
    {
        nextBlock(2);
        bytes in = codec->encodeWithSig("createUpdateGovernorProposal(address,uint32,uint256)",
            admin, codec::toString32(h256(uint32_t(2))), u256(2));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");

        tx->forceSender(admin.asBytes());

        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        // force cover write
        txpool->hash2Transaction[hash] = tx;
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::AUTH_COMMITTEE_ADDRESS);
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

        // proposalManager.create
        result2->setSeq(1001);
        result2->setKeyLocks({});
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        //
        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        commitBlock(2);
    }
}

BOOST_AUTO_TEST_CASE(testDeployAdmin)
{
    BlockNumber _number = 3;
    Address admin = Address("0x1234567890123456789012345678901234567890");
    auto result =
        deployHello2InAuthCheck("1234654b49838bd3e9466c85a4cc3428c9601235", _number++, admin);
    // test deploy admin
    {
        auto result1 =
            getAdmin(_number++, 1000, Address("1234654b49838bd3e9466c85a4cc3428c9601235"));
        BOOST_CHECK(result1->data().toBytes() == codec->encode(admin));
    }

    // test external deploy admin
    {
        auto result1 = getAdmin(_number++, 1000, Address(hello2Address));
        std::cout << toHexStringWithPrefix(result1->data().toBytes()) << std::endl;
        BOOST_CHECK(result1->data().toBytes() == codec->encode(admin));
    }
}

BOOST_AUTO_TEST_CASE(testContractStatus)
{
    deployHello();
    BlockNumber _number = 3;
    // frozen
    {
        auto r1 = setContractStatus(_number++, Address(helloAddress), true);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));

        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(false));
    }
    // frozen, revert
    {
        auto result = helloGet(_number++, 1000);
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::ContractFrozen);

        auto result2 = helloSet(_number++, 1000, "");
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::ContractFrozen);
    }
    // switch normal
    {
        auto r1 = setContractStatus(_number++, Address(helloAddress), false);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));
        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(true));
    }
    // normal
    {
        auto result = helloGet(_number++, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));

        auto result2 = helloSet(_number++, 1000, "test");
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::None);

        auto result3 = helloGet(_number++, 1000);
        BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string("test")));
    }

    // contract address not found
    {
        auto errorAddress = "123456";
        auto result2 = contractAvailable(_number++, Address(errorAddress));
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(testContractStatusInKeyPage)
{
    setIsWasm(false, true, true);

    deployHello();
    BlockNumber _number = 3;
    // frozen
    {
        auto r1 = setContractStatus(_number++, Address(helloAddress), true);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));

        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(false));
    }
    // frozen, revert
    {
        auto result = helloGet(_number++, 1000);
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::ContractFrozen);

        auto result2 = helloSet(_number++, 1000, "");
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::ContractFrozen);
    }
    // switch normal
    {
        auto r1 = setContractStatus(_number++, Address(helloAddress), false);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));
        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(true));
    }
    // normal
    {
        auto result = helloGet(_number++, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));

        auto result2 = helloSet(_number++, 1000, "test");
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::None);

        auto result3 = helloGet(_number++, 1000);
        BOOST_CHECK(result3->data().toBytes() == codec->encode(std::string("test")));
    }

    // contract address not found
    {
        auto errorAddress = "123456";
        auto result2 = contractAvailable(_number++, Address(errorAddress));
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test