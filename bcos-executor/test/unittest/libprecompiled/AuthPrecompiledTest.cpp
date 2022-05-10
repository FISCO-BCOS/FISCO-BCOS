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
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
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
        BOOST_CHECK(r1->status() == (int)TransactionStatus::PrecompiledError);

        auto r2 = getMethodAuth(number++, Address(helloAddress), "get()");
        BOOST_CHECK(r2->status() == (int)TransactionStatus::PrecompiledError);

        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::WHITE_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            // row not found
            auto result2 = getMethodAuth(number++, Address(helloAddress), "get()");
            BOOST_CHECK(result2->status() == (int)TransactionStatus::PrecompiledError);
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE),
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE),
                                                         std::vector<std::string>({}),
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
        BOOST_CHECK(r1->status() == (int)TransactionStatus::PrecompiledError);

        auto r2 = getMethodAuth(_number++, Address(helloAddress), "get()");
        BOOST_CHECK(r2->status() == (int)TransactionStatus::PrecompiledError);

        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, Address(helloAddress), "get()", AuthType::BLACK_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(_number++, Address(helloAddress), "get()");
            BOOST_CHECK(result2->status() == (int)TransactionStatus::PrecompiledError);
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(AuthType::BLACK_LIST_MODE),
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(AuthType::BLACK_LIST_MODE),
                                                         std::vector<std::string>({}),
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
            BOOST_CHECK(result->data().toBytes() == codec->encode(Address()));
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
    static const char* committeeBin = "60806040523480156200001157600080fd5b506040516200645138038062006451833981810160405260808110156200003757600080fd5b81019080805160405193929190846401000000008211156200005857600080fd5b838201915060208201858111156200006f57600080fd5b82518660208202830111640100000000821117156200008d57600080fd5b8083526020830192505050908051906020019060200280838360005b83811015620000c6578082015181840152602081019050620000a9565b5050505090500160405260200180516040519392919084640100000000821115620000f057600080fd5b838201915060208201858111156200010757600080fd5b82518660208202830111640100000000821117156200012557600080fd5b8083526020830192505050908051906020019060200280838360005b838110156200015e57808201518184015260208101905062000141565b505050509050016040526020018051906020019092919080519060200190929190505050611005600260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555083838383604051620001d790620003e7565b8080602001806020018560ff1660ff1681526020018460ff1660ff168152602001838103835287818151815260200191508051906020019060200280838360005b838110156200023557808201518184015260208101905062000218565b50505050905001838103825286818151815260200191508051906020019060200280838360005b83811015620002795780820151818401526020810190506200025c565b505050509050019650505050505050604051809103906000f080158015620002a5573d6000803e3d6000fd5b506000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550306000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff166040516200031690620003f5565b808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200192505050604051809103906000f0801580156200039c573d6000803e3d6000fd5b50600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505050505062000403565b611dd480620028bc83390190565b611dc1806200469083390190565b6124a980620004136000396000f3fe608060405234801561001057600080fd5b50600436106100b45760003560e01c80637475f00f116100715780637475f00f146102d857806385a6a0911461035a578063bcfb9b6114610388578063d978ffba146103c2578063e43581b81461040a578063f675fdaa14610466576100b4565b806303f19159146100b9578063185c1587146101155780632158671f1461015f5780633234f0e6146101a9578063614235f31461021b5780636ba4790c1461026a575b600080fd5b6100ff600480360360608110156100cf57600080fd5b81019080803560ff169060200190929190803560ff169060200190929190803590602001909291905050506104b0565b6040518082815260200191505060405180910390f35b61011d610726565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b61016761074b565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b610205600480360360608110156101bf57600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803563ffffffff16906020019092919080359060200190929190505050610771565b6040518082815260200191505060405180910390f35b6102546004803603604081101561023157600080fd5b81019080803560ff169060200190929190803590602001909291905050506108f4565b6040518082815260200191505060405180910390f35b6102c26004803603606081101561028057600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff16906020019092919080351515906020019092919080359060200190929190505050610b70565b6040518082815260200191505060405180910390f35b610344600480360360608110156102ee57600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff16906020019092919080359060200190929190505050610f65565b6040518082815260200191505060405180910390f35b6103866004803603602081101561037057600080fd5b81019080803590602001909291905050506112ea565b005b6103c06004803603604081101561039e57600080fd5b8101908080359060200190929190803515159060200190929190505050611429565b005b6103ee600480360360208110156103d857600080fd5b8101908080359060200190929190505050611e16565b604051808260ff1660ff16815260200191505060405180910390f35b61044c6004803603602081101561042057600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050611e43565b604051808215151515815260200191505060405180910390f35b61046e611f25565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b60006104bb33611e43565b61052d576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b60008460ff1610158015610545575060648460ff1611155b61059a576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602181526020018061241f6021913960400191505060405180910390fd5b60008360ff16101580156105b2575060648360ff1611155b610624576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260188152602001807f696e76616c69642072616e6765206f662077696e52617465000000000000000081525060200191505060405180910390fd5b606080600267ffffffffffffffff8111801561063f57600080fd5b5060405190808252806020026020018201604052801561066e5781602001602082028036833780820191505090505b509050858160008151811061067f57fe5b602002602001019060ff16908160ff168152505084816001815181106106a157fe5b602002602001019060ff16908160ff16815250506106bd612193565b6040518060c00160405280600c60ff1681526020013073ffffffffffffffffffffffffffffffffffffffff168152602001838152602001600063ffffffff16815260200184815260200160001515815250905061071a8186611f4b565b93505050509392505050565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600061077c33611e43565b6107ee576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b6060600167ffffffffffffffff8111801561080857600080fd5b506040519080825280602002602001820160405280156108375781602001602082028036833780820191505090505b5090506060858260008151811061084a57fe5b602002602001019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff168152505061088c612193565b6040518060c00160405280600b60ff1681526020018873ffffffffffffffffffffffffffffffffffffffff1681526020018381526020018763ffffffff1681526020018481526020016001151581525090506108e88186611f4b565b93505050509392505050565b60006108ff33611e43565b610971576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b8260ff16600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16631749bea96040518163ffffffff1660e01b815260040160206040518083038186803b1580156109dd57600080fd5b505afa1580156109f1573d6000803e3d6000fd5b505050506040513d6020811015610a0757600080fd5b81019080805190602001909291905050501415610a6f576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252603b8152602001806123b9603b913960400191505060405180910390fd5b606080600167ffffffffffffffff81118015610a8a57600080fd5b50604051908082528060200260200182016040528015610ab95781602001602082028036833780820191505090505b5090508481600081518110610aca57fe5b602002602001019060ff16908160ff1681525050610ae6612193565b6040518060c00160405280601560ff168152602001600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001838152602001600063ffffffff168152602001848152602001600015158152509050610b658186611f4b565b935050505092915050565b6000610b7b33611e43565b610bed576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b828015610cd15750600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663630577e5856040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060206040518083038186803b158015610c9457600080fd5b505afa158015610ca8573d6000803e3d6000fd5b505050506040513d6020811015610cbe57600080fd5b8101908080519060200190929190505050155b610d26576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602b8152602001806123f4602b913960400191505060405180910390fd5b82158015610e0a5750600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663630577e5856040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060206040518083038186803b158015610dce57600080fd5b505afa158015610de2573d6000803e3d6000fd5b505050506040513d6020811015610df857600080fd5b81019080805190602001909291905050505b610e5f576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602a81526020018061238f602a913960400191505060405180910390fd5b6060600167ffffffffffffffff81118015610e7957600080fd5b50604051908082528060200260200182016040528015610ea85781602001602082028036833780820191505090505b5090508481600081518110610eb957fe5b602002602001019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff16815250506060610efd612193565b6040518060c00160405280601660ff1681526020018873ffffffffffffffffffffffffffffffffffffffff168152602001838152602001600063ffffffff1681526020018481526020018715158152509050610f598186611f4b565b93505050509392505050565b6000610f7033611e43565b610fe2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b600073ffffffffffffffffffffffffffffffffffffffff168373ffffffffffffffffffffffffffffffffffffffff161415611085576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252601b8152602001807f636f6e747261637420616464726573206e6f74206578697374732e000000000081525060200191505060405180910390fd5b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166364efb22b846040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060206040518083038186803b15801561112457600080fd5b505afa158015611138573d6000803e3d6000fd5b505050506040513d602081101561114e57600080fd5b810190808051906020019092919050505073ffffffffffffffffffffffffffffffffffffffff168473ffffffffffffffffffffffffffffffffffffffff1614156111e3576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260348152602001806124406034913960400191505060405180910390fd5b6060600167ffffffffffffffff811180156111fd57600080fd5b5060405190808252806020026020018201604052801561122c5781602001602082028036833780820191505090505b5090506060858260008151811061123f57fe5b602002602001019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff1681525050611281612193565b6040518060c00160405280601f60ff1681526020018773ffffffffffffffffffffffffffffffffffffffff168152602001838152602001600063ffffffff1681526020018481526020016000151581525090506112de8186611f4b565b93505050509392505050565b6112f333611e43565b611365576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166320d154da82336040518363ffffffff1660e01b8152600401808381526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200192505050600060405180830381600087803b15801561140e57600080fd5b505af1158015611422573d6000803e3d6000fd5b5050505050565b61143233611e43565b6114a4576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260148152602001807f796f75206d75737420626520676f7665726e6f7200000000000000000000000081525060200191505060405180910390fd5b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663315ce5e28484336040518463ffffffff1660e01b815260040180848152602001831515151581526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019350505050602060405180830381600087803b15801561155b57600080fd5b505af115801561156f573d6000803e3d6000fd5b505050506040513d602081101561158557600080fd5b810190808051906020019092919050505090506115a0612193565b60028260ff161415611e105760006115b785611e16565b9050600360008681526020019081526020016000206040518060c00160405290816000820160009054906101000a900460ff1660ff1660ff1681526020016000820160019054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001600182018054806020026020016040519081016040528092919081815260200182805480156116bf57602002820191906000526020600020906000905b82829054906101000a900460ff1660ff16815260200190600101906020826000010492830192600103820291508084116116885790505b505050505081526020016002820160009054906101000a900463ffffffff1663ffffffff1663ffffffff1681526020016003820180548060200260200160405190810160405280929190818152602001828054801561177357602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019060010190808311611729575b505050505081526020016004820160009054906101000a900460ff1615151515815250509150600b8160ff161415611892576000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663f437695a83608001516000815181106117f357fe5b602002602001015184606001516040518363ffffffff1660e01b8152600401808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018263ffffffff1663ffffffff16815260200192505050600060405180830381600087803b15801561187557600080fd5b505af1158015611889573d6000803e3d6000fd5b50505050611e0e565b600c8160ff161415611973576000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166399bc9c1b83604001516000815181106118ec57fe5b6020026020010151846040015160018151811061190557fe5b60200260200101516040518363ffffffff1660e01b8152600401808360ff1660ff1681526020018260ff1660ff16815260200192505050600060405180830381600087803b15801561195657600080fd5b505af115801561196a573d6000803e3d6000fd5b50505050611e0d565b60158160ff161415611a5357600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663bb0aa40c83604001516000815181106119ce57fe5b60200260200101516040518263ffffffff1660e01b8152600401808260ff1660ff168152602001915050602060405180830381600087803b158015611a1257600080fd5b505af1158015611a26573d6000803e3d6000fd5b505050506040513d6020811015611a3c57600080fd5b810190808051906020019092919050505050611e0c565b60168160ff161415611c5e578160a0015115611b6357600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663615480998360800151600081518110611ab857fe5b60200260200101516040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180830381600087803b158015611b2257600080fd5b505af1158015611b36573d6000803e3d6000fd5b505050506040513d6020811015611b4c57600080fd5b810190808051906020019092919050505050611c59565b600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166356bd70848360800151600081518110611bb257fe5b60200260200101516040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001915050602060405180830381600087803b158015611c1c57600080fd5b505af1158015611c30573d6000803e3d6000fd5b505050506040513d6020811015611c4657600080fd5b8101908080519060200190929190505050505b611e0b565b601f8160ff161415611d9c57600260009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663c53057b483602001518460800151600081518110611cbe57fe5b60200260200101516040518363ffffffff1660e01b8152600401808373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200192505050602060405180830381600087803b158015611d5b57600080fd5b505af1158015611d6f573d6000803e3d6000fd5b505050506040513d6020811015611d8557600080fd5b810190808051906020019092919050505050611e0a565b6040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260108152602001807f766f74652074797065206572726f722e0000000000000000000000000000000081525060200191505060405180910390fd5b5b5b5b5b505b50505050565b60006003600083815260200190815260200160002060000160009054906101000a900460ff169050919050565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663e43581b8836040518263ffffffff1660e01b8152600401808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060206040518083038186803b158015611ee357600080fd5b505afa158015611ef7573d6000803e3d6000fd5b505050506040513d6020811015611f0d57600080fd5b81019080805190602001909291905050509050919050565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600080600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16632c3956fe3386600001518760200151876040518563ffffffff1660e01b8152600401808573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018460ff1660ff1681526020018373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001828152602001945050505050602060405180830381600087803b15801561204157600080fd5b505af1158015612055573d6000803e3d6000fd5b505050506040513d602081101561206b57600080fd5b81019080805190602001909291905050509050836003600083815260200190815260200160002060008201518160000160006101000a81548160ff021916908360ff16021790555060208201518160000160016101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060408201518160010190805190602001906121169291906121ea565b5060608201518160020160006101000a81548163ffffffff021916908363ffffffff160217905550608082015181600301908051906020019061215a929190612291565b5060a08201518160040160006101000a81548160ff021916908315150217905550905050612189816001611429565b8091505092915050565b6040518060c00160405280600060ff168152602001600073ffffffffffffffffffffffffffffffffffffffff16815260200160608152602001600063ffffffff168152602001606081526020016000151581525090565b82805482825590600052602060002090601f016020900481019282156122805791602002820160005b8382111561225157835183826101000a81548160ff021916908360ff1602179055509260200192600101602081600001049283019260010302612213565b801561227e5782816101000a81549060ff0219169055600101602081600001049283019260010302612251565b505b50905061228d919061231b565b5090565b82805482825590600052602060002090810192821561230a579160200282015b828111156123095782518260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550916020019190600101906122b1565b5b509050612317919061234b565b5090565b61234891905b8082111561234457600081816101000a81549060ff021916905550600101612321565b5090565b90565b61238b91905b8082111561238757600081816101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905550600101612351565b5090565b9056fe6163636f756e7420686173206e6f2061757468206f66206465706c6f79696e6720636f6e74726163742e7468652063757272656e74206465706c6f7920617574682074797065206973207468652073616d6520617320796f752077616e7420746f207365746163636f756e7420686173207468652061757468206f66206465706c6f79696e6720636f6e74726163742e696e76616c69642072616e6765206f662070617274696369706174657352617465746865206163636f756e7420686173206265656e207468652061646d696e206f6620636f6e637572727420636f6e74726163742ea264697066735822122006bb9aff4dbed1d5015d7313e100961e3f2eb586f884506a794dd062225a548164736f6c634300060a003360806040523480156200001157600080fd5b5060405162001dd438038062001dd4833981810160405260808110156200003757600080fd5b81019080805160405193929190846401000000008211156200005857600080fd5b838201915060208201858111156200006f57600080fd5b82518660208202830111640100000000821117156200008d57600080fd5b8083526020830192505050908051906020019060200280838360005b83811015620000c6578082015181840152602081019050620000a9565b5050505090500160405260200180516040519392919084640100000000821115620000f057600080fd5b838201915060208201858111156200010757600080fd5b82518660208202830111640100000000821117156200012557600080fd5b8083526020830192505050908051906020019060200280838360005b838110156200015e57808201518184015260208101905062000141565b505050509050016040526020018051906020019092919080519060200190929190505050336000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060008090505b84518163ffffffff1610156200022c576200021e858263ffffffff1681518110620001ef57fe5b6020026020010151858363ffffffff16815181106200020a57fe5b60200260200101516200027e60201b60201c565b8080600101915050620001c8565b5080600460016101000a81548160ff021916908360ff16021790555081600460006101000a81548160ff021916908360ff16021790555062000274336200048960201b60201c565b5050505062000a23565b6200028f336200055060201b60201c565b62000302576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b60008163ffffffff1614156200038557600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81549063ffffffff02191690556200037f826001620005fb60201b62000d7a1790919060201c565b62000485565b620003a08260016200082c60201b62000f9e1790919060201c565b156200040a5780600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548163ffffffff021916908363ffffffff16021790555062000484565b80600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548163ffffffff021916908363ffffffff160217905550620004838260016200087b60201b62000fed1790919060201c565b5b5b5050565b6200049a336200055060201b60201c565b6200050d576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b60003073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff161415620005915760019050620005f6565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff161415620005f15760019050620005f6565b600090505b919050565b6200060d82826200082c60201b60201c565b62000664576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602381526020018062001d906023913960400191505060405180910390fd5b600060018360000160008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020540390506000600184600101805490500390506000846001018281548110620006ce57fe5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff169050808560010184815481106200070c57fe5b9060005260206000200160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600183018560000160008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055508460000160008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000905584600101805480620007f057fe5b6001900381819060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905590555050505050565b6000808360000160008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020541415905092915050565b600073ffffffffffffffffffffffffffffffffffffffff168173ffffffffffffffffffffffffffffffffffffffff16141562000903576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602181526020018062001db36021913960400191505060405180910390fd5b6200091582826200082c60201b60201c565b156200096d576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602f81526020018062001d61602f913960400191505060405180910390fd5b81600101819080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555081600101805490508260000160008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055505050565b61132e8062000a336000396000f3fe608060405234801561001057600080fd5b50600436106100b45760003560e01c8063ac6c525111610071578063ac6c52511461045e578063b2bdfa7b146104c2578063b6fd90671461050c578063cd5d211814610530578063e43581b81461058c578063f437695a146105e8576100b4565b806313af4035146100b957806353bfcf2f146100fd5780635615696f146102635780635e77fe2014610287578063965b9ff11461034857806399bc9c1b14610420575b600080fd5b6100fb600480360360208110156100cf57600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff16906020019092919050505061063c565b005b6102476004803603604081101561011357600080fd5b810190808035906020019064010000000081111561013057600080fd5b82018360208201111561014257600080fd5b8035906020019184602083028401116401000000008311171561016457600080fd5b919080806020026020016040519081016040528093929190818152602001838360200280828437600081840152601f19601f820116905080830192505050505050509192919290803590602001906401000000008111156101c457600080fd5b8201836020820111156101d657600080fd5b803590602001918460208302840111640100000000831117156101f857600080fd5b919080806020026020016040519081016040528093929190818152602001838360200280828437600081840152601f19601f8201169050808301925050505050505091929192905050506106fa565b604051808260ff1660ff16815260200191505060405180910390f35b61026b6107a8565b604051808260ff1660ff16815260200191505060405180910390f35b61028f6107bb565b604051808560ff1660ff1681526020018460ff1660ff1681526020018060200180602001838103835285818151815260200191508051906020019060200280838360005b838110156102ee5780820151818401526020810190506102d3565b50505050905001838103825284818151815260200191508051906020019060200280838360005b83811015610330578082015181840152602081019050610315565b50505050905001965050505050505060405180910390f35b6103fe6004803603602081101561035e57600080fd5b810190808035906020019064010000000081111561037b57600080fd5b82018360208201111561038d57600080fd5b803590602001918460208302840111640100000000831117156103af57600080fd5b919080806020026020016040519081016040528093929190818152602001838360200280828437600081840152601f19601f8201169050808301925050505050505091929192905050506108e7565b604051808263ffffffff1663ffffffff16815260200191505060405180910390f35b61045c6004803603604081101561043657600080fd5b81019080803560ff169060200190929190803560ff169060200190929190505050610987565b005b6104a06004803603602081101561047457600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610a3c565b604051808263ffffffff1663ffffffff16815260200191505060405180910390f35b6104ca610a95565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b610514610aba565b604051808260ff1660ff16815260200191505060405180910390f35b6105726004803603602081101561054657600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610acd565b604051808215151515815260200191505060405180910390f35b6105ce600480360360208110156105a257600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610b74565b604051808215151515815260200191505060405180910390f35b61063a600480360360408110156105fe57600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803563ffffffff169060200190929190505050610b91565b005b61064533610acd565b6106b7576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b600080610706846108e7565b90506000610713846108e7565b82019050600061072b6107266001611189565b6108e7565b9050600460009054906101000a900460ff1660ff16810263ffffffff166064830263ffffffff16101561076457600193505050506107a2565b81600460019054906101000a900460ff1660ff160263ffffffff166064840263ffffffff161061079a57600293505050506107a2565b600393505050505b92915050565b600460009054906101000a900460ff1681565b6000806060806107cb6001611189565b9150815167ffffffffffffffff811180156107e557600080fd5b506040519080825280602002602001820160405280156108145781602001602082028036833780820191505090505b50905060008090505b82518110156108bc576003600084838151811061083657fe5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900463ffffffff1682828151811061089557fe5b602002602001019063ffffffff16908163ffffffff1681525050808060010191505061081d565b50600460019054906101000a900460ff169250600460009054906101000a900460ff16935090919293565b6000806000905060008090505b83518163ffffffff16101561097d5760036000858363ffffffff168151811061091957fe5b602002602001015173ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900463ffffffff168201915080806001019150506108f4565b5080915050919050565b61099033610acd565b610a02576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b80600460016101000a81548160ff021916908360ff16021790555081600460006101000a81548160ff021916908360ff1602179055505050565b6000600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060009054906101000a900463ffffffff169050919050565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600460019054906101000a900460ff1681565b60003073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff161415610b0c5760019050610b6f565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff161415610b6a5760019050610b6f565b600090505b919050565b6000610b8a826001610f9e90919063ffffffff16565b9050919050565b610b9a33610acd565b610c0c576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b60008163ffffffff161415610c8657600360008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81549063ffffffff0219169055610c81826001610d7a90919063ffffffff16565b610d76565b610c9a826001610f9e90919063ffffffff16565b15610d025780600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548163ffffffff021916908363ffffffff160217905550610d75565b80600360008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060006101000a81548163ffffffff021916908363ffffffff160217905550610d74826001610fed90919063ffffffff16565b5b5b5050565b610d848282610f9e565b610dd9576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260238152602001806112b56023913960400191505060405180910390fd5b600060018360000160008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020540390506000600184600101805490500390506000846001018281548110610e4257fe5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905080856001018481548110610e7f57fe5b9060005260206000200160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550600183018560000160008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055508460000160008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019081526020016000206000905584600101805480610f6257fe5b6001900381819060005260206000200160006101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905590555050505050565b6000808360000160008473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020541415905092915050565b600073ffffffffffffffffffffffffffffffffffffffff168173ffffffffffffffffffffffffffffffffffffffff161415611073576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260218152602001806112d86021913960400191505060405180910390fd5b61107d8282610f9e565b156110d3576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602f815260200180611286602f913960400191505060405180910390fd5b81600101819080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555081600101805490508260000160008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055505050565b606080826001018054905067ffffffffffffffff811180156111aa57600080fd5b506040519080825280602002602001820160405280156111d95781602001602082028036833780820191505090505b50905060005b836001018054905081101561127b578360010181815481106111fd57fe5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1682828151811061123457fe5b602002602001019073ffffffffffffffffffffffffffffffffffffffff16908173ffffffffffffffffffffffffffffffffffffffff168152505080806001019150506111df565b508091505091905056fe4c6962416464726573735365743a2076616c756520616c72656164792065786973747320696e20746865207365742e4c6962416464726573735365743a2076616c756520646f65736e27742065786973742e4c6962416464726573735365743a2076616c75652063616e277420626520307830a264697066735822122023e363eb9589fe94215444e41c5ae8122d8e550ecf129abf594300ff17e9a8c964736f6c634300060a00334c6962416464726573735365743a2076616c756520616c72656164792065786973747320696e20746865207365742e4c6962416464726573735365743a2076616c756520646f65736e27742065786973742e4c6962416464726573735365743a2076616c75652063616e27742062652030783060806040523480156200001157600080fd5b5060405162001dc138038062001dc1833981810160405260408110156200003757600080fd5b810190808051906020019092919080519060200190929190505050336000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550620000a382620000ec60201b60201c565b80600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050506200025e565b620000fd33620001b360201b60201c565b62000170576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b60003073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff161415620001f4576001905062000259565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff16141562000254576001905062000259565b600090505b919050565b611b53806200026e6000396000f3fe608060405234801561001057600080fd5b50600436106100ea5760003560e01c8063315ce5e21161008c5780636f2904cc116100665780636f2904cc146104ea578063b2bdfa7b14610508578063bc903cb814610552578063cd5d2118146106a4576100ea565b8063315ce5e2146103c9578063401853b71461043d5780636d23cd5814610485576100ea565b806319dcd07e116100c857806319dcd07e1461023f5780631cc05cbc1461028757806320d154da146102ec5780632c3956fe1461033a576100ea565b80630a494840146100ef57806313af4035146101b1578063185c1587146101f5575b600080fd5b61011b6004803603602081101561010557600080fd5b8101908080359060200190929190505050610700565b604051808673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018460ff1660ff1681526020018381526020018260ff1660ff1681526020019550505050505060405180910390f35b6101f3600480360360208110156101c757600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610790565b005b6101fd61084e565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b61026b6004803603602081101561025557600080fd5b8101908080359060200190929190505050610874565b604051808260ff1660ff16815260200191505060405180910390f35b6102d66004803603604081101561029d57600080fd5b81019080803560ff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610999565b6040518082815260200191505060405180910390f35b6103386004803603604081101561030257600080fd5b8101908080359060200190929190803573ffffffffffffffffffffffffffffffffffffffff1690602001909291905050506109be565b005b6103b36004803603608081101561035057600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190803560ff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff16906020019092919080359060200190929190505050610b9c565b6040518082815260200191505060405180910390f35b610421600480360360608110156103df57600080fd5b8101908080359060200190929190803515159060200190929190803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050610f6f565b604051808260ff1660ff16815260200191505060405180910390f35b6104696004803603602081101561045357600080fd5b81019080803590602001909291905050506115dd565b604051808260ff1660ff16815260200191505060405180910390f35b6104d46004803603604081101561049b57600080fd5b81019080803560ff169060200190929190803573ffffffffffffffffffffffffffffffffffffffff16906020019092919050505061160a565b6040518082815260200191505060405180910390f35b6104f261166b565b6040518082815260200191505060405180910390f35b610510611671565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b61057e6004803603602081101561056857600080fd5b8101908080359060200190929190505050611696565b604051808873ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018773ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020018660ff1660ff1681526020018581526020018460ff1660ff1681526020018060200180602001838103835285818151815260200191508051906020019060200280838360005b8381101561064757808201518184015260208101905061062c565b50505050905001838103825284818151815260200191508051906020019060200280838360005b8381101561068957808201518184015260208101905061066e565b50505050905001995050505050505050505060405180910390f35b6106e6600480360360208110156106ba57600080fd5b81019080803573ffffffffffffffffffffffffffffffffffffffff169060200190929190505050611857565b604051808215151515815260200191505060405180910390f35b60036020528060005260406000206000915090508060000160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16908060010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16908060010160149054906101000a900460ff16908060020154908060030160009054906101000a900460ff16905085565b61079933611857565b61080b576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b806000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008160006003600083815260200190815260200160002060030160009054906101000a900460ff1660ff161415610914576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260128152602001807f50726f706f73616c206e6f74206578697374000000000000000000000000000081525060200191505060405180910390fd5b600060036000858152602001908152602001600020905060018160030160009054906101000a900460ff1660ff16141561097d57806002015443111561097c5760058160030160006101000a81548160ff021916908360ff1602179055506005925050610993565b5b8060030160009054906101000a900460ff169250505b50919050565b6004602052816000526040600020602052806000526040600020600091509150505481565b6109c733611857565b610a39576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b60006003600084815260200190815260200160002090506001610a5b84610874565b60ff1614610ab4576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252602a815260200180611af4602a913960400191505060405180910390fd5b8173ffffffffffffffffffffffffffffffffffffffff168160010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614610b79576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260188152602001807f4f6e6c792070726f706f7365722063616e207265766f6b65000000000000000081525060200191505060405180910390fd5b60048160030160006101000a81548160ff021916908360ff160217905550505050565b6000610ba733611857565b610c19576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b6000600460008660ff1660ff16815260200190815260200160002060008573ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002054905060016003600083815260200190815260200160002060030160009054906101000a900460ff1660ff161415610cae57610cac81610874565b505b60016003600083815260200190815260200160002060030160009054906101000a900460ff1660ff161415610d4b576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260188152602001807f43757272656e742070726f706f73616c206e6f7420656e64000000000000000081525060200191505060405180910390fd5b60026000815480929190600101919050555060006002549050606080610d6f6119b7565b6040518060e001604052808973ffffffffffffffffffffffffffffffffffffffff1681526020018b73ffffffffffffffffffffffffffffffffffffffff1681526020018a60ff1681526020018843018152602001600160ff168152602001848152602001838152509050806003600086815260200190815260200160002060008201518160000160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060208201518160010160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060408201518160010160146101000a81548160ff021916908360ff1602179055506060820151816002015560808201518160030160006101000a81548160ff021916908360ff16021790555060a0820151816004019080519060200190610ee3929190611a26565b5060c0820151816005019080519060200190610f00929190611a26565b5090505083600460008b60ff1660ff16815260200190815260200160002060008a73ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055508395505050505050949350505050565b6000610f7a33611857565b610fec576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600b8152602001807f4f6e6c79206f776e65722100000000000000000000000000000000000000000081525060200191505060405180910390fd5b8360006003600083815260200190815260200160002060030160009054906101000a900460ff1660ff16141561108a576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260128152602001807f50726f706f73616c206e6f74206578697374000000000000000000000000000081525060200191505060405180910390fd5b60006003600087815260200190815260200160002090506112c2816040518060e00160405290816000820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020016001820160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020016001820160149054906101000a900460ff1660ff1660ff168152602001600282015481526020016003820160009054906101000a900460ff1660ff1660ff1681526020016004820180548060200260200160405190810160405280929190818152602001828054801561122557602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190600101908083116111db575b50505050508152602001600582018054806020026020016040519081016040528092919081815260200182805480156112b357602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019060010190808311611269575b505050505081525050856118fe565b15611335576040517f08c379a000000000000000000000000000000000000000000000000000000000815260040180806020018281038252600d8152602001807f416c726561647920766f7465640000000000000000000000000000000000000081525060200191505060405180910390fd5b84156113a55780600401849080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061140b565b80600501849080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166353bfcf2f83600401846005016040518363ffffffff1660e01b815260040180806020018060200183810383528581815481526020019150805480156114e157602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1681526020019060010190808311611497575b5050838103825284818154815260200191508054801561155657602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001906001019080831161150c575b505094505050505060206040518083038186803b15801561157657600080fd5b505afa15801561158a573d6000803e3d6000fd5b505050506040513d60208110156115a057600080fd5b81019080805190602001909291905050509050808260030160006101000a81548160ff021916908360ff1602179055508093505050509392505050565b60006003600083815260200190815260200160002060030160009054906101000a900460ff169050919050565b6000600460008460ff1660ff16815260200190815260200160002060008373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002054905092915050565b60025481565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60008060008060006060806000600360008a815260200190815260200160002090508060000160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1697508060010160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1696508060010160149054906101000a900460ff169550806002015494508060030160009054906101000a900460ff169350806004018054806020026020016040519081016040528092919081815260200182805480156117b957602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001906001019080831161176f575b505050505092508060050180548060200260200160405190810160405280929190818152602001828054801561184457602002820191906000526020600020905b8160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190600101908083116117fa575b5050505050915050919395979092949650565b60003073ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff16141561189657600190506118f9565b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffffffffffffffffffffffffffff1614156118f457600190506118f9565b600090505b919050565b600061190e8360a001518361193c565b8061192357506119228360c001518361193c565b5b156119315760019050611936565b600090505b92915050565b600080600090505b83518110156119ab5783818151811061195957fe5b602002602001015173ffffffffffffffffffffffffffffffffffffffff168373ffffffffffffffffffffffffffffffffffffffff16141561199e5760019150506119b1565b8080600101915050611944565b50600090505b92915050565b6040518060e00160405280600073ffffffffffffffffffffffffffffffffffffffff168152602001600073ffffffffffffffffffffffffffffffffffffffff168152602001600060ff16815260200160008152602001600060ff16815260200160608152602001606081525090565b828054828255906000526020600020908101928215611a9f579160200282015b82811115611a9e5782518260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555091602001919060010190611a46565b5b509050611aac9190611ab0565b5090565b611af091905b80821115611aec57600081816101000a81549073ffffffffffffffffffffffffffffffffffffffff021916905550600101611ab6565b5090565b9056fe4f6e6c79206e65776c7920637265617465642070726f706f73616c2063616e206265207265766f6b6564a264697066735822122012233361adcbb144d1e6d14ae14cc7bff05fce7ca8d8885160e1830509c1adc164736f6c634300060a0033";
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

        BOOST_CHECK_EQUAL(result10->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result10->contextID(), 99);
        BOOST_CHECK_EQUAL(result10->seq(), 1004);
        BOOST_CHECK_EQUAL(result10->create(), false);
        BOOST_CHECK_EQUAL(result10->origin(), sender);
        BOOST_CHECK_EQUAL(
            result10->newEVMContractAddress(), "2222222222222222222222222222222222222222");
        BOOST_CHECK_EQUAL(result10->to(), precompiled::AUTH_COMMITTEE_ADDRESS);


        result10->setSeq(1000);
        // new proposalManager address => committeeManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise11;
        executor->executeTransaction(
            std::move(result10), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise11.set_value(std::move(result));
            });

        auto result11 = executePromise11.get_future().get();
        BOOST_CHECK_EQUAL(result11->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result11->contextID(), 99);
        BOOST_CHECK_EQUAL(result11->seq(), 1000);
        BOOST_CHECK_EQUAL(result11->create(), false);
        BOOST_CHECK_EQUAL(result11->origin(), sender);
        BOOST_CHECK_EQUAL(result11->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

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
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test