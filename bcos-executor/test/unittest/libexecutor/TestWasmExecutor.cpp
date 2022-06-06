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
 */
/**
 * @brief : unitest for Wasm executor implementation
 * @author: catli
 * @date: 2021-10-19
 */

#include "../liquid/hello_world.h"
#include "../liquid/hello_world_caller.h"
#include "../liquid/transfer.h"
#include "../mock/MockLedger.h"
#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "Common.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/protocol/Transaction.h"
#include "bcos-protocol/protobuf/PBBlockHeader.h"
#include "bcos-table/src/StateStorage.h"
#include "executor/TransactionExecutor.h"
#include "executor/TransactionExecutorFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/executor/NativeExecutionMessage.h>
#include <bcos-protocol/testutils/protocol/FakeBlockHeader.h>
#include <bcos-protocol/testutils/protocol/FakeTransaction.h>
#include <unistd.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
struct WasmExecutorFixture
{
    WasmExecutorFixture()
    {
        // boost::log::core::get()->set_logging_enabled(false);
        hashImpl = std::make_shared<Keccak256>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        ledger = std::make_shared<MockLedger>();
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        executor = bcos::executor::TransactionExecutorFactory::build(
            ledger, txpool, nullptr, backend, executionResultFactory, hashImpl, true, false, false);


        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        memcpy(keyPair->secretKey()->mutableData(),
            fromHexString("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e")
                ->data(),
            32);
        memcpy(keyPair->publicKey()->mutableData(),
            fromHexString(
                "ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
                "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae")
                ->data(),
            64);

        codec = std::make_unique<bcos::CodecWrapper>(hashImpl, true);

        helloWorldBin.assign(hello_world_wasm, hello_world_wasm + hello_world_wasm_len);
        helloWorldBin = codec->encode(helloWorldBin);
        helloWorldAbi = string(
            R"([{"inputs":[{"internalType":"string","name":"name","type":"string"}],"type":"constructor"},{"conflictFields":[{"kind":0,"path":[],"read_only":false,"slot":0}],"constant":false,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"set","outputs":[],"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"internalType":"string","type":"string"}],"type":"function"}])");

        helloWorldCallerBin.assign(
            hello_world_caller_wasm, hello_world_caller_wasm + hello_world_caller_wasm_len);
        helloWorldCallerBin = codec->encode(helloWorldCallerBin);
        helloWorldCallerAbi = string(
            R"([{"inputs":[{"internalType":"string","name":"addr","type":"string"}],"type":"constructor"},{"constant":false,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"set","outputs":[],"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"internalType":"string","type":"string"}],"type":"function"}])");

        transferBin.assign(transfer_wasm, transfer_wasm + transfer_wasm_len);
        transferBin = codec->encode(transferBin);
        transferAbi = string(
            R"([{"inputs":[],"type":"constructor"},{"conflictFields":[{"kind":3,"path":[0],"read_only":false,"slot":0},{"kind":3,"path":[1],"read_only":false,"slot":0}],"constant":false,"inputs":[{"internalType":"string","name":"from","type":"string"},{"internalType":"string","name":"to","type":"string"},{"internalType":"uint32","name":"amount","type":"uint32"}],"name":"transfer","outputs":[{"internalType":"bool","type":"bool"}],"type":"function"},{"constant":true,"inputs":[{"internalType":"string","name":"name","type":"string"}],"name":"query","outputs":[{"internalType":"uint32","type":"uint32"}],"type":"function"}])");
        createSysTable();
    }

    void createSysTable()
    {
        // create / table
        {
            std::promise<std::optional<Table>> promise2;
            backend->asyncCreateTable(
                "/", "value", [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                    BOOST_CHECK(!_error);
                    promise2.set_value(std::move(_table));
                });
            auto rootTable = promise2.get_future().get();
            storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
            std::map<std::string, std::string> newSubMap;
            newSubMap.insert(std::make_pair("apps", FS_TYPE_DIR));
            newSubMap.insert(std::make_pair("/", FS_TYPE_DIR));
            newSubMap.insert(std::make_pair("tables", FS_TYPE_DIR));
            tEntry.importFields({FS_TYPE_DIR});
            newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
            aclTypeEntry.importFields({"0"});
            aclWEntry.importFields({""});
            aclBEntry.importFields({""});
            extraEntry.importFields({""});
            rootTable->setRow(FS_KEY_TYPE, std::move(tEntry));
            rootTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
            rootTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
            rootTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
            rootTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
            rootTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));
        }

        // create /tables table
        {
            std::promise<std::optional<Table>> promise3;
            backend->asyncCreateTable(
                "/tables", "value", [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                    BOOST_CHECK(!_error);
                    promise3.set_value(std::move(_table));
                });
            auto tablesTable = promise3.get_future().get();
            storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
            std::map<std::string, std::string> newSubMap;
            tEntry.importFields({FS_TYPE_DIR});
            newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
            aclTypeEntry.importFields({"0"});
            aclWEntry.importFields({""});
            aclBEntry.importFields({""});
            extraEntry.importFields({""});
            tablesTable->setRow(FS_KEY_TYPE, std::move(tEntry));
            tablesTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
            tablesTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
            tablesTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
            tablesTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
            tablesTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));
        }

        // create /apps table
        {
            std::promise<std::optional<Table>> promise4;
            backend->asyncCreateTable(
                "/apps", "value", [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                    BOOST_CHECK(!_error);
                    promise4.set_value(std::move(_table));
                });
            auto appsTable = promise4.get_future().get();
            storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
            std::map<std::string, std::string> newSubMap;
            tEntry.importFields({FS_TYPE_DIR});
            newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
            aclTypeEntry.importFields({"0"});
            aclWEntry.importFields({""});
            aclBEntry.importFields({""});
            extraEntry.importFields({""});
            appsTable->setRow(FS_KEY_TYPE, std::move(tEntry));
            appsTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
            appsTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
            appsTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
            appsTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
            appsTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));
        }
    }

    TransactionExecutor::Ptr executor;
    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<MockLedger> ledger;
    std::shared_ptr<Keccak256> hashImpl;

    KeyPairInterface::Ptr keyPair;
    int64_t gas = 3000000000;
    std::unique_ptr<bcos::CodecWrapper> codec;

    bytes helloWorldBin;
    std::string helloWorldAbi;

    bytes helloWorldCallerBin;
    std::string helloWorldCallerAbi;

    bytes transferBin;
    std::string transferAbi;
};
BOOST_FIXTURE_TEST_SUITE(TestWasmExecutor, WasmExecutorFixture)

