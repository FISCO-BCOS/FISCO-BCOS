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

#include "bcos-framework/protocol/Protocol.h"
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
class FileSystemPrecompiledFixture : public PrecompiledFixture
{
public:
    FileSystemPrecompiledFixture() = default;

    ~FileSystemPrecompiledFixture() override = default;

    void init(bool _isWasm, protocol::BlockVersion version = BlockVersion::V3_1_VERSION,
        std::shared_ptr<std::set<std::string, std::less<>>> _ignoreTables = nullptr)
    {
        setIsWasm(_isWasm, false, true, version, _ignoreTables);
        bfsAddress = _isWasm ? precompiled::BFS_NAME : BFS_ADDRESS;
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
                BOOST_TEST(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        if (_errorCode != 0)
        {
            if (isWasm && versionCompareTo(m_blockVersion, BlockVersion::V3_2_VERSION) >= 0)
            {
                BOOST_TEST(result4->data().toBytes() == codec->encode(int32_t(_errorCode)));
            }
            else
            {
                BOOST_TEST(result4->data().toBytes() == codec->encode(s256(_errorCode)));
            }
        }

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

    ExecutionMessage::UniquePtr rebuildBfsBySysConfig(protocol::BlockNumber _number,
        std::string version, int _errorCode = 0, std::function<void()> funcBeforeCommit = nullptr)
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

        if (funcBeforeCommit != nullptr)
        {
            funcBeforeCommit();
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
    std::string tableAddress;
    std::string tableTestAddress1;
    std::string tableTestAddress2;
};
BOOST_FIXTURE_TEST_SUITE(precompiledFileSystemTest, FileSystemPrecompiledFixture)

BOOST_AUTO_TEST_CASE(lsTest)
{
    init(false);
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 4);
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        for (auto const& rootSub : tool::FS_ROOT_SUBS | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(rootSub.substr(1))));
        }
    }

    // ls /sys
    {
        auto result = list(_number++, "/sys");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 2);
        }
        else if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 1);
        }
        else
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT);
        }
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        auto take = precompiled::BFS_SYS_SUBS_COUNT;
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            // remove cast
            take--;
        }
        if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            // remove shard
            take--;
        }
        for (auto const& sysSub :
            precompiled::BFS_SYS_SUBS | RANGES::views::take(take) | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(sysSub.substr(tool::FS_SYS_BIN.size() + 1))));
        }
    }
}

BOOST_AUTO_TEST_CASE(lsPageTest)
{
    init(false);
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = listPage(_number++, "/tables", 0, 500);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");

        result = listPage(_number++, "/tables", 1, 2);
        ls.clear();
        ls.shrink_to_fit();
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
    }

    // ls regular
    {
        auto result = listPage(_number++, "/tables/test2", 0, 500);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls not exist
    {
        auto result = listPage(_number++, "/tables/test3", 0, 500, CODE_FILE_NOT_EXIST);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls invalid path
    {
        listPage(_number++, "", 0, 500, CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        listPage(_number++, "/" + errorPath.str(), 0, 500, CODE_FILE_INVALID_PATH);
        listPage(_number++, "/path/level/too/deep/not/over/six/", 0, 500, CODE_FILE_INVALID_PATH);
    }

    // ls /
    {
        auto result = listPage(_number++, "/", 0, 500);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 4);
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        for (auto const& rootSub : tool::FS_ROOT_SUBS | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(rootSub.substr(1))));
        }
    }

    // ls /sys
    {
        auto result = listPage(_number++, "/sys", 0, 500);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 2);
        }
        else if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 1);
        }
        else
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT);
        }
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        auto take = precompiled::BFS_SYS_SUBS_COUNT;
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            // remove cast
            take--;
        }
        if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            // remove shard
            take--;
        }
        for (auto const& sysSub :
            precompiled::BFS_SYS_SUBS | RANGES::views::take(take) | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(sysSub.substr(tool::FS_SYS_BIN.size() + 1))));
        }
    }
}

