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
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class ShardPrecompiledFixture : public PrecompiledFixture
{
public:
    ShardPrecompiledFixture() = default;

    ~ShardPrecompiledFixture() override = default;

    void init(bool _isWasm, protocol::BlockVersion version = DEFAULT_VERSION)
    {
        setIsWasm(_isWasm, false, true, version);
        bfsAddress = _isWasm ? precompiled::BFS_NAME : BFS_ADDRESS;
        shardingPrecompiledAddress =
            _isWasm ? precompiled::SHARDING_PRECOMPILED_NAME : SHARDING_PRECOMPILED_ADDRESS;
        tableAddress = _isWasm ? precompiled::KV_TABLE_NAME : KV_TABLE_ADDRESS;
        tableTestAddress1 = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
        tableTestAddress2 = Address("0x420f853b49838bd3e9466c85a4cc3428c9601234").hex();

        if (_isWasm)
        {
            auto result1 = creatKVTable(1, "test1", "id", "item1", "/tables/test1");
            BOOST_TEST(result1->data().toBytes() == codec->encode(int32_t(0)));
            auto result2 = creatKVTable(2, "test2", "id", "item1", "/tables/test2");
            BOOST_TEST(result2->data().toBytes() == codec->encode(int32_t(0)));
        }
        else
        {
            auto result1 = creatKVTable(1, "test1", "id", "item1", tableTestAddress1);
            BOOST_TEST(result1->data().toBytes() == codec->encode(int32_t(0)));
            auto result2 = creatKVTable(2, "test2", "id", "item1", tableTestAddress2);
            BOOST_TEST(result2->data().toBytes() == codec->encode(int32_t(0)));
        }

        h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        addressString = addressCreate.hex().substr(0, 40);
    }

    void deployHelloContract(protocol::BlockNumber _number, std::string const& address)
    {
        std::string helloBin =
            "608060405234801561001057600080fd5b506040805190810160405280600d81526020017f48656c6c6f2c"
            "20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c92"
            "9190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090"
            "601f016020900481019282601f106100a357805160ff19168380011785556100d1565b8280016001018555"
            "82156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100"
            "de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b50"
            "90565b90565b61047a806101166000396000f300608060405260043610610062576000357c010000000000"
            "0000000000000000000000000000000000000000000000900463ffffffff16806306fdde03146100675780"
            "634ed3885e146100f75780636d4ce63c14610160578063b8368615146101f0575b600080fd5b3480156100"
            "7357600080fd5b5061007c610247565b604051808060200182810382528381815181526020019150805190"
            "6020019080838360005b838110156100bc5780820151818401526020810190506100a1565b505050509050"
            "90810190601f1680156100e95780820380516001836020036101000a031916815260200191505b50925050"
            "5060405180910390f35b34801561010357600080fd5b5061015e6004803603810190808035906020019082"
            "01803590602001908080601f01602080910402602001604051908101604052809392919081815260200183"
            "838082843782019150505050505091929192905050506102e5565b005b34801561016c57600080fd5b5061"
            "01756102ff565b604051808060200182810382528381815181526020019150805190602001908083836000"
            "5b838110156101b557808201518184015260208101905061019a565b50505050905090810190601f168015"
            "6101e25780820380516001836020036101000a031916815260200191505b509250505060405180910390f3"
            "5b3480156101fc57600080fd5b506102056103a1565b604051808273ffffffffffffffffffffffffffffff"
            "ffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390"
            "f35b60008054600181600116156101000203166002900480601f0160208091040260200160405190810160"
            "405280929190818152602001828054600181600116156101000203166002900480156102dd5780601f1061"
            "02b2576101008083540402835291602001916102dd565b820191906000526020600020905b815481529060"
            "0101906020018083116102c057829003601f168201915b505050505081565b806000908051906020019061"
            "02fb9291906103a9565b5050565b606060008054600181600116156101000203166002900480601f016020"
            "80910402602001604051908101604052809291908181526020018280546001816001161561010002031660"
            "02900480156103975780601f1061036c57610100808354040283529160200191610397565b820191906000"
            "526020600020905b81548152906001019060200180831161037a57829003601f168201915b505050505090"
            "5090565b600030905090565b82805460018160011615610100020316600290049060005260206000209060"
            "1f016020900481019282601f106103ea57805160ff1916838001178555610418565b828001600101855582"
            "15610418579182015b828111156104175782518255916020019190600101906103fc565b5b509050610425"
            "9190610429565b5090565b61044b91905b8082111561044757600081600090555060010161042f565b5090"
            "565b905600a165627a7a723058208c7b44898edb531f977931e72d1195b47424ff97a80e0d22932be8fab3"
            "6bd9750029";
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

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(address);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        nextBlock(_number);
        // --------------------------------
        // Create contract HelloWorld
        // --------------------------------

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();
        commitBlock(_number);
        BOOST_TEST(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), address);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), address);
    }

    ExecutionMessage::UniquePtr creatKVTable(protocol::BlockNumber _number,
        const std::string& tableName, const std::string& key, const std::string& value,
        const std::string& solidityAddress, int _errorCode = 0, bool errorInTableManager = false)
    {
        nextBlock(_number, m_blockVersion);
        bytes in =
            codec->encodeWithSig("createKVTable(string,string,string)", tableName, key, value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(100), 10000, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(std::string(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS));
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        if (errorInTableManager)
        {
            if (_errorCode != 0)
            {
                BOOST_TEST(result2->data().toBytes() == codec->encode(s256(_errorCode)));
            }
            commitBlock(_number);
            return result2;
        }

        // set new address
        result2->setTo(solidityAddress);
        // external create
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1002);
        // external call bfs
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        // call bfs success, callback to create
        result4->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        // create success, callback to precompiled
        result5->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(std::move(result5),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise6.set_value(std::move(result));
            });
        auto result6 = executePromise6.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_TEST(result6->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result6;
    };

    ExecutionMessage::UniquePtr makeShard(protocol::BlockNumber _number,
        std::string const& shardName, int _errorCode = 0, bool errorInPrecompiled = false)
    {
        bytes in = codec->encodeWithSig("makeShard(string)", shardName);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1001);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(shardingPrecompiledAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (errorInPrecompiled)
        {
            commitBlock(_number);
            return result2;
        }
        // call precompiled
        result2->setSeq(1001);
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                // if _errorCode not 0, error should has value
                BOOST_TEST(!error == (_errorCode == 0));
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        /*
                if (_errorCode != 0)
                {
                    if (isWasm && versionCompareTo(m_blockVersion, BlockVersion::V3_2_VERSION) >= 0)
                    {
                        BOOST_TEST(result4->data().toBytes() ==
           codec->encode(int32_t(_errorCode)));
                    }
                    else
                    {
                        BOOST_TEST(result4->data().toBytes() == codec->encode(s256(_errorCode)));
                    }
                }
        */
        commitBlock(_number);
        return result4;
    };

    ExecutionMessage::UniquePtr mkdir(protocol::BlockNumber _number, std::string const& path,
        int _errorCode = 0, bool errorInPrecompiled = false)
    {
        bytes in = codec->encodeWithSig("mkdir(string)", path);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(bfsAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (errorInPrecompiled)
        {
            commitBlock(_number);
            return result2;
        }
        // call precompiled
        result2->setSeq(1001);
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                // if _errorCode not 0, error should has value
                BOOST_TEST(!error == (_errorCode == 0));
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        /*
                if (_errorCode != 0)
                {
                    if (isWasm && versionCompareTo(m_blockVersion, BlockVersion::V3_2_VERSION) >= 0)
                    {
                        BOOST_TEST(result4->data().toBytes() ==
           codec->encode(int32_t(_errorCode)));
                    }
                    else
                    {
                        BOOST_TEST(result4->data().toBytes() == codec->encode(s256(_errorCode)));
                    }
                }
        */
        commitBlock(_number);
        return result4;
    };


    ExecutionMessage::UniquePtr linkShard([[maybe_unused]] bool _isWasm,
        protocol::BlockNumber _number, std::string const& name, std::string const& address,
        int _errorCode = 0, bool _isCover = false)
    {
        bytes in;

        in = codec->encodeWithSig("linkShard(string,string)", name, address);

        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(shardingPrecompiledAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        // linkShard
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // if cover write link, then
        // no need to touch new file external call
        if (_isCover)
        {
            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);
        result2->setKeyLocks({});

        // setContractShard
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        // setContractShard ret
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        result4->setSeq(1001);

        // touch
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(std::move(result4),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();
        result5->setSeq(1000);

        // touch ret
        std::promise<ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(std::move(result5),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise6.set_value(std::move(result));
            });
        auto result6 = executePromise6.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_TEST(result6->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result6;
    };

    ExecutionMessage::UniquePtr getContractShard(
        protocol::BlockNumber _number, std::string const& contract, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("getContractShard(string)", contract);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(1000);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(shardingPrecompiledAddress);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        // getContractShard
        std::promise<ExecutionMessage::UniquePtr> executePromise1;
        executor->dmcExecuteTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                // if _errorCode not 0, error should has value
                BOOST_TEST(!error);
                executePromise1.set_value(std::move(result));
            });
        auto result2 = executePromise1.get_future().get();
        if (_errorCode != 0)
        {
            return result2;
        }

        result2->setSeq(1001);

        // getShardInternal
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        result3->setSeq(1000);

        // getShardInternal ret
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();
        commitBlock(_number);
        return result4;
    };

    ExecutionMessage::UniquePtr link([[maybe_unused]] bool _isWasm, protocol::BlockNumber _number,
        std::string const& name, std::string const& version, std::string const& address,
        std::string const& abi, int _errorCode = 0, bool _isCover = false)
    {
        bytes in;
        if (version.empty())
        {
            in = codec->encodeWithSig("link(string,string,string)", name, address, abi);
        }
        else
        {
            in = codec->encodeWithSig(
                "link(string,string,string,string)", name, version, address, abi);
        }
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(bfsAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // if cover write link, then
        // no need to touch new file external call
        if (_isCover)
        {
            if (_errorCode != 0)
            {
                BOOST_TEST(result2->data().toBytes() == codec->encode(s256(_errorCode)));
            }

            commitBlock(_number);
            return result2;
        }

        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        result3->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_TEST(result4->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result4;
    };

    ExecutionMessage::UniquePtr readlink(
        protocol::BlockNumber _number, std::string const& _path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("readlink(string)", _path);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(bfsAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            BOOST_TEST(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr rebuildBfs(
        protocol::BlockNumber _number, uint32_t from, uint32_t to, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("rebuildBfs(uint256,uint256)", from, to);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        Address newSender = Address(isWasm ? std::string(precompiled::SYS_CONFIG_NAME) :
                                             std::string(precompiled::SYS_CONFIG_ADDRESS));
        tx->forceSender(newSender.asBytes());
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(bfsAddress);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            BOOST_TEST(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr rebuildBfsBySysConfig(
        protocol::BlockNumber _number, std::string version, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("setValueByKey(string,string)",
            std::string(ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), version);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        Address newSender = Address(std::string(precompiled::AUTH_COMMITTEE_ADDRESS));
        tx->forceSender(newSender.asBytes());
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(std::string(isWasm ? SYS_CONFIG_NAME : SYS_CONFIG_ADDRESS));
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call to BFS
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        // BFS callback to sys
        result3->setSeq(1000);

        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(std::move(result3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_TEST(result4->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result4;
    };

    ExecutionMessage::UniquePtr listPage(protocol::BlockNumber _number, std::string const& path,
        uint32_t offset, uint32_t count, int _errorCode = 0)
    {
        bytes in =
            codec->encodeWithSig("list(string,uint256,uint256)", path, u256(offset), u256(count));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(std::string(isWasm ? BFS_NAME : BFS_ADDRESS));
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_TEST(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            std::vector<BfsTuple> empty;
            BOOST_TEST(result2->data().toBytes() == codec->encode(s256(_errorCode), empty));
        }

        commitBlock(_number);
        return result2;
    };

    std::string sender;
    std::string addressString;
    std::string bfsAddress;
    std::string shardingPrecompiledAddress;
    std::string tableAddress;
    std::string tableTestAddress1;
    std::string tableTestAddress2;
};
BOOST_FIXTURE_TEST_SUITE(shardPrecompiledTest, ShardPrecompiledFixture)

BOOST_AUTO_TEST_CASE(makeShardTest)
{
    init(false);
    bcos::protocol::BlockNumber _number = 3;

    {
        auto result = makeShard(_number++, "hello");
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/shards");
        std::vector<BfsTuple> ls;
        s256 code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(std::get<0>(ls[0]) == "hello");
        BOOST_TEST(std::get<1>(ls[0]) == executor::FS_TYPE_DIR);
    }

    {
        auto result = makeShard(_number++, "/shards/hello", CODE_FILE_INVALID_TYPE, true);
    }

    {
        auto result = makeShard(_number++, "hello/world", CODE_FILE_INVALID_TYPE, true);
    }
}

BOOST_AUTO_TEST_CASE(couldNotMakeShardTest)
{
    init(false, protocol::BlockVersion::V3_3_VERSION);
    bcos::protocol::BlockNumber _number = 3;
    // must could not mkShard shard in normal BFS precompiled
    {
        auto result = mkdir(_number++, "/shards/hello", CODE_FILE_BUILD_DIR_FAILED, true);
    }
}

BOOST_AUTO_TEST_CASE(linkShardTest)
{
    init(false);
    bcos::protocol::BlockNumber number = 3;
    deployHelloContract(number++, addressString);

    std::string shardName = "hello";
    std::string contractVersion = "1.0";
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":"
        "\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,"
        "\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{"
        "\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";

    // link overflow
    std::string overflowVersion130 =
        "012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789";

    {
        auto result = makeShard(number++, shardName);
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(number++, "/shards");
        std::vector<BfsTuple> ls;
        s256 code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(std::get<0>(ls[0]) == "hello");
        BOOST_TEST(std::get<1>(ls[0]) == executor::FS_TYPE_DIR);
    }

    // check empty shard
    {
        auto shardInfo = getContractShard(number++, addressString, 0);
        s256 code;
        std::string shardCmp;
        codec->decode(shardInfo->data(), code, shardCmp);
        BOOST_CHECK_EQUAL("", shardCmp);
    }

    // simple link shard
    {
        linkShard(false, number++, shardName, addressString);
        auto result = list(number++, "/shards/" + shardName);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == addressString);
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);

        auto shardInfo = getContractShard(number++, addressString, 0);
        std::string shardCmp;
        codec->decode(shardInfo->data(), code, shardCmp);
        BOOST_CHECK_EQUAL(shardName, shardCmp);
    }


    // error link shard
    {
        std::string errorShardName = "hello/world";
        auto result =
            linkShard(false, number++, errorShardName, addressString, CODE_FILE_INVALID_TYPE, true);
    }
}

BOOST_AUTO_TEST_CASE(getContractShardErrorTest)
{
    init(false);
    bcos::protocol::BlockNumber number = 3;
    // invalid contract address
    {
        auto shardInfo = getContractShard(number++, "kkkkk", CODE_FILE_INVALID_PATH);
    }

    // error contract address
    {
        std::string noExistAddress = "0x420f853b49838bd3e9466c85a4cc3428c9608888";
        auto shardInfo = getContractShard(number++, noExistAddress, 0);
        s256 code;
        std::string shardCmp;
        codec->decode(shardInfo->data(), code, shardCmp);
        BOOST_CHECK_EQUAL(code, s256(int(CODE_SUCCESS)));
        BOOST_CHECK_EQUAL("", shardCmp);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