BOOST_AUTO_TEST_CASE(deployAndCall)
{
    bytes input;

    input.insert(input.end(), helloWorldBin.begin(), helloWorldBin.end());

    bytes constructorParam = codec->encode(string("alice"));
    constructorParam = codec->encode(constructorParam);
    input.insert(input.end(), constructorParam.begin(), constructorParam.end());

    string selfAddress = "usr/alice/hello_world";

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1", helloWorldAbi);
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);
    params->setTo(selfAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(ExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);
    ledger->setBlockNumber(blockHeader->number() - 1);
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();
    result->setSeq(1001);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(result),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    auto result2 = executePromise2.get_future().get();
    result2->setSeq(1000);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });

    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK_EQUAL(result3->origin(), sender);
    BOOST_CHECK_EQUAL(result3->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result3->to(), sender);

    BOOST_CHECK(result3->message().empty());
    BOOST_CHECK(!result3->newEVMContractAddress().empty());
    BOOST_CHECK_LT(result3->gasAvailable(), gas);

    auto address = result3->newEVMContractAddress();

    TwoPCParams commitParams;
    commitParams.number = 1;

    std::promise<void> preparePromise;
    executor->prepare(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        preparePromise.set_value();
    });
    preparePromise.get_future().get();

    std::promise<void> commitPromise;
    executor->commit(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        commitPromise.set_value();
    });
    commitPromise.get_future().get();
    auto tableName = std::string("/apps/") + std::string(result3->newEVMContractAddress());

    EXECUTOR_LOG(TRACE) << "Checking table: " << tableName;
    std::promise<Table> tablePromise;
    backend->asyncOpenTable(tableName, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
        BOOST_CHECK(!error);
        BOOST_CHECK(table);
        tablePromise.set_value(std::move(*table));
    });
    auto table = tablePromise.get_future().get();

    auto entry = table.getRow("code");
    BOOST_CHECK(entry);
    BOOST_CHECK_GT(entry->getField(0).size(), 0);

    // start new block
    auto blockHeader2 = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader2->setNumber(2);
    ledger->setBlockNumber(blockHeader2->number() - 1);
    std::promise<void> nextPromise2;
    executor->nextBlockHeader(0, std::move(blockHeader2), [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);

        nextPromise2.set_value();
    });

    nextPromise2.get_future().get();

    // set "fisco bcos"
    bytes txInput;
    char inputBytes[] = "4ed3885e28666973636f2062636f73";
    boost::algorithm::unhex(
        &inputBytes[0], inputBytes + sizeof(inputBytes) - 1, std::back_inserter(txInput));
    auto params2 = std::make_unique<NativeExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1000);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(address));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setData(std::move(txInput));
    params2->setType(NativeExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise4;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise4.set_value(std::move(result));
        });
    auto result4 = executePromise4.get_future().get();

    BOOST_CHECK(result4);
    BOOST_CHECK_EQUAL(result4->status(), 0);
    BOOST_CHECK_EQUAL(result4->message(), "");
    BOOST_CHECK_EQUAL(result4->newEVMContractAddress(), "");
    BOOST_CHECK_LT(result4->gasAvailable(), gas);

    // read "fisco bcos"
    bytes queryBytes;
    char inputBytes2[] = "6d4ce63c";
    boost::algorithm::unhex(
        &inputBytes2[0], inputBytes2 + sizeof(inputBytes2) - 1, std::back_inserter(queryBytes));

    auto params3 = std::make_unique<NativeExecutionMessage>();
    params3->setContextID(102);
    params3->setSeq(1000);
    params3->setDepth(0);
    params3->setFrom(std::string(sender));
    params3->setTo(std::string(address));
    params3->setOrigin(std::string(sender));
    params3->setStaticCall(true);
    params3->setGasAvailable(gas);
    params3->setData(std::move(queryBytes));
    params3->setType(ExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise5;
    executor->executeTransaction(std::move(params3),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise5.set_value(std::move(result));
        });
    auto result5 = executePromise5.get_future().get();

    BOOST_CHECK(result5);
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK_EQUAL(result5->message(), "");
    BOOST_CHECK_EQUAL(result5->newEVMContractAddress(), "");
    BOOST_CHECK_LT(result5->gasAvailable(), gas);

    std::string output;
    codec->decode(result5->data(), output);
    BOOST_CHECK_EQUAL(output, "fisco bcos");
}