BOOST_AUTO_TEST_CASE(lsPagWasmTest)
{
    init(true);
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = listPage(_number++, "/tables", 0, 500);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");

        result = listPage(_number++, "/tables", 1, 2);
        ls.clear();
        ls.shrink_to_fit();
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
    }

    // ls regular
    {
        auto result = listPage(_number++, "/tables/test2", 0, 500);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_CONTRACT);
    }

    // ls not exist
    {
        auto result = listPage(_number++, "/tables/test3", 0, 500, CODE_FILE_NOT_EXIST);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls invalid path
    {
        listPage(_number++, "", 0, 500, CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        listPage(_number++, "/" + errorPath.str(), 0, 500, CODE_FILE_INVALID_PATH);
        listPage(_number++, "/path/level/too/deep/not/over/six/", 0, 500, CODE_FILE_INVALID_PATH);
    }

    // ls /
    {
        auto result = listPage(_number++, "/", 0, 500);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 4);
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        for (auto const& rootSub : tool::FS_ROOT_SUBS | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(rootSub.substr(1))));
        }
    }

    // ls /sys
    {
        auto result = listPage(_number++, "/sys", 0, 500);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 2);
        }
        else if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 1);
        }
        else
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT);
        }
        std::set<std::string> lsSet;
        for (const auto& item : ls | RANGES::views::transform([](auto&& bfs) -> std::string {
                 return std::get<0>(bfs);
             }))
        {
            lsSet.insert(item);
        }

        auto take = precompiled::BFS_SYS_SUBS_COUNT;
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            // remove cast
            take--;
        }
        if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            // remove shard
            take--;
        }
        for (auto const& sysSub :
            precompiled::BFS_SYS_SUBS | RANGES::views::take(take) | RANGES::views::drop(1))
        {
            BOOST_TEST(lsSet.contains(std::string(sysSub.substr(tool::FS_SYS_BIN.size() + 1))));
        }
    }
}

BOOST_AUTO_TEST_CASE(lsTest_3_0)
{
    init(false, BlockVersion::V3_0_VERSION);
    m_blockVersion = BlockVersion::V3_0_VERSION;
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);  // with '/'
    }

    // mkdir invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(lsTestWasm)
{
    init(true);
    bcos::protocol::BlockNumber _number = 3;
    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == executor::FS_TYPE_CONTRACT);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 4);  // with '/'
    }

    // mkdir invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(lsTestWasm_3_0)
{
    init(true, BlockVersion::V3_0_VERSION);
    bcos::protocol::BlockNumber _number = 3;
    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == executor::FS_TYPE_CONTRACT);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_TEST(ls.empty());
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);  // with '/'
    }

    // mkdir invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(mkdirTest)
{
    init(false);
    bcos::protocol::BlockNumber _number = 3;
    // simple mkdir
    {
        auto result = mkdir(_number++, "/tables/temp/test");
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/tables");
        std::vector<BfsTuple> ls;
        s256 code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);

        auto lsResult2 = list(_number++, "/tables/temp");
        std::vector<BfsTuple> ls2;
        codec->decode(lsResult2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<0>(ls2[0]) == "test");
        BOOST_TEST(std::get<1>(ls2[0]) == executor::FS_TYPE_DIR);
    }

    // mkdir /tables/test1/test
    {
        auto result = mkdir(_number++, "/tables/test1/test", CODE_FILE_BUILD_DIR_FAILED);
    }

    // mkdir /tables/test1
    {
        auto result = mkdir(_number++, "/tables/test1", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
    }

    // mkdir /tables
    {
        auto result = mkdir(_number++, "/tables", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
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
    }
}

BOOST_AUTO_TEST_CASE(mkdirWasmTest)
{
    init(true);
    bcos::protocol::BlockNumber _number = 3;
    // simple mkdir
    {
        auto result = mkdir(_number++, "/tables/temp/test");
        int32_t m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/tables");
        std::vector<BfsTuple> ls;
        int32_t code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);

        auto lsResult2 = list(_number++, "/tables/temp");
        std::vector<BfsTuple> ls2;
        codec->decode(lsResult2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<0>(ls2[0]) == "test");
        BOOST_TEST(std::get<1>(ls2[0]) == executor::FS_TYPE_DIR);
    }

    // mkdir /tables/test1/test
    {
        auto result = mkdir(_number++, "/tables/test1/test", CODE_FILE_BUILD_DIR_FAILED);
    }

    // mkdir /tables/test1
    {
        auto result = mkdir(_number++, "/tables/test1", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode((int32_t)CODE_FILE_ALREADY_EXIST));
    }

    // mkdir /tables
    {
        auto result = mkdir(_number++, "/tables", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode((int32_t)CODE_FILE_ALREADY_EXIST));
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
    }
}

