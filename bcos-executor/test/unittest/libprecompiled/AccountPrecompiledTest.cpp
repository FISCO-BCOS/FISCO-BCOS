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
        helloAddress = Address("0x1234654b49838bd3e9466c85a4cc3428c9601234").hex();
        setIsWasm(false, true);
    }

    ~AccountPrecompiledFixture() override = default;

    ExecutionMessage::UniquePtr deployHelloInAuthCheck(std::string newAddress, BlockNumber _number,
        Address _address = Address(), bool _errorInFrozen = false)
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
        helloAddress = newAddress;

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(newAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(_number, m_blockVersion);
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

        if (_errorInFrozen)
        {
            commitBlock(_number);
            return result;
        }

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


        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), newAddress);
        BOOST_CHECK_LT(result3->gasAvailable(), gas);

        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->create(), false);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->from(), newAddress);
        BOOST_CHECK(result3->to() == sender);

        commitBlock(_number);

        return result3;
    }

    ExecutionMessage::UniquePtr helloGet(
        protocol::BlockNumber _number, int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number, m_blockVersion);
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
        params2->setContextID(_number);
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

    ExecutionMessage::UniquePtr helloSet(protocol::BlockNumber _number, const std::string& _value,
        int _errorCode = 0, Address _address = Address())
    {
        nextBlock(_number, m_blockVersion);
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
        params2->setContextID(_number);
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

    ExecutionMessage::UniquePtr setAccountStatus(protocol::BlockNumber _number, Address account,
        uint8_t status, int _errorCode = 0, bool exist = false, bool errorInAccountManager = false,
        std::string _sender = "1111654b49838bd3e9466c85a4cc3428c9601111")
    {
        nextBlock(_number, m_blockVersion);
        bytes in = codec->encodeWithSig("setAccountStatus(address,uint8)", account, status);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
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

    ExecutionMessage::UniquePtr getAccountStatusByManager(protocol::BlockNumber _number,
        Address account, int _errorCode = 0, bool errorInAccountManager = false,
        bool noExist = true)
    {
        nextBlock(_number, m_blockVersion);
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
        result2->takeKeyLocks();

        // external call
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);
        result3->takeKeyLocks();

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
        nextBlock(_number, m_blockVersion);
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
    std::string helloAddress;

    // clang-format off
    std::string helloBin =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c146100f6575b600080fd5b6100f46004803603602081101561005157600080fd5b810190808035906020019064010000000081111561006e57600080fd5b82018360208201111561008057600080fd5b803590602001918460018302840111640100000000831117156100a257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610179565b005b6100fe610193565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f16801561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b806000908051906020019061018f929190610235565b5050565b606060008054600181600116156101000203166002900480601f01602080910402602001604051908101604052809291908181526020018280546001816001161561010002031660029004801561022b5780601f106102005761010080835404028352916020019161022b565b820191906000526020600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061027657805160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760008160009055506001016102bb565b5090565b9056fea2646970667358221220bf4a4547462412a2d27d205b50ba5d4dba42f506f9ea3628eb3d0299c9c28d5664736f6c634300060a0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(AccountPrecompiledTest, AccountPrecompiledFixture)

BOOST_AUTO_TEST_CASE(createAccountTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address errorAccount = Address("17505f128bd4d00c2698441b1f54ef843b837211");
    BlockNumber number = 2;
    {
        auto response = setAccountStatus(number++, newAccount, 0);
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

    // setAccount exist again
    {
        setAccountStatus(number++, newAccount, 0, 0, true);
    }

    // check status is 0
    {
        auto response = getAccountStatusByManager(number++, newAccount);
        BOOST_CHECK(response->status() == 0);
        uint8_t status = UINT8_MAX;
        codec->decode(response->data(), status);
        BOOST_CHECK(status == 0);

        // not exist account in chain, return 0 by default
        auto response2 = getAccountStatusByManager(number++, errorAccount);
        BOOST_CHECK(response2->status() == 0);
        uint8_t status2 = UINT8_MAX;
        codec->decode(response2->data(), status2);
        BOOST_CHECK(status2 == 0);
    }

    // check status by account contract
    {
        auto response = getAccountStatus(number++, newAccount);
        BOOST_CHECK(response->status() == 0);
        uint8_t status = UINT8_MAX;
        codec->decode(response->data(), status);
        BOOST_CHECK(status == 0);
        auto response2 = getAccountStatus(number++, errorAccount);
        BOOST_CHECK(response2->status() == (int32_t)TransactionStatus::CallAddressError);
    }
}

BOOST_AUTO_TEST_CASE(setAccountStatusTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address errorAccount = Address("17505f128bd4d00c2698441b1f54ef843b837211");
    Address h1 = Address("12305f128bd4d00c2698441b1f54ef843b837123");
    BlockNumber number = 2;

    // setAccountStatus account not exist
    {
        auto response = setAccountStatus(number++, newAccount, 0);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);
    }

    // use account to deploy
    {
        auto response1 = deployHelloInAuthCheck(helloAddress, number++, newAccount);
        BOOST_CHECK(response1->status() == 0);
        auto response2 = helloSet(number++, "test1", 0, newAccount);
        BOOST_CHECK(response2->status() == 0);
        auto response3 = helloGet(number++, 0, newAccount);
        std::string hello;
        codec->decode(response3->data(), hello);
        BOOST_CHECK(hello == "test1");
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
        uint8_t status = UINT8_MAX;
        codec->decode(rsp2->data(), status);
        BOOST_CHECK(status == 1);
    }

    // use freeze account to use
    {
        auto response1 = deployHelloInAuthCheck(h1.hex(), number++, newAccount, true);
        BOOST_CHECK(response1->status() == (uint32_t)TransactionStatus::AccountFrozen);
        auto response2 = helloSet(number++, "test2", 0, newAccount);
        BOOST_CHECK(response2->status() == (uint32_t)TransactionStatus::AccountFrozen);

        auto response3 = helloGet(number++, 0);
        BOOST_CHECK(response3->status() == (uint32_t)TransactionStatus::CallAddressError);
    }

    // use error account to setAccountStatus
    {
        auto response =
            setAccountStatus(number++, newAccount, 1, 0, true, true, errorAccount.hex());
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == CODE_NO_AUTHORIZED);
    }
}

BOOST_AUTO_TEST_CASE(setAccountStatusErrorTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address errorAccount = Address("17505f128bd4d00c2698441b1f54ef843b837211");
    BlockNumber number = 2;

    // setAccountStatus account not exist
    {
        auto response = setAccountStatus(number++, newAccount, 0);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);
    }

    // use not governor account set
    {
        auto response = setAccountStatus(
            number++, newAccount, 1, CODE_NO_AUTHORIZED, false, true, errorAccount.hex());
        BOOST_CHECK(response->status() == 0);
    }

    // set governor account status
    {
        auto response = setAccountStatus(number++, Address(admin), 1, 0, false, true);
        BOOST_CHECK(response->status() == 15);
        BOOST_CHECK(response->message() == "Should not set governor's status.");
    }
}