BOOST_AUTO_TEST_CASE(deployError)
{
    bytes input;

    input.insert(input.end(), helloWorldBin.begin(), helloWorldBin.end());

    bytes constructorParam = codec->encode(string("alice"));
    constructorParam = codec->encode(constructorParam);
    input.insert(input.end(), constructorParam.begin(), constructorParam.end());

    string selfAddress = "usr/alice/hello_world";

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1", helloWorldAbi);
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);
    params->setTo(selfAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(ExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);
    ledger->setBlockNumber(blockHeader->number() - 1);
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();
    result->setSeq(1001);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(result),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    auto result2 = executePromise2.get_future().get();
    result2->setSeq(1000);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });

    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK_EQUAL(result3->origin(), sender);
    BOOST_CHECK_EQUAL(result3->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result3->to(), sender);

    BOOST_CHECK(result3->message().empty());
    BOOST_CHECK(!result3->newEVMContractAddress().empty());
    BOOST_CHECK_LT(result3->gasAvailable(), gas);

    TwoPCParams commitParams;
    commitParams.number = 1;

    std::promise<void> preparePromise;
    executor->prepare(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        preparePromise.set_value();
    });
    preparePromise.get_future().get();

    std::promise<void> commitPromise;
    executor->commit(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        commitPromise.set_value();
    });
    commitPromise.get_future().get();
    auto tableName = std::string("/apps/") + std::string(result3->newEVMContractAddress());

    EXECUTOR_LOG(TRACE) << "Checking table: " << tableName;
    std::promise<Table> tablePromise;
    backend->asyncOpenTable(tableName, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
        BOOST_CHECK(!error);
        BOOST_CHECK(table);
        tablePromise.set_value(std::move(*table));
    });
    auto table = tablePromise.get_future().get();

    auto entry = table.getRow("code");
    BOOST_CHECK(entry);
    BOOST_CHECK_GT(entry->getField(0).size(), 0);

    string errorAddress = "usr/alice/hello_world/hello_world";

    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setType(bcos::protocol::ExecutionMessage::TXHASH);
        params->setContextID(100);
        params->setSeq(1000);
        params->setDepth(0);
        // error address
        params->setTo(errorAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setType(ExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(2);
        ledger->setBlockNumber(blockHeader->number() - 1);
        std::promise<void> p1;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            p1.set_value();
        });
        p1.get_future().get();

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> p2;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                p2.set_value(std::move(result));
            });

        auto r2 = p2.get_future().get();
        r2->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> p3;
        executor->executeTransaction(
            std::move(r2), [&](bcos::Error::UniquePtr&& error,
                               bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                p3.set_value(std::move(result));
            });

        auto r3 = p3.get_future().get();
        r3->setSeq(1000);

        BOOST_CHECK_EQUAL(r3->status(), 15);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> p4;
        executor->executeTransaction(
            std::move(r3), [&](bcos::Error::UniquePtr&& error,
                               bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                p4.set_value(std::move(result));
            });

        auto r4 = p4.get_future().get();

        BOOST_CHECK_EQUAL(r4->status(), 16);
        BOOST_CHECK_EQUAL(r4->origin(), sender);
        BOOST_CHECK_EQUAL(r4->from(), paramsBak.to());
        BOOST_CHECK_EQUAL(r4->to(), sender);

        BOOST_CHECK(r4->message() == "Error occurs in build BFS dir");
        BOOST_CHECK_LT(r4->gasAvailable(), gas);
    }
}