BOOST_AUTO_TEST_CASE(mkdirTest_3_0)
{
    init(false, protocol::BlockVersion::V3_0_VERSION);
    bcos::protocol::BlockNumber _number = 3;
    // simple mkdir
    {
        auto result = mkdir(_number++, "/tables/temp/test");
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/tables");
        std::vector<BfsTuple> ls;
        s256 code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);

        auto lsResult2 = list(_number++, "/tables/temp");
        std::vector<BfsTuple> ls2;
        codec->decode(lsResult2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<0>(ls2[0]) == "test");
        BOOST_TEST(std::get<1>(ls2[0]) == executor::FS_TYPE_DIR);
    }

    // mkdir /tables/test1/test
    {
        auto result = mkdir(_number++, "/tables/test1/test", CODE_FILE_BUILD_DIR_FAILED);
    }

    // mkdir /tables/test1
    {
        auto result = mkdir(_number++, "/tables/test1", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
    }

    // mkdir /tables
    {
        auto result = mkdir(_number++, "/tables", 0, true);
        BOOST_TEST(result->data().toBytes() == codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
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
    }
}

BOOST_AUTO_TEST_CASE(linkTest)
{
    init(false);
    bcos::protocol::BlockNumber number = 3;
    deployHelloContract(number++, addressString);

    std::string contractName = "Hello";
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
    // simple link
    {
        link(false, number++, contractName, contractVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/1.0");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<0>(ls2.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls2.at(0)) == tool::FS_TYPE_LINK);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(0) == addressString);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(1) == contractAbi);

        auto result3 = readlink(number++, "/apps/Hello/1.0");
        Address address;
        codec->decode(result3->data(), address);
        BOOST_CHECK_EQUAL(address.hex(), addressString);
    }

    // overwrite link
    {
        auto latestVersion = "latest";
        link(false, number++, contractName, latestVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(0) == addressString);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(1) == contractAbi);

        auto resultR1 = readlink(number++, "/apps/Hello/1.0");
        Address address;
        codec->decode(resultR1->data(), address);
        BOOST_CHECK_EQUAL(address.hex(), addressString);

        // cover write
        auto newAddress = "420f853b49838bd3e9466c85a4cc3428c960dde1";
        deployHelloContract(number++, newAddress);
        link(false, number++, contractName, latestVersion, newAddress, contractAbi, 0, true);
        auto result3 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls3;
        codec->decode(result3->data(), code, ls3);
        BOOST_TEST(ls3.size() == 1);
        BOOST_TEST(std::get<2>(ls3.at(0)).at(0) == newAddress);
        BOOST_TEST(std::get<2>(ls3.at(0)).at(1) == contractAbi);

        auto resultR2 = readlink(number++, "/apps/Hello/latest");
        Address address2;
        codec->decode(resultR2->data(), address2);
        BOOST_CHECK_EQUAL(address2.hex(), newAddress);
    }

    // wrong version
    {
        auto errorVersion = "ver/tion";
        link(false, number++, contractName, errorVersion, addressString, contractAbi,
            CODE_ADDRESS_OR_VERSION_ERROR, true);
    }

    // wrong address
    {
        auto wrongAddress = addressString;
        std::reverse(wrongAddress.begin(), wrongAddress.end());
        link(false, number++, contractName, contractVersion, wrongAddress, contractAbi,
            CODE_ADDRESS_OR_VERSION_ERROR, true);
    }

    // overflow version
    {
        std::stringstream errorVersion;
        for (size_t i = 0; i < FS_PATH_MAX_LENGTH - contractName.size(); ++i)
        {
            errorVersion << "1";
        }
        link(false, number++, contractName, errorVersion.str(), addressString, contractAbi,
            CODE_FILE_INVALID_PATH, true);
    }

    // simple link without version
    {
        std::string newAbsolutePath = "link_test";
        link(false, number++, newAbsolutePath, "", addressString, contractAbi);
        auto result = list(number++, "/apps/link_test");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "link_test");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
        BOOST_TEST(std::get<2>(ls.at(0)).at(0) == addressString);
        BOOST_TEST(std::get<2>(ls.at(0)).at(1) == contractAbi);

        auto result3 = readlink(number++, "/apps/link_test");
        Address address;
        codec->decode(result3->data(), address);
        BOOST_CHECK_EQUAL(address.hex(), addressString);
    }
}