BOOST_AUTO_TEST_CASE(abolishTest)
{
    Address newAccount = Address("27505f128bd4d00c2698441b1f54ef843b837215");
    Address h1 = Address("12305f128bd4d00c2698441b1f54ef843b837123");
    BlockNumber number = 2;

    // setAccountStatus account not exist
    {
        auto response = setAccountStatus(number++, newAccount, 0);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);
    }

    // use account to deploy
    {
        auto response1 = deployHelloInAuthCheck(helloAddress, number++, newAccount);
        BOOST_CHECK(response1->status() == 0);
        auto response2 = helloSet(number++, "test1", 0, newAccount);
        BOOST_CHECK(response2->status() == 0);
        auto response3 = helloGet(number++, 0, newAccount);
        std::string hello;
        codec->decode(response3->data(), hello);
        BOOST_CHECK(hello == "test1");
    }

    // setAccountStatus account exist, abolish account
    {
        auto response = setAccountStatus(number++, newAccount, 2, 0, true);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);

        auto rsp2 = getAccountStatus(number++, newAccount);
        BOOST_CHECK(rsp2->status() == 0);
        uint8_t status = UINT8_MAX;
        codec->decode(rsp2->data(), status);
        BOOST_CHECK(status == 2);
    }

    // use abolish account to use
    {
        auto response1 = deployHelloInAuthCheck(h1.hex(), number++, newAccount, true);
        BOOST_CHECK(response1->status() == (uint32_t)TransactionStatus::AccountAbolished);
        auto response2 = helloSet(number++, "test2", 0, newAccount);
        BOOST_CHECK(response2->status() == (uint32_t)TransactionStatus::AccountAbolished);
    }

    // freeze/unfreeze abolish account status
    {
        auto response = setAccountStatus(number++, newAccount, 1, 0, true);
        BOOST_CHECK(response->status() == (uint32_t)TransactionStatus::PrecompiledError);

        auto response2 = setAccountStatus(number++, newAccount, 0, 0, true);
        BOOST_CHECK(response2->status() == (uint32_t)TransactionStatus::PrecompiledError);
    }

    // abolish account again, success
    {
        auto response = setAccountStatus(number++, newAccount, 2, 0, true);
        BOOST_CHECK(response->status() == 0);
        int32_t result = -1;
        codec->decode(response->data(), result);
        BOOST_CHECK(result == 0);

        auto rsp2 = getAccountStatus(number++, newAccount);
        BOOST_CHECK(rsp2->status() == 0);
        uint8_t status = UINT8_MAX;
        codec->decode(rsp2->data(), status);
        BOOST_CHECK(status == 2);
    }
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test