BOOST_AUTO_TEST_CASE(deployAndCall_100)
{
    bytes input;

    input.insert(input.end(), helloWorldBin.begin(), helloWorldBin.end());

    bytes constructorParam = codec->encode(string("alice"));
    constructorParam = codec->encode(constructorParam);
    input.insert(input.end(), constructorParam.begin(), constructorParam.end());

    string selfAddress = "usr/alice/hello_world";

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1", helloWorldAbi);
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);
    params->setTo(selfAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(ExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);
    ledger->setBlockNumber(blockHeader->number() - 1);
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    result->setSeq(1001);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(result),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    auto result2 = executePromise2.get_future().get();
    result2->setSeq(1000);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });

    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK_EQUAL(result3->origin(), sender);
    BOOST_CHECK_EQUAL(result3->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result3->to(), sender);

    BOOST_CHECK(result3->message().empty());
    BOOST_CHECK(!result3->newEVMContractAddress().empty());
    BOOST_CHECK_EQUAL(result3->gasAvailable(), 2999552552);

    auto address = result3->newEVMContractAddress();
    BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), selfAddress);
    TwoPCParams commitParams;
    commitParams.number = 1;

    std::promise<void> preparePromise;
    executor->prepare(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        preparePromise.set_value();
    });
    preparePromise.get_future().get();

    std::promise<void> commitPromise;
    executor->commit(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        commitPromise.set_value();
    });
    commitPromise.get_future().get();
    auto tableName = std::string("/apps/") + std::string(result3->newEVMContractAddress());

    EXECUTOR_LOG(TRACE) << "Checking table: " << tableName;
    std::promise<Table> tablePromise;
    backend->asyncOpenTable(tableName, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
        BOOST_CHECK(!error);
        BOOST_CHECK(table);
        tablePromise.set_value(std::move(*table));
    });
    auto table = tablePromise.get_future().get();

    auto entry = table.getRow("code");
    BOOST_CHECK(entry);
    BOOST_CHECK_GT(entry->getField(0).size(), 0);

    // start new block
    auto blockHeader2 = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader2->setNumber(2);
    ledger->setBlockNumber(blockHeader2->number() - 1);
    std::promise<void> nextPromise2;
    executor->nextBlockHeader(0, std::move(blockHeader2), [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);

        nextPromise2.set_value();
    });

    nextPromise2.get_future().get();
    char inputBytes[] = "4ed3885e28666973636f2062636f73";

    auto helloSet = [&](size_t i) -> int64_t {
        // set "fisco bcos"
        bytes txInput;
        boost::algorithm::unhex(
            &inputBytes[0], inputBytes + sizeof(inputBytes) - 1, std::back_inserter(txInput));
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setContextID(i);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(std::string(sender));
        params2->setTo(std::string(address));
        params2->setOrigin(std::string(sender));
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(txInput));
        params2->setType(NativeExecutionMessage::MESSAGE);
        cout << ">>>>>>>>>>>>Executing set id=" << i << endl;
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->status(), 0);
        BOOST_CHECK_EQUAL(result2->message(), "");
        BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
        return result2->gasAvailable();
    };

    auto helloGet = [&](size_t i, const string& ret = string("fisco bcos")) -> int64_t {
        // read "fisco bcos"
        bytes queryBytes;

        char inputBytes2[] = "6d4ce63c";
        boost::algorithm::unhex(
            &inputBytes2[0], inputBytes2 + sizeof(inputBytes2) - 1, std::back_inserter(queryBytes));

        auto params3 = std::make_unique<NativeExecutionMessage>();
        params3->setContextID(i);
        params3->setSeq(1000);
        params3->setDepth(0);
        params3->setFrom(std::string(sender));
        params3->setTo(std::string(address));
        params3->setOrigin(std::string(sender));
        params3->setStaticCall(false);
        params3->setGasAvailable(gas);
        params3->setData(std::move(queryBytes));
        params3->setType(ExecutionMessage::MESSAGE);
        cout << ">>>>>>>>>>>>Executing get id=" << i << endl;
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(params3),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK(result3);
        BOOST_CHECK_EQUAL(result3->status(), 0);
        BOOST_CHECK_EQUAL(result3->message(), "");
        BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), "");
        std::string output;
        codec->decode(result3->data(), output);
        BOOST_CHECK_EQUAL(output, "fisco bcos");
        return result3->gasAvailable();
    };
    int64_t getGas = 2999989808;
    int64_t setGas = 2999982821;
    size_t id = 101;
    BOOST_CHECK_EQUAL(helloSet(id++), 2999983105);
    BOOST_CHECK_EQUAL(helloSet(id++), setGas);
    BOOST_CHECK_EQUAL(helloGet(id++), getGas);
    BOOST_CHECK_EQUAL(helloSet(id++), setGas);
    BOOST_CHECK_EQUAL(helloSet(id++), setGas);

    for (size_t i = 899; i < 1001; ++i)
    {
        if (i % 3 == 0)
        {
            BOOST_CHECK_EQUAL(helloSet(i), setGas);
        }
        else if (i % 3 == 1)
        {
            BOOST_CHECK_EQUAL(helloGet(i), getGas);
        }
        else
        {
            BOOST_CHECK_EQUAL(helloSet(i), setGas);
        }
    }
    BOOST_CHECK_EQUAL(helloGet(id++), getGas);
}

