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
 * @file BalancePrecompiledTest.cpp
 * @author: wenlinli
 * @date 2023-11-07
 */

#include <bcos-executor/src/precompiled/extension/BalancePrecompiled.h>
#include <bcos-executor/test/unittest/libprecompiled/PreCompiledFixture.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Features.h>
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
class BalancePrecompiledFixture : public PrecompiledFixture
{
public:
    BalancePrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        helloAddress = Address("0x1234654b49838bd3e9466c85a4cc3428c9601234").hex();
        setIsWasm(false, true);
    }

    ~BalancePrecompiledFixture() override = default;


    ExecutionMessage::UniquePtr setAccountStatus(protocol::BlockNumber _number, Address account,
        uint8_t status, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig("setAccountStatus(address,uint8)", account, status);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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

        // call account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);
        result2->takeKeyLocks();

        // call committee manager to get _committee
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK(result3->status() == 0);
        BOOST_CHECK(result3->to() == ACCOUNT_MGR_ADDRESS);
        BOOST_CHECK(result3->from() == AUTH_COMMITTEE_ADDRESS);
        BOOST_CHECK(result3->type() == ExecutionMessage::FINISHED);

        result3->setSeq(1000);

        /// committee manager call back to account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        BOOST_CHECK(result4->status() == 0);
        BOOST_CHECK(result4->from() == ACCOUNT_MGR_ADDRESS);
        BOOST_CHECK(result4->type() == ExecutionMessage::MESSAGE);

        result4->setSeq(1002);

        /// account manager call to committee, get committee info
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        BOOST_CHECK(result5->status() == 0);
        BOOST_CHECK(result5->to() == ACCOUNT_MGR_ADDRESS);
        BOOST_CHECK(result5->type() == ExecutionMessage::FINISHED);

        result5->setSeq(1000);

        /// committee call back to account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(std::move(result5),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise6.set_value(std::move(result));
            });
        auto result6 = executePromise6.get_future().get();

        if (errorInAccountManager)
        {
            if (_errorCode != 0)
            {
                BOOST_CHECK(result6->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            commitBlock(_number);
            return result6;
        }

        result6->setSeq(1003);

        // external create
        std::promise<ExecutionMessage::UniquePtr> executePromise7;
        executor->dmcExecuteTransaction(std::move(result6),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise7.set_value(std::move(result));
            });
        auto result7 = executePromise7.get_future().get();

        if (exist)
        {
            // if account exist, just callback
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
        }

        result7->setSeq(1004);

        // external get deploy auth
        std::promise<ExecutionMessage::UniquePtr> executePromise8;
        executor->dmcExecuteTransaction(std::move(result7),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise8.set_value(std::move(result));
            });
        auto result8 = executePromise8.get_future().get();

        result8->setSeq(1003);

        // get deploy auth
        std::promise<ExecutionMessage::UniquePtr> executePromise9;
        executor->dmcExecuteTransaction(std::move(result8),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise9.set_value(std::move(result));
            });
        auto result9 = executePromise9.get_future().get();

        result9->setSeq(1005);

        // external call bfs
        std::promise<ExecutionMessage::UniquePtr> executePromise10;
        executor->dmcExecuteTransaction(std::move(result9),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise10.set_value(std::move(result));
            });
        auto result10 = executePromise10.get_future().get();

        // call bfs success, callback to create
        result10->setSeq(1003);

        std::promise<ExecutionMessage::UniquePtr> executePromise11;
        executor->dmcExecuteTransaction(std::move(result10),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise11.set_value(std::move(result));
            });
        auto result11 = executePromise11.get_future().get();

        // create success, callback to precompiled
        result11->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise12;
        executor->dmcExecuteTransaction(std::move(result11),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise12.set_value(std::move(result));
            });
        auto result12 = executePromise12.get_future().get();

        // external call, set account status
        result12->setSeq(1006);
        std::promise<ExecutionMessage::UniquePtr> executePromise13;
        executor->dmcExecuteTransaction(std::move(result12),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise13.set_value(std::move(result));
            });
        auto result13 = executePromise13.get_future().get();

        // external call back
        result13->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise14;
        executor->dmcExecuteTransaction(std::move(result13),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise14.set_value(std::move(result));
            });
        auto result14 = executePromise14.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result14->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result14;
    };

    ExecutionMessage::UniquePtr getAccountBalance(protocol::BlockNumber _number, Address account,
        int _errorCode = 0, bool exist = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig("getBalance(address)", account);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();
        result->setSeq(1001);

        // external call
        std::promise<ExecutionMessage::UniquePtr> executePromise1;
        executor->dmcExecuteTransaction(std::move(result),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(error->errorMessage() == "account not exist");
                executePromise1.set_value(std::move(result));
            });
        auto result1 = executePromise1.get_future().get();
        commitBlock(_number);
        return result1;
    }

    ExecutionMessage::UniquePtr addAccountBalance(protocol::BlockNumber _number, Address caller,
        Address account, u256 value, int _errorCode = 0, bool exist = false,
        bool errorInAccountManager = false)
    {
        bytes in = codec->encodeWithSig("addBalance(address,uint256)", account, value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        tx->forceSender(caller.asBytes());
        auto hash = tx->hash();
        txpool->hash2Transaction[hash] = tx;
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_number);
        params2->setSeq(10000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call BalancePrecompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise8;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                executePromise8.set_value(std::move(result));
            });
        auto result8 = executePromise8.get_future().get();

        result8->setSeq(10001);

        // external call
        std::promise<ExecutionMessage::UniquePtr> executePromise9;
        executor->dmcExecuteTransaction(std::move(result8),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise9.set_value(std::move(result));
            });
        auto result9 = executePromise9.get_future().get();
        return result9;
    }

    ExecutionMessage::UniquePtr subAccountBalance(protocol::BlockNumber _number, Address account,
        u256 value, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        bytes in = codec->encodeWithSig("subBalance(address,uint256))", account, value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1001);
        result2->takeKeyLocks();

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        commitBlock(_number);
        return result3;
    }

    ExecutionMessage::UniquePtr transfer(protocol::BlockNumber _number, Address from, Address to,
        u256 value, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        bytes in = codec->encodeWithSig("transfer(address,address,uint256)", from, to, value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1001);
        result2->takeKeyLocks();

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        return result3;
    }

    ExecutionMessage::UniquePtr registerCaller(protocol::BlockNumber _number, Address caller,
        int _errorCode = 0, bool exist = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        bytes in = codec->encodeWithSig("registerCaller(address)", caller);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1001);
        result2->takeKeyLocks();

        // call committee manager to get _committee
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK(result3->status() == 0);
        BOOST_CHECK(result3->to() == ACCOUNT_MGR_ADDRESS);
        BOOST_CHECK(result3->from() == AUTH_COMMITTEE_ADDRESS);
        BOOST_CHECK(result3->type() == ExecutionMessage::FINISHED);

        result3->setSeq(1000);

        /// committee manager call back to account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        return result4;
    }

    ExecutionMessage::UniquePtr unregisterCaller(protocol::BlockNumber _number, Address caller,
        u256 value, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        bytes in = codec->encodeWithSig("unregisterCaller(address)", caller);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto newSender = Address(_sender);
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
        params2->setTo(precompiled::BALANCE_PRECOMPILED_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call account manager
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1001);
        result2->takeKeyLocks();

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        commitBlock(_number);
        return result3;
    }


    std::string sender;
    std::string helloAddress;

    // clang-format off
   std::string helloBin =
       "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4dba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(BalancePrecompiledTest, BalancePrecompiledFixture)

BOOST_AUTO_TEST_CASE(BalanceTest)
{
    // set balance feature
    Features features;
    features.set(Features::Flag::feature_balance);
    features.set(Features::Flag::feature_balance_precompiled);
    BOOST_CHECK_EQUAL(features.get(Features::Flag::feature_balance), true);
    BOOST_CHECK_EQUAL(features.get(Features::Flag::feature_balance_precompiled), true);


    // create account
    Address newAccount = Address("0x420f853b49838bd3e9466c85a4cc3428c9601234");
    bcos::protocol::BlockNumber number = 2;
    //   auto response = setAccountStatus(number++, newAccount, 1);
    //   BOOST_CHECK_EQUAL(response->status(), 0);

    // register caller
    auto response1 = registerCaller(number++, newAccount);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test