BOOST_AUTO_TEST_CASE(linkTest_3_0)
{
    init(false, protocol::BlockVersion::V3_0_VERSION);
    bcos::protocol::BlockNumber number = 3;
    deployHelloContract(number++, addressString);

    std::string contractName = "Hello";
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
    // simple link
    {
        link(false, number++, contractName, contractVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/1.0");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<0>(ls2.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls2.at(0)) == tool::FS_TYPE_LINK);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(0) == addressString);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(1) == contractAbi);

        auto result3 = readlink(number++, "/apps/Hello/1.0");
        Address address;
        codec->decode(result3->data(), address);
        BOOST_CHECK_EQUAL(address.hex(), addressString);
    }

    // overwrite link
    {
        auto latestVersion = "latest";
        link(false, number++, contractName, latestVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_TEST(ls2.size() == 1);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(0) == addressString);
        BOOST_TEST(std::get<2>(ls2.at(0)).at(1) == contractAbi);

        auto resultR1 = readlink(number++, "/apps/Hello/1.0");
        Address address;
        codec->decode(resultR1->data(), address);
        BOOST_CHECK_EQUAL(address.hex(), addressString);

        // cover write
        auto newAddress = "420f853b49838bd3e9466c85a4cc3428c960dde1";
        deployHelloContract(number++, newAddress);
        link(false, number++, contractName, latestVersion, newAddress, contractAbi, 0, true);
        auto result3 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls3;
        codec->decode(result3->data(), code, ls3);
        BOOST_TEST(ls3.size() == 1);
        BOOST_TEST(std::get<2>(ls3.at(0)).at(0) == newAddress);
        BOOST_TEST(std::get<2>(ls3.at(0)).at(1) == contractAbi);

        auto resultR2 = readlink(number++, "/apps/Hello/latest");
        Address address2;
        codec->decode(resultR2->data(), address2);
        BOOST_CHECK_EQUAL(address2.hex(), newAddress);
    }

    // wrong version
    {
        auto errorVersion = "ver/tion";
        link(false, number++, contractName, errorVersion, addressString, contractAbi,
            CODE_ADDRESS_OR_VERSION_ERROR, true);
    }

    // wrong address
    {
        auto wrongAddress = addressString;
        std::reverse(wrongAddress.begin(), wrongAddress.end());
        link(false, number++, contractName, contractVersion, wrongAddress, contractAbi,
            CODE_ADDRESS_OR_VERSION_ERROR, true);
    }

    // overflow version
    {
        std::stringstream errorVersion;
        for (size_t i = 0; i < FS_PATH_MAX_LENGTH - contractName.size(); ++i)
        {
            errorVersion << "1";
        }
        link(false, number++, contractName, errorVersion.str(), addressString, contractAbi,
            CODE_FILE_INVALID_PATH, true);
    }
}

BOOST_AUTO_TEST_CASE(rebuildBfsTest)
{
    init(false, protocol::BlockVersion::V3_0_VERSION);
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);
    }

    const int32_t mkdirCount = 1000;
    mkdir(_number++, "/apps/temp/temp2/temp3/temp4");

    boost::log::core::get()->set_logging_enabled(false);
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> temp4p;
    storage->asyncOpenTable(
        "/apps/temp/temp2/temp3/temp4", [&](Error::UniquePtr _e, std::optional<Table> _t) {
            temp4p.set_value({std::move(_e), std::move(_t)});
        });
    auto [error, temp4T] = temp4p.get_future().get();
    auto subEntry = temp4T->getRow(tool::FS_KEY_SUB);
    std::map<std::string, std::string> bfsInfo;
    auto&& out = asBytes(std::string(subEntry->get()));
    codec::scale::decode(bfsInfo, gsl::make_span(out));
    for (int i = 0; i < mkdirCount; ++i)
    {
        bfsInfo.insert({"test" + std::to_string(i), std::string(tool::FS_TYPE_DIR)});
    }
    subEntry->importFields({asString(codec::scale::encode(bfsInfo))});
    temp4T->setRow(tool::FS_KEY_SUB, std::move(subEntry.value()));
    boost::log::core::get()->set_logging_enabled(true);

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = list(_number++, "/apps/temp/temp2/temp3/temp4");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == mkdirCount);
    }


    // upgrade to v3.1.0
    m_blockVersion = protocol::BlockVersion::V3_1_VERSION;

    boost::log::core::get()->set_logging_enabled(false);
    rebuildBfs(_number++, (uint32_t)protocol::BlockVersion::V3_0_VERSION,
        (uint32_t)protocol::BlockVersion::V3_1_VERSION);
    boost::log::core::get()->set_logging_enabled(true);

    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> p;
    storage->asyncOpenTable(tool::FS_ROOT, [&](Error::UniquePtr _e, std::optional<Table> _t) {
        p.set_value({std::move(_e), std::move(_t)});
    });
    auto [e, t] = p.get_future().get();
    BOOST_TEST(t->getRow(tool::FS_APPS.substr(1)).has_value());
    BOOST_TEST(t->getRow(tool::FS_USER_TABLE.substr(1)).has_value());
    BOOST_TEST(t->getRow(tool::FS_SYS_BIN.substr(1)).has_value());
    BOOST_TEST(!t->getRow(tool::FS_KEY_SUB).has_value());

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);
    }

    // ls /sys
    {
        auto result = list(_number++, "/sys");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 2);
        }
        else if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 1);
        }
        else
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT);
        }
    }

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = list(_number++, "/apps/temp/temp2/temp3/temp4");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == USER_TABLE_MAX_LIMIT_COUNT);
    }

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = listPage(_number++, "/apps/temp/temp2/temp3/temp4", 0, 10000);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == mkdirCount);
    }

    // rebuild again
    rebuildBfs(_number++, (uint32_t)protocol::BlockVersion::V3_0_VERSION,
        (uint32_t)protocol::BlockVersion::V3_1_VERSION);
}