BOOST_AUTO_TEST_CASE(externalCall)
{
    string aliceAddress = "usr/alice/hello_world";
    string bobAddress = "/usr/bob/hello_world_caller";
    string sender;
    // --------------------------------
    // Create contract HelloWorld
    // --------------------------------
    {
        bytes input;

        input.insert(input.end(), helloWorldBin.begin(), helloWorldBin.end());

        bytes constructorParam = codec->encode(string("alice"));
        constructorParam = codec->encode(constructorParam);
        input.insert(input.end(), constructorParam.begin(), constructorParam.end());

        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1", helloWorldAbi);
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(100);
        params->setSeq(1000);
        params->setDepth(0);
        params->setOrigin(std::string(sender));
        params->setFrom(std::string(sender));
        params->setTo(aliceAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(1);
        ledger->setBlockNumber(blockHeader->number() - 1);
        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        result->setSeq(1001);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1000);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        auto address = result3->newEVMContractAddress();
        BOOST_CHECK_EQUAL(result3->type(), NativeExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result3->status(), 0);
        BOOST_CHECK_EQUAL(address, aliceAddress);
    }

    // --------------------------------
    // Create contract HelloWorldCaller
    // --------------------------------
    {
        bytes input;

        input.insert(input.end(), helloWorldCallerBin.begin(), helloWorldCallerBin.end());

        bytes constructorParam = codec->encode(aliceAddress);
        constructorParam = codec->encode(constructorParam);
        input.insert(input.end(), constructorParam.begin(), constructorParam.end());

        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", input, 102, 100001, "1", "1", helloWorldCallerAbi);
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(200);
        params->setSeq(1002);
        params->setDepth(0);
        params->setOrigin(std::string(sender));
        params->setFrom(std::string(sender));
        params->setTo(bobAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(2);
        ledger->setBlockNumber(blockHeader->number() - 1);
        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        result->setSeq(1003);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();
        result2->setSeq(1002);

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        auto address = result3->newEVMContractAddress();
        BOOST_CHECK_EQUAL(result3->type(), NativeExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result3->status(), 0);
        BOOST_CHECK_EQUAL(address, bobAddress);
    }

    // --------------------------------
    // HelloWorldCaller calls `set` of HelloWorld
    // --------------------------------
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(300);
        params->setSeq(1003);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(bobAddress));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);

        bytes data;
        auto encodedParams = codec->encodeWithSig("set(string)", string("fisco bcos"));
        data.insert(data.end(), encodedParams.begin(), encodedParams.end());

        params->setData(data);
        params->setType(NativeExecutionMessage::MESSAGE);

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(3);
        ledger->setBlockNumber(blockHeader->number() - 1);
        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params),
            [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result->data().size(), 15);
        BOOST_CHECK_EQUAL(result->contextID(), 300);
        BOOST_CHECK_EQUAL(result->seq(), 1003);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), std::string(sender));
        BOOST_CHECK_EQUAL(result->from(), std::string(bobAddress));
        BOOST_CHECK_EQUAL(result->to(), aliceAddress);
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        BOOST_CHECK_GT(result->keyLocks().size(), 0);

        result->setSeq(1004);

        // clear the keylock
        result->setKeyLocks({});

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result2->data().size(), 0);
        BOOST_CHECK_EQUAL(result2->contextID(), 300);
        BOOST_CHECK_EQUAL(result2->seq(), 1004);
        BOOST_CHECK_EQUAL(result2->origin(), std::string(sender));
        BOOST_CHECK_EQUAL(result2->from(), aliceAddress);
        BOOST_CHECK_EQUAL(result2->to(), bobAddress);
        BOOST_CHECK_EQUAL(result2->create(), false);
        BOOST_CHECK_EQUAL(result2->status(), 0);
    }
}

