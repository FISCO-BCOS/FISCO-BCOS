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

#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/extension/AuthManagerPrecompiled.h"
#include "precompiled/extension/ContractAuthMgrPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <libinitializer/AuthInitializer.h>

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
        newHello2Address = Address("0x9999999999999999999999999999999999999999").hex();
    }

    ~PermissionPrecompiledFixture() override {}

    void deployHello()
    {
        bytes input;
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), helloAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), helloAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        commitBlock(2);
    }
    void deployNewHello2()
    {
        bytes input;
        boost::algorithm::unhex(newHello2, std::back_inserter(input));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        params->setTo(newHello2Address);
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

        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), newHello2Address);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), newHello2Address);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);

        commitBlock(2);
    }

    ExecutionMessage::UniquePtr deployHelloInAuthCheck(std::string newAddress,
        bcos::protocol::BlockNumber _number, Address _address = Address(), bool _noAuth = false)
    {
        bytes input;
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

        if (_noAuth)
        {
            BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
            BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        }
        else
        {
            BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
            BOOST_CHECK_EQUAL(result->newEVMContractAddress(), newAddress);
            BOOST_CHECK_LT(result->gasAvailable(), gas);
        }
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), newAddress);
        BOOST_CHECK(result->to() == sender);

        commitBlock(_number);

        return result;
    }

    ExecutionMessage::UniquePtr deployHello2InAuthCheck(
        std::string newAddress, bcos::protocol::BlockNumber _number, Address _address = Address())
    {
        bytes input;
        boost::algorithm::unhex(h2, std::back_inserter(input));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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
        executor->dmcExecuteTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        /// call Auth manager to check deploy auth
        result->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        /// callback to create context
        result2->setSeq(1000);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(
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
        executor->dmcExecuteTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });

        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1003);
        /// call to check deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });

        auto result5 = executePromise5.get_future().get();

        result5->setSeq(1002);
        /// callback to deploy hello2 context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(
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
        int _errorCode = 0, Address _address = Address(), std::string to = "")
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("get()");
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        if (to != "")
        {
            params2->setTo(to);
        }
        else
        {
            params2->setTo(helloAddress);
        }
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

    ExecutionMessage::UniquePtr getHelloInside(protocol::BlockNumber _number, int _contextId)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("getHello()");

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setTo(newHello2Address);
        params2->setOrigin(sender);
        params2->setStaticCall(true);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::MESSAGE);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->call(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr helloSet(protocol::BlockNumber _number, int _contextId,
        const std::string& _value, int _errorCode = 0, Address _address = Address(),
        std::string to = "")
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("set(string)", _value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        if (to != "")
        {
            params2->setTo(to);
        }
        else
        {
            params2->setTo(helloAddress);
        }
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr modifyMethodAuth(protocol::BlockNumber _number, int _contextId,
        std::string const& authMethod, Address const& _path, std::string const& helloMethod,
        Address const& _account, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig(authMethod, _path, func, _account);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        // call precompiled
        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);

        return result;
    };

    ExecutionMessage::UniquePtr getMethodAuth(protocol::BlockNumber _number, Address const& _path,
        std::string const& helloMethod, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig("getMethodAuth(address,bytes4)", _path, func);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result1;
    };

    ExecutionMessage::UniquePtr checkMethodAuth(protocol::BlockNumber _number, Address const& _path,
        std::string const& helloMethod, Address const& _account, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes fun = codec->encodeWithSig(helloMethod);
        auto func = toString32(h256(fun, FixedBytes<32>::AlignLeft));
        bytes in = codec->encodeWithSig("checkMethodAuth(address,bytes4,address)", _path, func);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result1;
    };

    ExecutionMessage::UniquePtr resetAdmin(protocol::BlockNumber _number, int _contextId,
        Address const& _path, Address const& _newAdmin, int _errorCode = 0,
        bool _useWrongSender = false)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("resetAdmin(address,address)", _path, _newAdmin);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address("0000000000000000000000000000000000010001");
        auto wrongSender = Address("0000000000000000000000000000000000011111");
        tx->forceSender(_useWrongSender ? wrongSender.asBytes() : newSender.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_contextId);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_useWrongSender)
        {
            commitBlock(_number);
            return result1;
        }

        if (_errorCode != 0)
        {
            BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result1;
    };

    ExecutionMessage::UniquePtr setContractStatus(
        protocol::BlockNumber _number, Address const& _path, bool _isFreeze, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("setContractStatus(address,bool)", _path, _isFreeze);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr setContractStatus32(protocol::BlockNumber _number,
        Address const& _path, ContractStatus status, int _errorCode = 0)
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig(
            "setContractStatus(address,uint8)", _path, static_cast<uint8_t>(status));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr contractAvailable(
        protocol::BlockNumber _number, Address const& _path, int _errorCode = 0)
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig("contractAvailable(address)", _path);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr getAdmin(
        protocol::BlockNumber _number, int _contextId, Address const& _path, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("getAdmin(address)", _path);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params1->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

        if (_errorCode != 0)
        {
            BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result1;
    };

    ExecutionMessage::UniquePtr setDeployType(
        protocol::BlockNumber _number, int _contextId, AuthType _authType, int _errorCode = 0)
    {
        nextBlock(_number);
        uint8_t type = (_authType == AuthType::WHITE_LIST_MODE) ? 1 : 2;
        auto t = toString32(h256(type));
        bytes in = codec->encodeWithSig("setDeployAuthType(uint8)", t);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
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
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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

    ExecutionMessage::UniquePtr initAuth(
        protocol::BlockNumber _number, std::string const& _address, int _errorCode = 0)
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig("initAuth(string)", _address);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(std::string(precompiled::AUTH_MANAGER_ADDRESS));
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
    std::string newHello2Address;

    // clang-format off
    std::string helloBin =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4dba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
    std::string h2 =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c9291906100cb565b506040516100699061014b565b604051809103906000f080158015610085573d6000803e3d6000fd5b50600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550610175565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061010c57805160ff191683800117855561013a565b8280016001018555821561013a579182015b8281111561013957825182559160200191906001019061011e565b5b5090506101479190610158565b5090565b6104168061060f83390190565b5b80821115610171576000816000905550600101610159565b5090565b61048b806101846000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c8063a7ef43291461003b578063d2178b081461006f575b600080fd5b61004361015e565b604051808273ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b610077610188565b604051808060200180602001838103835285818151815260200191508051906020019080838360005b838110156100bb5780820151818401526020810190506100a0565b50505050905090810190601f1680156100e85780820380516001836020036101000a031916815260200191505b50838103825284818151815260200191508051906020019080838360005b83811015610121578082015181840152602081019050610106565b50505050905090810190601f16801561014e5780820380516001836020036101000a031916815260200191505b5094505050505060405180910390f35b6000600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905090565b606080600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff1660e01b815260040160006040518083038186803b1580156101f357600080fd5b505afa158015610207573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f82011682018060405250602081101561023157600080fd5b810190808051604051939291908464010000000082111561025157600080fd5b8382019150602082018581111561026757600080fd5b825186600182028301116401000000008211171561028457600080fd5b8083526020830192505050908051906020019080838360005b838110156102b857808201518184015260208101905061029d565b50505050905090810190601f1680156102e55780820380516001836020036101000a031916815260200191505b50604052505050600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff1660e01b815260040160006040518083038186803b15801561035457600080fd5b505afa158015610368573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f82011682018060405250602081101561039257600080fd5b81019080805160405193929190846401000000008211156103b257600080fd5b838201915060208201858111156103c857600080fd5b82518660018202830111640100000000821117156103e557600080fd5b8083526020830192505050908051906020019080838360005b838110156104195780820151818401526020810190506103fe565b50505050905090810190601f1680156104465780820380516001836020036101000a031916815260200191505b5060405250505091509150909156fea2646970667358221220004a7724bbd7d9ee1d4c00b949283850a69ddc538be408fa1c79fce561994b0a64736f6c634300060c0033608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b506100ff565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b5b808211156100fb5760008160009055506001016100e3565b5090565b6103088061010e6000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b5b808211156102ce5760008160009055506001016102b6565b509056fea26469706673582212200374a2bdb89cd0187ff6b1f551739ca53a44f456bd10ea4052e069475292e45b64736f6c634300060c0033";
    std::string_view newHello2 =
        "608060405234801561001057600080fd5b5060405161001d9061007e565b604051809103906000f080158015610039573d6000803e3d6000fd5b506000806101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555061008b565b61061d8061062283390190565b6105888061009a6000396000f3fe608060405234801561001057600080fd5b50600436106100415760003560e01c80634ed3885e146100465780636d4ce63c146100625780638da9b77214610080575b600080fd5b610060600480360381019061005b919061034a565b61009e565b005b61006a61012c565b604051610077919061041b565b60405180910390f35b6100886101c7565b604051610095919061047e565b60405180910390f35b60008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16634ed3885e826040518263ffffffff1660e01b81526004016100f7919061041b565b600060405180830381600087803b15801561011157600080fd5b505af1158015610125573d6000803e3d6000fd5b5050505050565b606060008054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff1660e01b8152600401600060405180830381865afa158015610199573d6000803e3d6000fd5b505050506040513d6000823e3d601f19601f820116820180604052508101906101c29190610509565b905090565b60008060009054906101000a900473ffffffffffffffffffffffffffffffffffffffff16905090565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6102578261020e565b810181811067ffffffffffffffff821117156102765761027561021f565b5b80604052505050565b60006102896101f0565b9050610295828261024e565b919050565b600067ffffffffffffffff8211156102b5576102b461021f565b5b6102be8261020e565b9050602081019050919050565b82818337600083830152505050565b60006102ed6102e88461029a565b61027f565b90508281526020810184848401111561030957610308610209565b5b6103148482856102cb565b509392505050565b600082601f83011261033157610330610204565b5b81356103418482602086016102da565b91505092915050565b6000602082840312156103605761035f6101fa565b5b600082013567ffffffffffffffff81111561037e5761037d6101ff565b5b61038a8482850161031c565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b838110156103cd5780820151818401526020810190506103b2565b838111156103dc576000848401525b50505050565b60006103ed82610393565b6103f7818561039e565b93506104078185602086016103af565b6104108161020e565b840191505092915050565b6000602082019050818103600083015261043581846103e2565b905092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006104688261043d565b9050919050565b6104788161045d565b82525050565b6000602082019050610493600083018461046f565b92915050565b60006104ac6104a78461029a565b61027f565b9050828152602081018484840111156104c8576104c7610209565b5b6104d38482856103af565b509392505050565b600082601f8301126104f0576104ef610204565b5b8151610500848260208601610499565b91505092915050565b60006020828403121561051f5761051e6101fa565b5b600082015167ffffffffffffffff81111561053d5761053c6101ff565b5b610549848285016104db565b9150509291505056fea26469706673582212204f50db6889de82b87ccbd36ad33556e98fc49b306d739e10444ec8677311201564736f6c634300080b003360806040526040518060400160405280600c81526020017f48656c6c6f20576f726c642100000000000000000000000000000000000000008152506000908051906020019061004f929190610062565b5034801561005c57600080fd5b50610166565b82805461006e90610134565b90600052602060002090601f01602090048101928261009057600085556100d7565b82601f106100a957805160ff19168380011785556100d7565b828001600101855582156100d7579182015b828111156100d65782518255916020019190600101906100bb565b5b5090506100e491906100e8565b5090565b5b808211156101015760008160009055506001016100e9565b5090565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061014c57607f821691505b602082108114156101605761015f610105565b5b50919050565b6104a8806101756000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c14610057575b600080fd5b6100556004803603810190610050919061031e565b610075565b005b61005f61008f565b60405161006c91906103ef565b60405180910390f35b806000908051906020019061008b929190610121565b5050565b60606000805461009e90610440565b80601f01602080910402602001604051908101604052809291908181526020018280546100ca90610440565b80156101175780601f106100ec57610100808354040283529160200191610117565b820191906000526020600020905b8154815290600101906020018083116100fa57829003601f168201915b5050505050905090565b82805461012d90610440565b90600052602060002090601f01602090048101928261014f5760008555610196565b82601f1061016857805160ff1916838001178555610196565b82800160010185558215610196579182015b8281111561019557825182559160200191906001019061017a565b5b5090506101a391906101a7565b5090565b5b808211156101c05760008160009055506001016101a8565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b61022b826101e2565b810181811067ffffffffffffffff8211171561024a576102496101f3565b5b80604052505050565b600061025d6101c4565b90506102698282610222565b919050565b600067ffffffffffffffff821115610289576102886101f3565b5b610292826101e2565b9050602081019050919050565b82818337600083830152505050565b60006102c16102bc8461026e565b610253565b9050828152602081018484840111156102dd576102dc6101dd565b5b6102e884828561029f565b509392505050565b600082601f830112610305576103046101d8565b5b81356103158482602086016102ae565b91505092915050565b600060208284031215610334576103336101ce565b5b600082013567ffffffffffffffff811115610352576103516101d3565b5b61035e848285016102f0565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b838110156103a1578082015181840152602081019050610386565b838111156103b0576000848401525b50505050565b60006103c182610367565b6103cb8185610372565b93506103db818560208601610383565b6103e4816101e2565b840191505092915050565b6000602082019050818103600083015261040981846103b6565b905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061045857607f821691505b6020821081141561046c5761046b610411565b5b5091905056fea2646970667358221220d2b919fa4d32bf35a96d79eb60a77920a2e30aebf4bae63983d1015ab15755c564736f6c634300080b0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(precompiledPermissionTest, PermissionPrecompiledFixture)

BOOST_AUTO_TEST_CASE(testMethodWhiteList)
{
    deployHello();
    bcos::protocol::BlockNumber number = 2;
    // simple get
    {
        auto result = helloGet(number++, 1000);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello, World!")));
    }

    // add method acl type
    {
        bcos::protocol::BlockNumber _number = 3;

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

            auto result2 = checkMethodAuth(number++, Address(helloAddress), "get()",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result2->data().toBytes() == codec->encode(bool(false)));

            auto result3 = checkMethodAuth(number++, Address(helloAddress), "set(string)",
                Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result3->data().toBytes() == codec->encode(bool(true)));
        }

        // use address 0x1234567890123456789012345678901234567890 still can set
        {
            auto result = helloSet(
                _number++, 1000, "test2", 0, Address("0x1234567890123456789012345678901234567890"));
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::None);
        }
    }
}

BOOST_AUTO_TEST_CASE(testMethodWhiteListWithSenderCheck)
{
    deployNewHello2();
    bcos::protocol::BlockNumber number = 2;
    // simple get
    {
        auto result = helloGet(number++, 1000, 0, Address(), newHello2Address);
        BOOST_CHECK(result->data().toBytes() == codec->encode(std::string("Hello World!")));
    }

    Address helloInside;
    {
        auto result = getHelloInside(number++, 1000);
        codec->decode(result->data(), helloInside);
    }

    // add method acl type
    {
        bcos::protocol::BlockNumber _number = 4;

        // not found
        auto r1 = getMethodAuth(number++, helloInside, "set(string)");
        BOOST_CHECK(
            r1->data().toBytes() == codec->encode((uint8_t)(0), std::vector<std::string>({}),
                                        std::vector<std::string>({})));
        // set method acl type
        {
            auto result = setMethodType(
                _number++, 1000, helloInside, "set(string)", AuthType::WHITE_LIST_MODE);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(0)));

            // row not found
            auto result2 = getMethodAuth(number++, helloInside, "set(string)");
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(1),
                                                         std::vector<std::string>({}),
                                                         std::vector<std::string>({})));
        }

        // can't set now, white list doesnt have any account
        {
            auto result = helloSet(_number++, 1000, "123", 0, Address(), helloInside.hex());
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // can't set by new hello2 now, white list doesnt have any account
        {
            auto result = helloSet(_number++, 1000, "123", 0, Address(), newHello2Address);
            BOOST_CHECK(result->status() == (int32_t)TransactionStatus::RevertInstruction);
            BOOST_CHECK(result->type() == ExecutionMessage::REVERT);
        }

        // open white list, only newHelloAddress address can use
        {
            auto result4 =
                modifyMethodAuth(_number++, 1000, "openMethodAuth(address,bytes4,address)",
                    helloInside, "set(string)", Address(newHello2Address));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(number++, helloInside, "set(string)");
            BOOST_CHECK(
                result2->data().toBytes() == codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE),
                                                 std::vector<std::string>({newHello2Address}),
                                                 std::vector<std::string>({})));
        }

        // use random tx.origin set permission denied
        {
            auto result5 = helloSet(_number++, 1000, "123", 0, Address(), helloInside.hex());
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }

        // use hello2 contract to set, success set
        {
            auto result7 = helloSet(_number++, 1000, "123", 0, Address(), newHello2Address);
            BOOST_CHECK_EQUAL(result7->status(), 0);
        }

        // close white list, hello2 address can not use
        {
            auto result4 =
                modifyMethodAuth(_number++, 1000, "closeMethodAuth(address,bytes4,address)",
                    helloInside, "set(string)", Address(newHello2Address));
            BOOST_CHECK(result4->data().toBytes() == codec->encode(u256(0)));

            auto result2 = getMethodAuth(number++, helloInside, "set(string)");
            BOOST_CHECK(
                result2->data().toBytes() == codec->encode((uint8_t)(AuthType::WHITE_LIST_MODE),
                                                 std::vector<std::string>({}),
                                                 std::vector<std::string>({newHello2Address})));
        }

        // call hello inside get permission denied
        {
            auto result5 = helloSet(_number++, 1000, "123", 0, Address(), helloInside.hex());
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::PermissionDenied);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
        }
        // call hello outside get permission denied
        {
            auto result5 = helloSet(_number++, 1000, "123", 0, Address(), newHello2Address);
            BOOST_CHECK(result5->status() == (int32_t)TransactionStatus::RevertInstruction);
            BOOST_CHECK(result5->type() == ExecutionMessage::REVERT);
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
        bcos::protocol::BlockNumber _number = 4;
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
            BOOST_CHECK(result2->data().toBytes() == codec->encode((uint8_t)(2),
                                                         std::vector<std::string>({}),
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
        bcos::protocol::BlockNumber _number = 3;
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
            if (versionCompareTo(m_blockVersion, BlockVersion::V3_3_VERSION) >= 0)
            {
                BOOST_CHECK(
                    result->data().toBytes() == codec->encode(Address(std::string(EMPTY_ADDRESS))));
            }
            else
            {
                BOOST_CHECK(result->status() == (int32_t)TransactionStatus::PrecompiledError);
            }
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

        // reset admin with wrong sender
        {
            auto result = resetAdmin(_number++, 1000, Address(helloAddress),
                Address("0x1234567890123456789012345678901234567890"), 0, true);
            BOOST_CHECK(result->data().toBytes() == codec->encode(u256(CODE_NO_AUTHORIZED)));
        }

        // get admin
        {
            auto result = getAdmin(_number++, 1000, Address(helloAddress));
            BOOST_CHECK(result->data().toBytes() ==
                        codec->encode(Address("0x1234567890123456789012345678901234567890")));
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
        bcos::protocol::BlockNumber _number = 3;
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
        bcos::protocol::BlockNumber _number = 3;
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
    // call CommitteeManager
    {
        nextBlock(2);
        bytes in = codec->encodeWithSig("createUpdateGovernorProposal(address,uint32,uint256)",
            admin, codec::toString32(h256(uint32_t(2))), u256(2));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");

        Address _admin(admin);
        tx->forceSender(_admin.asBytes());

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
        params2->setTo(std::string(precompiled::AUTH_COMMITTEE_ADDRESS));
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

        commitBlock(2);
    }
}

BOOST_AUTO_TEST_CASE(testDeployAdmin)
{
    bcos::protocol::BlockNumber _number = 3;
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
    bcos::protocol::BlockNumber _number = 3;
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
    bcos::protocol::BlockNumber _number = 3;
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

BOOST_AUTO_TEST_CASE(testContractAbolish)
{
    setIsWasm(false, true, true, BlockVersion::V3_2_VERSION);

    deployHello();
    bcos::protocol::BlockNumber _number = 3;
    // frozen
    {
        auto r1 = setContractStatus32(_number++, Address(helloAddress), ContractStatus::Frozen);
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
        auto r1 = setContractStatus32(_number++, Address(helloAddress), ContractStatus::Available);
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

    // abolish
    {
        auto r1 = setContractStatus32(_number++, Address(helloAddress), ContractStatus::Abolish);
        BOOST_CHECK(r1->data().toBytes() == codec->encode(int32_t(0)));

        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(false));
    }
    // abolish, revert
    {
        auto result = helloGet(_number++, 1000);
        BOOST_CHECK(result->status() == (int32_t)TransactionStatus::ContractAbolished);

        auto result2 = helloSet(_number++, 1000, "");
        BOOST_CHECK(result2->status() == (int32_t)TransactionStatus::ContractAbolished);
    }

    // frozen again, return error
    {
        auto r1 = setContractStatus32(_number++, Address(helloAddress), ContractStatus::Frozen);
        BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

        auto r2 = contractAvailable(_number++, Address(helloAddress));
        BOOST_CHECK(r2->data().toBytes() == codec->encode(false));

        auto r3 = setContractStatus(_number++, Address(helloAddress), true);
        BOOST_CHECK(r3->status() == (int32_t)TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(testInitAuth)
{
    // 3.2 version, not set auth
    setIsWasm(false, false, true, BlockVersion::V3_2_VERSION);
    deployHello();
    bcos::protocol::BlockNumber _number = 3;

    // init in 3.2, not exist address
    auto re = initAuth(_number++, admin);
    BOOST_CHECK(re->status() == (int32_t)TransactionStatus::CallAddressError);

    helloSet(_number++, 3, "test");
    helloGet(_number++, 3);

    // update to 3.3
    m_blockVersion = BlockVersion::V3_3_VERSION;

    // init auth contract
    boost::log::core::get()->set_logging_enabled(false);
    initAuth(_number++, admin);
    boost::log::core::get()->set_logging_enabled(true);

    // call CommitteeManager
    {
        _number++;
        nextBlock(_number);
        bytes in = codec->encodeWithSig("createUpdateGovernorProposal(address,uint32,uint256)",
            admin, codec::toString32(h256(uint32_t(2))), u256(2));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");

        Address _admin(admin);
        tx->forceSender(_admin.asBytes());

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
        params2->setTo(std::string(precompiled::AUTH_COMMITTEE_ADDRESS));
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

        BOOST_CHECK(result2->status() == 0);
        commitBlock(_number);
    }

    // init again, it should throw
    auto re2 = initAuth(_number++, admin);
    BOOST_CHECK(re2->status() == (int32_t)TransactionStatus::PrecompiledError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test