BOOST_AUTO_TEST_CASE(rebuildBfsBySysTest)
{
    //    auto keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
    //        IGNORED_ARRAY.begin(), IGNORED_ARRAY.end());
    //    init(false, protocol::BlockVersion::V3_0_VERSION, keyPageIgnoreTables);
    init(false, protocol::BlockVersion::V3_0_VERSION);
    bcos::protocol::BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);
    }

    const int32_t mkdirCount = 1000;
    mkdir(_number++, "/apps/temp/temp2/temp3/temp4");

    boost::log::core::get()->set_logging_enabled(false);
    //    auto stateStorageFactory = storage::StateStorageFactory(10240);
    //    auto stateStorage = stateStorageFactory.createStateStorage(
    //        storage, (uint32_t)m_blockVersion, false, keyPageIgnoreTables);
    //    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> temp4p;
    //    stateStorage->asyncOpenTable(
    //        "/apps/temp/temp2/temp3/temp4", [&](Error::UniquePtr _e, std::optional<Table> _t) {
    //            temp4p.set_value({std::move(_e), std::move(_t)});
    //        });
    //    auto [error, temp4T] = temp4p.get_future().get();
    //    auto subEntry = temp4T->getRow(tool::FS_KEY_SUB);
    //    std::map<std::string, std::string> bfsInfo;
    //    auto&& out = asBytes(std::string(subEntry->get()));
    //    codec::scale::decode(bfsInfo, gsl::make_span(out));
    //    for (int i = 0; i < mkdirCount; ++i)
    //    {
    //        bfsInfo.insert({"test" + std::to_string(i), std::string(tool::FS_TYPE_DIR)});
    //    }
    //    subEntry->importFields({asString(codec::scale::encode(bfsInfo))});
    //    temp4T->setRow(tool::FS_KEY_SUB, std::move(subEntry.value()));
    //    stateStorage->parallelTraverse(
    //        false, [this](auto const& table, auto const& key, auto const& entry) -> bool {
    //            auto keyHex = boost::algorithm::hex_lower(std::string(key));
    //            std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> tempPromise;
    //            storage->asyncOpenTable(table, [&](auto&& err, auto&& table) {
    //                tempPromise.set_value(
    //                    {std::forward<decltype(err)>(err), std::forward<decltype(table)>(table)});
    //            });
    //            auto [err, myTable] = tempPromise.get_future().get();
    //            myTable->setRow(key, entry);
    //            return true;
    //        });
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> temp4p;
    storage->asyncOpenTable(
        "/apps/temp/temp2/temp3/temp4", [&](Error::UniquePtr _e, std::optional<Table> _t) {
            temp4p.set_value({std::move(_e), std::move(_t)});
        });
    auto [error, temp4T] = temp4p.get_future().get();
    auto subEntry = temp4T->getRow(tool::FS_KEY_SUB);
    std::map<std::string, std::string> bfsInfo;
    auto&& out = asBytes(std::string(subEntry->get()));
    codec::scale::decode(bfsInfo, gsl::make_span(out));
    for (int i = 0; i < mkdirCount; ++i)
    {
        bfsInfo.insert({"test" + std::to_string(i), std::string(tool::FS_TYPE_DIR)});
    }
    subEntry->importFields({asString(codec::scale::encode(bfsInfo))});
    temp4T->setRow(tool::FS_KEY_SUB, std::move(subEntry.value()));
    boost::log::core::get()->set_logging_enabled(true);

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = list(_number++, "/apps/temp/temp2/temp3/temp4");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == mkdirCount);
    }

    // upgrade to v3.1.0
    boost::log::core::get()->set_logging_enabled(false);
    auto updateNumber = _number++;
    auto versionStr = boost::lexical_cast<std::string>(bcos::protocol::BlockVersion::V3_1_VERSION);
    rebuildBfsBySysConfig(_number++, versionStr, 0, [&]() {
        for (const auto& item : tool::FS_ROOT_SUBS)
        {
            //            keyPageIgnoreTables->erase(std::string(item));
        }
    });
    boost::log::core::get()->set_logging_enabled(true);

    m_blockVersion = BlockVersion::V3_1_VERSION;
    for (const auto& item : tool::FS_ROOT_SUBS)
    {
        //        keyPageIgnoreTables->erase(std::string(item));
    }

    // ls dir
    {
        auto result = list(_number++, "/tables");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 2);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test1");
        BOOST_TEST(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 1);
        BOOST_TEST(std::get<0>(ls.at(0)) == "test2");
        BOOST_TEST(std::get<1>(ls.at(0)) == tool::FS_TYPE_LINK);
    }

    // ls /
    {
        auto result = list(_number++, "/");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == 3);
    }

    // ls /sys
    {
        auto result = list(_number++, "/sys");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        if (m_blockVersion < BlockVersion::V3_2_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 2);
        }
        else if (m_blockVersion < BlockVersion::V3_3_VERSION)
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT - 1);
        }
        else
        {
            BOOST_TEST(ls.size() == precompiled::BFS_SYS_SUBS_COUNT);
        }
    }

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = list(_number++, "/apps/temp/temp2/temp3/temp4");
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == USER_TABLE_MAX_LIMIT_COUNT);
    }

    // ls /apps/temp/temp2/temp3/temp4
    {
        auto result = listPage(_number++, "/apps/temp/temp2/temp3/temp4", 0, 10000);
        int32_t code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_TEST(code == (int)CODE_SUCCESS);
        BOOST_TEST(ls.size() == mkdirCount);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