BOOST_AUTO_TEST_CASE(performance)
{
    size_t count = 10 * 1000;

    bytes input;
    input.insert(input.end(), transferBin.begin(), transferBin.end());

    input.push_back(0);

    string transferAddress = "usr/alice/transfer";

    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1", transferAbi);
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(99);
    params->setSeq(1000);
    params->setDepth(0);
    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));
    params->setTo(transferAddress);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);
    ledger->setBlockNumber(blockHeader->number() - 1);
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Create contract transfer
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    result->setSeq(1001);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(result),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    auto result2 = executePromise2.get_future().get();
    result2->setSeq(1000);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });

    auto result3 = executePromise3.get_future().get();

    auto address = result3->newEVMContractAddress();

    std::vector<ExecutionMessage::UniquePtr> requests;
    requests.reserve(count);
    // Transfer
    for (size_t i = 0; i < count; ++i)
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(i);
        params->setSeq(6000);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(address));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setCreate(false);

        std::string from = "alice";
        std::string to = "bob";
        uint32_t amount = 1;
        bytes data;
        auto encodedParams =
            codec->encodeWithSig("transfer(string,string,uint32)", from, to, amount);
        data.insert(data.end(), encodedParams.begin(), encodedParams.end());
        params->setData(data);
        params->setType(NativeExecutionMessage::MESSAGE);

        requests.emplace_back(std::move(params));
    }

    auto now = std::chrono::system_clock::now();

    for (auto& it : requests)
    {
        std::promise<std::optional<ExecutionMessage::UniquePtr>> outputPromise;
        executor->executeTransaction(
            std::move(it), [&outputPromise](bcos::Error::UniquePtr&& error,
                               NativeExecutionMessage::UniquePtr&& result) {
                if (error)
                {
                    std::cout << "Error!" << boost::diagnostic_information(*error);
                }
                // BOOST_CHECK(!error);
                outputPromise.set_value(std::move(result));
            });
        ExecutionMessage::UniquePtr result4 = std::move(*outputPromise.get_future().get());
        if (result4->status() != 0)
        {
            std::cout << "Error: " << result->status() << std::endl;
        }
    }

    std::cout << "Execute elapsed: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now() - now)
                     .count()
              << std::endl;

    {
        bytes queryBytes;

        auto encodedParams = codec->encodeWithSig("query(string)", string("alice"));
        queryBytes.insert(queryBytes.end(), encodedParams.begin(), encodedParams.end());

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(102);
        params->setSeq(1000);
        params->setDepth(0);
        params->setFrom(std::string(sender));
        params->setTo(std::string(address));
        params->setOrigin(std::string(sender));
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(std::move(queryBytes));
        params->setType(ExecutionMessage::MESSAGE);

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result5 = executePromise.get_future().get();

        BOOST_CHECK(result5);
        BOOST_CHECK_EQUAL(result5->status(), 0);
        BOOST_CHECK_EQUAL(result5->message(), "");
        BOOST_CHECK_EQUAL(result5->newEVMContractAddress(), "");
        BOOST_CHECK_LT(result5->gasAvailable(), gas);

        uint32_t dept;
        codec->decode(result5->data(), dept);
        BOOST_CHECK_EQUAL(dept, numeric_limits<uint32_t>::max() - count);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
