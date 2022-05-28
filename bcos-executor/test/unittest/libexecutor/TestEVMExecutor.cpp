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
 * @brief : unitest for executor implement
 * @author: ancelmo
 * @date: 2021-09-14
 */

#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "Common.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-framework//executor/ExecutionMessage.h"
#include "bcos-framework//protocol/Transaction.h"
#include "bcos-protocol/protobuf/PBBlockHeader.h"
#include "bcos-table/src/StateStorage.h"
#include "executor/TransactionExecutorFactory.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework//executor/NativeExecutionMessage.h>
#include <bcos-protocol/testutils/protocol/FakeBlockHeader.h>
#include <bcos-protocol/testutils/protocol/FakeTransaction.h>
#include <tbb/task_group.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/latch.hpp>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
struct TransactionExecutorFixture
{
    TransactionExecutorFixture()
    {
        // boost::log::core::get()->set_logging_enabled(false);
        hashImpl = std::make_shared<Keccak256>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(backend);

        executor = bcos::executor::TransactionExecutorFactory::build(
            txpool, lruStorage, backend, executionResultFactory, hashImpl, false, false);

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

        codec = std::make_unique<bcos::CodecWrapper>(hashImpl, false);
    }

    TransactionExecutor::Ptr executor;
    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<Keccak256> hashImpl;

    KeyPairInterface::Ptr keyPair;
    int64_t gas = 3000000;
    std::unique_ptr<bcos::CodecWrapper> codec;

    string helloBin =
        "60806040526040805190810160405280600181526020017f3100000000000000000000000000000000000000"
        "0000000000000000000000008152506001908051906020019061004f9291906100ae565b5034801561005c5760"
        "0080fd5b506040805190810160405280600d81526020017f48656c6c6f2c20576f726c64210000000000000000"
        "0000000000000000000000815250600090805190602001906100a89291906100ae565b50610153565b82805460"
        "0181600116156101000203166002900490600052602060002090601f016020900481019282601f106100ef5780"
        "5160ff191683800117855561011d565b8280016001018555821561011d579182015b8281111561011c57825182"
        "5591602001919060010190610101565b5b50905061012a919061012e565b5090565b61015091905b8082111561"
        "014c576000816000905550600101610134565b5090565b90565b6104ac806101626000396000f3006080604052"
        "60043610610057576000357c0100000000000000000000000000000000000000000000000000000000900463ff"
        "ffffff1680634ed3885e1461005c57806354fd4d50146100c55780636d4ce63c14610155575b600080fd5b3480"
        "1561006857600080fd5b506100c3600480360381019080803590602001908201803590602001908080601f0160"
        "208091040260200160405190810160405280939291908181526020018383808284378201915050505050509192"
        "9192905050506101e5565b005b3480156100d157600080fd5b506100da61029b565b6040518080602001828103"
        "825283818151815260200191508051906020019080838360005b8381101561011a578082015181840152602081"
        "0190506100ff565b50505050905090810190601f1680156101475780820380516001836020036101000a031916"
        "815260200191505b509250505060405180910390f35b34801561016157600080fd5b5061016a610339565b6040"
        "518080602001828103825283818151815260200191508051906020019080838360005b838110156101aa578082"
        "01518184015260208101905061018f565b50505050905090810190601f1680156101d757808203805160018360"
        "20036101000a031916815260200191505b509250505060405180910390f35b80600090805190602001906101fb"
        "9291906103db565b507f93a093529f9c8a0c300db4c55fcd27c068c4f5e0e8410bc288c7e76f3d71083e816040"
        "518080602001828103825283818151815260200191508051906020019080838360005b8381101561025e578082"
        "015181840152602081019050610243565b50505050905090810190601f16801561028b57808203805160018360"
        "20036101000a031916815260200191505b509250505060405180910390a150565b600180546001816001161561"
        "01000203166002900480601f016020809104026020016040519081016040528092919081815260200182805460"
        "0181600116156101000203166002900480156103315780601f1061030657610100808354040283529160200191"
        "610331565b820191906000526020600020905b81548152906001019060200180831161031457829003601f1682"
        "01915b505050505081565b606060008054600181600116156101000203166002900480601f0160208091040260"
        "200160405190810160405280929190818152602001828054600181600116156101000203166002900480156103"
        "d15780601f106103a6576101008083540402835291602001916103d1565b820191906000526020600020905b81"
        "54815290600101906020018083116103b457829003601f168201915b5050505050905090565b82805460018160"
        "0116156101000203166002900490600052602060002090601f016020900481019282601f1061041c57805160ff"
        "191683800117855561044a565b8280016001018555821561044a579182015b8281111561044957825182559160"
        "200191906001019061042e565b5b509050610457919061045b565b5090565b61047d91905b8082111561047957"
        "6000816000905550600101610461565b5090565b905600a165627a7a723058204736027ad6b97d7cd2685379ac"
        "b35b386dcb18799934be8283f1e08cd1f0c6ec0029";
};
BOOST_FIXTURE_TEST_SUITE(TestTransactionExecutor, TransactionExecutorFixture)

BOOST_AUTO_TEST_CASE(deployAndCall)
{
    auto helloworld = string(helloBin);

    bytes input;
    boost::algorithm::unhex(helloworld, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setType(bcos::protocol::ExecutionMessage::TXHASH);
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    // params->setData(input);
    params->setType(ExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
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
    BOOST_CHECK_EQUAL(result->status(), 0);

    BOOST_CHECK_EQUAL(result->origin(), sender);
    BOOST_CHECK_EQUAL(result->from(), paramsBak.to());
    BOOST_CHECK_EQUAL(result->to(), sender);

    BOOST_CHECK(result->message().empty());
    BOOST_CHECK(!result->newEVMContractAddress().empty());
    BOOST_CHECK_LT(result->gasAvailable(), gas);

    auto address = result->newEVMContractAddress();

    bcos::protocol::TwoPCParams commitParams{};
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
    auto tableName = std::string("/apps/") +
                     std::string(result->newEVMContractAddress());  // TODO: ensure the contract
                                                                    // address is hex

    // test getCode()
    executor->getCode(address, [](Error::Ptr error, bcos::bytes code) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!code.empty());
        BOOST_CHECK_GT(code.size(), 0);
    });

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

    std::promise<void> nextPromise2;
    executor->nextBlockHeader(std::move(blockHeader2), [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);

        nextPromise2.set_value();
    });

    nextPromise2.get_future().get();

    // set "fisco bcos"
    bytes txInput;
    char inputBytes[] =
        "4ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000"
        "0000000000000000000000000000000000000000000005666973636f0000000000000000000000000000000000"
        "00000000000000000000";
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
    BOOST_CHECK_LT(result2->gasAvailable(), gas);

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
    params3->setStaticCall(false);
    params3->setGasAvailable(gas);
    params3->setData(std::move(queryBytes));
    params3->setType(ExecutionMessage::MESSAGE);

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
    BOOST_CHECK_LT(result3->gasAvailable(), gas);

    std::string output;
    boost::algorithm::hex_lower(
        result3->data().begin(), result3->data().end(), std::back_inserter(output));
    BOOST_CHECK_EQUAL(output,
        "00000000000000000000000000000000000000000000000000000000000000200000000000000000000"
        "000000000000000000000000000000000000000000005666973636f0000000000000000000000000000"
        "00000000000000000000000000");
}

BOOST_AUTO_TEST_CASE(externalCall)
{
    // Solidity source code from test_external_call.sol, using remix
    // 0.6.10+commit.00c0fcaf

    std::string ABin =
        "608060405234801561001057600080fd5b5061037f806100206000396000f3fe60806040523480156100105760"
        "0080fd5b506004361061002b5760003560e01c80635b975a7314610030575b600080fd5b61005c600480360360"
        "2081101561004657600080fd5b8101908080359060200190929190505050610072565b60405180828152602001"
        "91505060405180910390f35b600081604051610081906101c7565b808281526020019150506040518091039060"
        "00f0801580156100a7573d6000803e3d6000fd5b506000806101000a81548173ffffffffffffffffffffffffff"
        "ffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055507fd8e189e965"
        "f1ff506594c5c65110ea4132cee975b58710da78ea19bc094414ae826040518082815260200191505060405180"
        "910390a16000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffff"
        "ffffffffffffffffffffffffffff16633fa4f2456040518163ffffffff1660e01b815260040160206040518083"
        "038186803b15801561018557600080fd5b505afa158015610199573d6000803e3d6000fd5b505050506040513d"
        "60208110156101af57600080fd5b81019080805190602001909291905050509050919050565b610175806101d5"
        "8339019056fe608060405234801561001057600080fd5b50604051610175380380610175833981810160405260"
        "2081101561003357600080fd5b8101908080519060200190929190505050806000819055507fdc509bfccbee28"
        "6f248e0904323788ad0c0e04e04de65c04b482b056acb1a0658160405180828152602001915050604051809103"
        "90a15060e4806100916000396000f3fe6080604052348015600f57600080fd5b506004361060325760003560e0"
        "1c80633fa4f245146037578063a16fe09b146053575b600080fd5b603d605b565b604051808281526020019150"
        "5060405180910390f35b60596064565b005b60008054905090565b6000808154600101919050819055507f052f"
        "6b9dfac9e4e1257cb5b806b7673421c54730f663c8ab02561743bb23622d600054604051808281526020019150"
        "5060405180910390a156fea264697066735822122006eea3bbe24f3d859a9cb90efc318f26898aeb4dffb31cac"
        "e105776a6c272f8464736f6c634300060a0033a2646970667358221220b441da8ba792a40e444d0ed767a4417e"
        "944c55578d1c8d0ca4ad4ec050e05a9364736f6c634300060a0033";

    std::string BBin =
        "608060405234801561001057600080fd5b50604051610175380380610175833981810160405260208110156100"
        "3357600080fd5b8101908080519060200190929190505050806000819055507fdc509bfccbee286f248e090432"
        "3788ad0c0e04e04de65c04b482b056acb1a065816040518082815260200191505060405180910390a15060e480"
        "6100916000396000f3fe6080604052348015600f57600080fd5b506004361060325760003560e01c80633fa4f2"
        "45146037578063a16fe09b146053575b600080fd5b603d605b565b604051808281526020019150506040518091"
        "0390f35b60596064565b005b60008054905090565b6000808154600101919050819055507f052f6b9dfac9e4e1"
        "257cb5b806b7673421c54730f663c8ab02561743bb23622d600054604051808281526020019150506040518091"
        "0390a156fea264697066735822122006eea3bbe24f3d859a9cb90efc318f26898aeb4dffb31cace105776a6c27"
        "2f8464736f6c634300060a0033";

    bytes input;
    boost::algorithm::unhex(ABin, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string addressString = addressCreate.hex().substr(0, 40);
    params->setTo(std::move(addressString));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Create contract A
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    auto address = result->newEVMContractAddress();
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_GT(address.size(), 0);
    BOOST_CHECK(result->keyLocks().empty());

    // --------------------------------
    // Call A createAndCallB(int256)
    // --------------------------------
    auto params2 = std::make_unique<NativeExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1001);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(address));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setCreate(false);

    bcos::u256 value(1000);
    params2->setData(codec->encodeWithSig("createAndCallB(int256)", value));
    params2->setType(NativeExecutionMessage::MESSAGE);

    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();

    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK(result2->data().size() > 0);
    BOOST_CHECK_EQUAL(result2->contextID(), 101);
    BOOST_CHECK_EQUAL(result2->seq(), 1001);
    BOOST_CHECK_EQUAL(result2->create(), true);
    BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
    BOOST_CHECK_EQUAL(result2->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result2->from(), std::string(address));
    BOOST_CHECK(result2->to().empty());
    BOOST_CHECK_LT(result2->gasAvailable(), gas);
    BOOST_CHECK_EQUAL(result2->keyLocks().size(), 1);
    BOOST_CHECK_EQUAL(result2->keyLocks()[0], "code");

    // --------------------------------
    // Message 1: Create contract B, set new seq 1002
    // A -> B
    // --------------------------------
    result2->setSeq(1002);

    // Clear the key lock to avoid effect
    result2->setKeyLocks({});

    h256 addressCreate2(
        "ee6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");  // ee6f30856ad3bae00b1169808488502786a13e3c
    std::string addressString2 = addressCreate2.hex().substr(0, 40);
    result2->setTo(addressString2);

    std::promise<ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });
    auto result3 = executePromise3.get_future().get();
    BOOST_CHECK(result3);
    BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result3->data().size(), 0);
    BOOST_CHECK_EQUAL(result3->contextID(), 101);
    BOOST_CHECK_EQUAL(result3->seq(), 1002);
    BOOST_CHECK_EQUAL(result3->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result3->from(), addressString2);
    BOOST_CHECK_EQUAL(result3->to(), std::string(address));
    BOOST_CHECK_EQUAL(result3->newEVMContractAddress(), addressString2);
    BOOST_CHECK_EQUAL(result3->create(), false);
    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK(result3->logEntries().size() == 0);
    BOOST_CHECK(result3->keyLocks().empty());

    // --------------------------------
    // Message 2: Create contract B success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result3->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise4;
    executor->executeTransaction(std::move(result3),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise4.set_value(std::move(result));
        });
    auto result4 = executePromise4.get_future().get();

    BOOST_CHECK(result4);
    BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK_GT(result4->data().size(), 0);
    auto param = codec->encodeWithSig("value()");
    BOOST_CHECK(result4->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result4->contextID(), 101);
    BOOST_CHECK_EQUAL(result4->seq(), 1001);
    BOOST_CHECK_EQUAL(result4->from(), std::string(address));
    BOOST_CHECK_EQUAL(result4->to(), boost::algorithm::to_lower_copy(std::string(addressString2)));
    BOOST_CHECK_EQUAL(result4->keyLocks().size(), 1);
    BOOST_CHECK_EQUAL(toHex(result4->keyLocks()[0]), h256(0).hex());  // first member

    // Request message without status
    // BOOST_CHECK_EQUAL(result4->status(), 0);
    BOOST_CHECK(result4->message().empty());
    BOOST_CHECK(result4->newEVMContractAddress().empty());
    BOOST_CHECK_GT(result4->keyLocks().size(), 0);

    // --------------------------------
    // Message 3: A call B's value(), set new seq 1003
    // A -> B
    // --------------------------------
    result4->setSeq(1003);
    // Clear the keylock
    result4->setKeyLocks({});

    std::promise<ExecutionMessage::UniquePtr> executePromise5;
    executor->executeTransaction(std::move(result4),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise5.set_value(std::move(result));
        });
    auto result5 = executePromise5.get_future().get();

    BOOST_CHECK(result5);
    BOOST_CHECK_EQUAL(result5->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result5->data().size(), 0);
    param = codec->encode(s256(1000));
    BOOST_CHECK(result5->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result5->contextID(), 101);
    BOOST_CHECK_EQUAL(result5->seq(), 1003);
    BOOST_CHECK_EQUAL(
        result5->from(), boost::algorithm::to_lower_copy(std::string(addressString2)));
    BOOST_CHECK_EQUAL(result5->to(), std::string(address));
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK(result5->message().empty());
    BOOST_CHECK_EQUAL(result5->keyLocks().size(), 0);

    // --------------------------------
    // Message 4: A call B's success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result5->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise6;
    executor->executeTransaction(std::move(result5),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise6.set_value(std::move(result));
        });
    auto result6 = executePromise6.get_future().get();
    BOOST_CHECK(result6);
    BOOST_CHECK_EQUAL(result6->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result6->data().size(), 0);
    BOOST_CHECK(result6->data().toBytes() == param);
    BOOST_CHECK_EQUAL(result6->contextID(), 101);
    BOOST_CHECK_EQUAL(result6->seq(), 1001);
    BOOST_CHECK_EQUAL(result6->from(), std::string(address));
    BOOST_CHECK_EQUAL(result6->to(), std::string(sender));
    BOOST_CHECK_EQUAL(result6->origin(), std::string(sender));
    BOOST_CHECK_EQUAL(result6->status(), 0);
    BOOST_CHECK(result6->message().empty());
    BOOST_CHECK(result6->logEntries().size() == 1);
    BOOST_CHECK_EQUAL(result6->keyLocks().size(), 0);

    executor->getHash(1, [&](bcos::Error::UniquePtr&& error, crypto::HashType&& hash) {
        BOOST_CHECK(!error);
        BOOST_CHECK_NE(hash.hex(), h256().hex());
    });

    // commit the state
    TwoPCParams commitParams;
    commitParams.number = 1;

    executor->prepare(commitParams, [](bcos::Error::Ptr error) { BOOST_CHECK(!error); });
    executor->commit(commitParams, [](bcos::Error::Ptr error) { BOOST_CHECK(!error); });

    // execute a call request
    auto callParam = std::make_unique<NativeExecutionMessage>();
    callParam->setType(executor::NativeExecutionMessage::MESSAGE);
    callParam->setContextID(500);
    callParam->setSeq(7778);
    callParam->setDepth(0);
    callParam->setFrom(std::string(sender));
    callParam->setTo(boost::algorithm::to_lower_copy(std::string(addressString2)));
    callParam->setData(codec->encodeWithSig("value()"));
    callParam->setOrigin(std::string(sender));
    callParam->setStaticCall(true);
    callParam->setGasAvailable(gas);
    callParam->setCreate(false);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> callResultPromise;
    executor->call(std::move(callParam),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResultPromise.set_value(std::move(response));
        });

    bcos::protocol::ExecutionMessage::UniquePtr callResult = callResultPromise.get_future().get();


    BOOST_CHECK_EQUAL(callResult->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult->status(), 0);

    auto expectResult = codec->encode(s256(1000));
    BOOST_CHECK(callResult->data().toBytes() == expectResult);

    // commit the state, and call
    // bcos::protocol::TwoPCParams commitParams;
    // commitParams.number = 1;
    // executor->prepare(commitParams, [&](bcos::Error::Ptr error) { BOOST_CHECK(!error); });
    // executor->commit(commitParams, [&](bcos::Error::Ptr error) { BOOST_CHECK(!error); });

    auto callParam2 = std::make_unique<NativeExecutionMessage>();
    callParam2->setType(executor::NativeExecutionMessage::MESSAGE);
    callParam2->setContextID(501);
    callParam2->setSeq(7779);
    callParam2->setDepth(0);
    callParam2->setFrom(std::string(sender));
    callParam2->setTo(boost::algorithm::to_lower_copy(std::string(addressString2)));
    callParam2->setData(codec->encodeWithSig("value()"));
    callParam2->setOrigin(std::string(sender));
    callParam2->setStaticCall(true);
    callParam2->setGasAvailable(gas);
    callParam2->setCreate(false);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> callResult2Promise;
    executor->call(std::move(callParam2),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResult2Promise.set_value(std::move(response));
        });

    bcos::protocol::ExecutionMessage::UniquePtr callResult2 = callResult2Promise.get_future().get();


    BOOST_CHECK_EQUAL(callResult2->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult2->status(), 0);

    auto expectResult2 = codec->encode(s256(1000));
    BOOST_CHECK(callResult2->data().toBytes() == expectResult);
}

BOOST_AUTO_TEST_CASE(performance)
{
    size_t count = 10 * 100;

    bcos::crypto::HashType hash;
    for (size_t blockNumber = 1; blockNumber < 10; ++blockNumber)
    {
        std::string bin =
            "608060405234801561001057600080fd5b506105db806100206000396000f3006080604052600436106100"
            "6257"
            "6000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff168063"
            "35ee"
            "5f87146100675780638a42ebe9146100e45780639b80b05014610157578063fad42f8714610210575b6000"
            "80fd"
            "5b34801561007357600080fd5b506100ce6004803603810190808035906020019082018035906020019080"
            "8060"
            "1f016020809104026020016040519081016040528093929190818152602001838380828437820191505050"
            "5050"
            "5091929192905050506102c9565b6040518082815260200191505060405180910390f35b3480156100f057"
            "6000"
            "80fd5b50610155600480360381019080803590602001908201803590602001908080601f01602080910402"
            "6020"
            "01604051908101604052809392919081815260200183838082843782019150505050505091929192908035"
            "9060"
            "20019092919050505061033d565b005b34801561016357600080fd5b5061020e6004803603810190808035"
            "9060"
            "2001908201803590602001908080601f016020809104026020016040519081016040528093929190818152"
            "6020"
            "018383808284378201915050505050509192919290803590602001908201803590602001908080601f0160"
            "2080"
            "91040260200160405190810160405280939291908181526020018383808284378201915050505050509192"
            "9192"
            "90803590602001909291905050506103b1565b005b34801561021c57600080fd5b506102c7600480360381"
            "0190"
            "80803590602001908201803590602001908080601f01602080910402602001604051908101604052809392"
            "9190"
            "81815260200183838082843782019150505050505091929192908035906020019082018035906020019080"
            "8060"
            "1f016020809104026020016040519081016040528093929190818152602001838380828437820191505050"
            "5050"
            "509192919290803590602001909291905050506104a8565b005b6000808260405180828051906020019080"
            "8383"
            "5b60208310151561030257805182526020820191506020810190506020830392506102dd565b6001836020"
            "0361"
            "01000a03801982511681845116808217855250505050505090500191505090815260200160405180910390"
            "2054"
            "9050919050565b806000836040518082805190602001908083835b60208310151561037657805182526020"
            "8201"
            "9150602081019050602083039250610351565b6001836020036101000a0380198251168184511680821785"
            "5250"
            "50505050509050019150509081526020016040518091039020819055505050565b80600084604051808280"
            "5190"
            "602001908083835b6020831015156103ea57805182526020820191506020810190506020830392506103c5"
            "565b"
            "6001836020036101000a038019825116818451168082178552505050505050905001915050908152602001"
            "6040"
            "51809103902060008282540392505081905550806000836040518082805190602001908083835b60208310"
            "1515"
            "610463578051825260208201915060208101905060208303925061043e565b6001836020036101000a0380"
            "1982"
            "51168184511680821785525050505050509050019150509081526020016040518091039020600082825401"
            "9250"
            "5081905550505050565b806000846040518082805190602001908083835b6020831015156104e157805182"
            "5260"
            "20820191506020810190506020830392506104bc565b6001836020036101000a0380198251168184511680"
            "8217"
            "85525050505050509050019150509081526020016040518091039020600082825403925050819055508060"
            "0083"
            "6040518082805190602001908083835b60208310151561055a578051825260208201915060208101905060"
            "2083"
            "039250610535565b6001836020036101000a03801982511681845116808217855250505050505090500191"
            "5050"
            "908152602001604051809103902060008282540192505081905550606481111515156105aa57600080fd5b"
            "5050"
            "505600a165627a7a723058205669c1a68cebcef35822edcec77a15792da5c32a8aa127803290253b3d5f62"
            "7200"
            "29";

        bytes input;
        boost::algorithm::unhex(bin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(std::string(sender));
        params->setFrom(std::string(sender));

        // The contract address
        std::string addressSeed = "address" + boost::lexical_cast<std::string>(blockNumber);
        h256 addressCreate(hashImpl->hash(addressSeed));
        // h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        std::string addressString = addressCreate.hex().substr(0, 40);
        // toChecksumAddress(addressString, hashImpl);
        params->setTo(std::move(addressString));

        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(blockNumber);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        // --------------------------------
        // Create contract ParallelOk
        // --------------------------------
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();

        auto address = result->newEVMContractAddress();

        // Set user
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(5000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string user = "user" + boost::lexical_cast<std::string>(i);
            bcos::u256 value(1000000);
            params->setData(codec->encodeWithSig("set(string,uint256)", user, value));
            params->setType(NativeExecutionMessage::MESSAGE);

            std::promise<ExecutionMessage::UniquePtr> executePromise2;
            executor->executeTransaction(std::move(params),
                [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
                    if (error)
                    {
                        std::cout << "Error!" << boost::diagnostic_information(*error);
                    }
                    executePromise2.set_value(std::move(result));
                });
            auto result2 = executePromise2.get_future().get();
            // BOOST_CHECK_EQUAL(result->status(), 0);
        }

        std::vector<ExecutionMessage::UniquePtr> requests;
        requests.reserve(count);
        // Transfer
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(6000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string from = "user" + boost::lexical_cast<std::string>(i);
            std::string to = "user" + boost::lexical_cast<std::string>(count - 1);
            bcos::u256 value(10);
            params->setData(
                codec->encodeWithSig("transfer(string,string,uint256)", from, to, value));
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
            ExecutionMessage::UniquePtr transResult = std::move(*outputPromise.get_future().get());
            if (transResult->status() != 0)
            {
                std::cout << "Error: " << transResult->status() << std::endl;
            }
        }

        std::cout << "Execute elapsed: "
                  << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

        now = std::chrono::system_clock::now();
        // Check the result
        for (size_t i = 0; i < count; ++i)
        {
            params = std::make_unique<NativeExecutionMessage>();
            params->setContextID(i);
            params->setSeq(7000);
            params->setDepth(0);
            params->setFrom(std::string(sender));
            params->setTo(std::string(address));
            params->setOrigin(std::string(sender));
            params->setStaticCall(false);
            params->setGasAvailable(gas);
            params->setCreate(false);

            std::string account = "user" + boost::lexical_cast<std::string>(i);
            params->setData(codec->encodeWithSig("balanceOf(string)", account));
            params->setType(NativeExecutionMessage::MESSAGE);

            std::promise<std::optional<ExecutionMessage::UniquePtr>> outputPromise;
            executor->executeTransaction(
                std::move(params), [&outputPromise](bcos::Error::UniquePtr&& error,
                                       NativeExecutionMessage::UniquePtr&& result) {
                    if (error)
                    {
                        std::cout << "Error!" << boost::diagnostic_information(*error);
                    }
                    // BOOST_CHECK(!error);
                    outputPromise.set_value(std::move(result));
                });


            ExecutionMessage::UniquePtr balanceResult =
                std::move(*outputPromise.get_future().get());

            bcos::u256 value(0);
            codec->decode(balanceResult->data(), value);

            if (i < count - 1)
            {
                BOOST_CHECK_EQUAL(value, u256(1000000 - 10));
            }
            else
            {
                BOOST_CHECK_EQUAL(value, u256(1000000 + 10 * (count - 1)));
            }
        }

        std::cout << "Check elapsed: "
                  << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

        executor->getHash(
            blockNumber, [&hash](bcos::Error::UniquePtr error, crypto::HashType resultHash) {
                BOOST_CHECK(!error);
                BOOST_CHECK_NE(resultHash, h256());

                if (hash == h256())
                {
                    hash = resultHash;
                }
                else
                {
                    hash = resultHash;
                }
            });
    }
}

BOOST_AUTO_TEST_CASE(multiDeploy)
{
    tbb::task_group group;

    size_t count = 100;
    std::vector<NativeExecutionMessage::UniquePtr> paramsList;

    for (size_t i = 0; i < count; ++i)
    {
        auto helloworld = string(helloBin);
        bytes input;
        boost::algorithm::unhex(helloworld, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 100 + i, 100001, "1", "1");

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setType(bcos::protocol::ExecutionMessage::TXHASH);
        params->setContextID(100 + i);
        params->setSeq(1000);
        params->setDepth(0);
        auto sender = *toHexString(string_view((char*)tx->sender().data(), tx->sender().size()));

        auto addressCreate =
            cryptoSuite->hashImpl()->hash("i am a address" + boost::lexical_cast<std::string>(i));
        std::string addressString = addressCreate.hex().substr(0, 40);
        params->setTo(std::move(addressString));

        params->setStaticCall(false);
        params->setGasAvailable(gas);
        // params->setData(input);
        params->setType(ExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        paramsList.emplace_back(std::move(params));
    }

    auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
    blockHeader->setNumber(1);

    std::promise<void> nextPromise;
    executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    boost::latch latch(paramsList.size());

    std::vector<std::tuple<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>>
        responses(count);
    for (size_t i = 0; i < paramsList.size(); ++i)
    {
        group.run([&responses, executor = executor, &paramsList, index = i, &latch]() {
            executor->executeTransaction(std::move(std::move(paramsList[index])),
                [&](bcos::Error::UniquePtr error,
                    bcos::protocol::ExecutionMessage::UniquePtr result) {
                    responses[index] = std::make_tuple(std::move(error), std::move(result));
                    latch.count_down();
                });
        });
    }

    latch.wait();
    group.wait();

    for (auto& it : responses)
    {
        auto& [error, result] = it;

        BOOST_CHECK(!error);
        if (error)
        {
            std::cout << boost::diagnostic_information(*error) << std::endl;
        }

        BOOST_CHECK_EQUAL(result->status(), 0);

        BOOST_CHECK(result->message().empty());
        BOOST_CHECK(!result->newEVMContractAddress().empty());
        BOOST_CHECK_LT(result->gasAvailable(), gas);
    }
}

BOOST_AUTO_TEST_CASE(keyLock) {}

BOOST_AUTO_TEST_CASE(deployErrorCode)
{
    // an infinity-loop constructor
    {
        std::string errorBin =
            "608060405234801561001057600080fd5b505b60011561006a576040518060400160405280600381526020"
            "017f"
            "31323300000000000000000000000000000000000000000000000000000000008152506000908051906020"
            "0190"
            "61006492919061006f565b50610012565b610114565b828054600181600116156101000203166002900490"
            "6000"
            "52602060002090601f016020900481019282601f106100b057805160ff19168380011785556100de565b82"
            "8001"
            "600101855582156100de579182015b828111156100dd5782518255916020019190600101906100c2565b5b"
            "5090"
            "506100eb91906100ef565b5090565b61011191905b8082111561010d5760008160009055506001016100f5"
            "565b"
            "5090565b90565b6101f8806101236000396000f3fe608060405234801561001057600080fd5b5060043610"
            "6100"
            "365760003560e01c806344733ae11461003b5780638e397a0314610059575b600080fd5b61004361006356"
            "5b60"
            "40516100509190610140565b60405180910390f35b610061610105565b005b606060008054600181600116"
            "1561"
            "01000203166002900480601f01602080910402602001604051908101604052809291908181526020018280"
            "5460"
            "0181600116156101000203166002900480156100fb5780601f106100d05761010080835404028352916020"
            "0191"
            "6100fb565b820191906000526020600020905b8154815290600101906020018083116100de57829003601f"
            "1682"
            "01915b5050505050905090565b565b600061011282610162565b61011c818561016d565b935061012c8185"
            "6020"
            "860161017e565b610135816101b1565b840191505092915050565b60006020820190508181036000830152"
            "6101"
            "5a8184610107565b905092915050565b600081519050919050565b60008282526020820190509291505056"
            "5b60"
            "005b8381101561019c578082015181840152602081019050610181565b838111156101ab57600084840152"
            "5b50"
            "505050565b6000601f19601f830116905091905056fea2646970667358221220e4e19dff46d31f82111f92"
            "61d8"
            "687c52312c9221962991e27bbddc409dfbd7c564736f6c634300060a0033";
        bytes input;
        boost::algorithm::unhex(errorBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        std::string addressString = addressCreate.hex().substr(0, 40);

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(addressString);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(1);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
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
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::OutOfGas);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), addressString);
        BOOST_CHECK(result->to() == sender);

        TwoPCParams commitParams{};
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
    }

    // wasm code in evm
    {
        std::string errorBin =
            "0061736d01000000018b011660037f7f7f017f60027f7f017f60047f7f7f7f017f6000017f60017f006002"
            "7f7f0060047f7f7f7f0060077f7f7f7f7f7f7f0060037f7e7e0060037e7f7f0060057f7f7f7f7f017f6003"
            "7f7f7f0060017f017f60057f7f7f7e7e0060000060037e7e7f0060057f7f7f7f7f0060027f7f017e60027f"
            "7e0060057f7e7e7e7e0060027e7e017e60017e017f02b0010a0462636f730463616c6c00020462636f7311"
            "67657452657475726e4461746153697a6500030462636f730d67657452657475726e446174610004046263"
            "6f730666696e69736800050462636f730a73657453746f7261676500060462636f73036c6f670007046263"
            "6f730672657665727400050462636f730a67657453746f7261676500000462636f730f67657443616c6c44"
            "61746153697a6500030462636f730b67657443616c6c44617461000403a601a4010808090a040404040507"
            "01050b0005060b050605040b0b01040b0b0c0505050505050305040504040d0c0506040b06010201000400"
            "01010b05050505030e0505050404040e0505050f050505040404060610021102060b120504050c05050505"
            "0b050506050505050b050505040005040b05050b0b0b04050505010b01020602050b0605040b1005050505"
            "0505000b010c0004131313000000001413151514140811121204050170010a0a05030100110609017f0141"
            "8080c0000b072604066d656d6f7279020009686173685f747970650046066465706c6f790047046d61696e"
            "004e0910010041010b0984010f17143b3d3e3f400a91fd01a401f90201027f23004180016b220324002000"
            "4200370204200041002802908540360200200341cc006a418080c000360200200341033a00502003428080"
            "808080043703302003200036024820034100360240200341003602382003412736027c200341186a200120"
            "02100b200341206a2903002101200329031821022003290328200341d5006a200341fc006a100c02400240"
            "02402002200184500d00200328027c2200416c6a220420004e0d01200341d5006a41146a4130200410a201"
            "1a2003411436027c200320022001100b200341086a2903002102200329030021012003290310200341d500"
            "6a200341fc006a100c2001200284500d00200328027c2200417f6a220420004e0d01200341d6006a413020"
            "0410a2011a2003410036027c2001a741ff017141306a220041ff01712000470d01200320003a00550b4127"
            "200328027c22006b220441284f0d00200341306a41d082c0004100200341d5006a20006a2004100d0d0120"
            "034180016a24000f0b00000b4137100e000bfd0202017f047e230041d0006b220324000240024020024280"
            "8020540d00200341206a2001420042d2e1aadaeda7c987f6004200109d01200341106a2001420042f3b2d8"
            "c19e9ebdcc957f4200109d01200341c0006a2002420042d2e1aadaeda7c987f6004200109d01200341306a"
            "2002420042f3b2d8c19e9ebdcc957f4200109d01200341c0006a41086a290300200341206a41086a290300"
            "200341106a41086a290300220420032903207c2205200454ad7c220620032903407c2204200654ad7c2004"
            "200341306a41086a290300200520032903307c200554ad7c7c2206200454ad7c2204423e8821052006423e"
            "8820044202868421040c010b20014213882002422d868442bda282a38eab04802104420021050b20032004"
            "2005428080a0cfc8e0c8e38a7f4200109d0102402001200329030022067d22072001562002200341086a29"
            "03007d2001200654ad7d220120025620012002511b0d002000200437030020002007370310200020053703"
            "08200341d0006a24000f0b00000bf00503027f017e017f0240200228020022034114480d00024002402000"
            "42ffff83fea6dee111580d002002200341706a2204360200200320016a2203417e6a200042808084fea6de"
            "e11182220542e40082a7410174418881c0006a2f00003b00002003417c6a200542e4008042e40082a74101"
            "74418881c0006a2f00003b00002003417a6a20054290ce008042e40082a7410174418881c0006a2f00003b"
            "0000200341786a200542c0843d8042e40082a7410174418881c0006a2f00003b0000200341766a20054280"
            "c2d72f80a741e40070410174418881c0006a2f00003b0000200341746a20054280c8afa02580a741e40070"
            "410174418881c0006a2f00003b0000200341726a20054280a094a58d1d80a741e40070410174418881c000"
            "6a2f00003b0000200120046a2005428080e983b1de1680a741e40070410174418881c0006a2f00003b0000"
            "200042808084fea6dee1118021000c010b024020004280c2d72f5a0d00200321040c010b2002200341786a"
            "2204360200200320016a2206417e6a20004280c2d72f82a7220341e40070410174418881c0006a2f00003b"
            "00002006417c6a200341e4006e41e40070410174418881c0006a2f00003b00002006417a6a20034190ce00"
            "6e41e40070410174418881c0006a2f00003b0000200120046a200341c0843d6e41e40070410174418881c0"
            "006a2f00003b000020004280c2d72f8021000b02402000a722034190ce00490d00200420016a417e6a2003"
            "4190ce0070220641e40070410174418881c0006a2f00003b000020012004417c6a22046a200641e4006e41"
            "0174418881c0006a2f00003b000020034190ce006e21030b0240200341ffff0371220641e400490d002001"
            "2004417e6a22046a200641e40070410174418881c0006a2f00003b0000200641e4006e21030b0240200341"
            "ffff037141094b0d0020022004417f6a2204360200200120046a200341306a3a00000f0b20022004417e6a"
            "2204360200200120046a200341ffff0371410174418881c0006a2f00003b00000f0b00000bfa0301077f23"
            "0041106b22052400418080c40021062004210702400240024020002802002208410171450d00200441016a"
            "22072004490d01412b21060b0240024020084104710d0041002101200721090c010b20072001200120026a"
            "1086016a22092007490d010b41012107024020002802084101460d0020002006200120021087010d022000"
            "280218200320042000411c6a28020028020c11000021070c020b0240024020092000410c6a280200220a4f"
            "0d0020084108710d01200a20096b2208200a4b0d0241012107200520002008410110880120052802002208"
            "418080c400460d032005280204210920002006200120021087010d0320002802182201200320042000411c"
            "6a280200220028020c1100000d03200820092001200010890121070c030b20002006200120021087010d02"
            "2000280218200320042000411c6a28020028020c11000021070c020b200028020421082000413036020420"
            "002d0020210b41012107200041013a002020002006200120021087010d01200a20096b2201200a4b0d0041"
            "012107200541086a20002001410110880120052802082201418080c400460d01200528020c210220002802"
            "182206200320042000411c6a280200220928020c1100000d0120012002200620091089010d012000200b3a"
            "002020002008360204410021070c010b00000b200541106a240020070b040000000b0600200010100b2601"
            "017f024020002802004100200028020422001b2201450d002000450d002001200010120b0b3901017f2000"
            "10100240200041146a2d00004102460d00024020002802102201280200450d002001101020002802102101"
            "0b2001410c10120b0bf90101037f02402000450d002001109a011a41002802a888402102200041786a2201"
            "20012802002203417e713602000240024002402003417c71220420006b20044b0d00200041003602002000"
            "417c6a280200417c712204450d0120042d00004101710d012001109c0120042802002100024020012d0000"
            "410271450d002004200041027222003602000b200221012000417c71220020046b41786a20004d0d020b00"
            "000b02402003417c712204450d004100200420034102711b2203450d0020032d00004101710d0020002003"
            "280208417c7136020020032001410172360208200221010c010b200020023602000b410020013602a88840"
            "0b0b990403077f017e067f024002402005417f6a220720054b0d0020012802082208417f6a210920052001"
            "280210220a6b220b20054b210c2001280214210d2001290300210e0340200d20076a220f200d490d010240"
            "200f2003490d00200120033602144100210f0c030b0240200e2002200f6a310000423f8388420183500d00"
            "20082008200128021c221020061b200820104b1b220f2005200f20054b1b2111034002402011200f470d00"
            "4100201020061b21112009210f034002402011200f41016a490d00200d20056a220f200d490d062001200f"
            "360214024020060d002001410036021c0b2000200d360204200041086a200f3602004101210f0c070b200f"
            "20054f0d05200d200f6a2212200d490d05201220034f0d052004200f6a2113200f417f6a210f20132d0000"
            "41ff0171200220126a2d0000460d000b200d200a6a220f200d490d042001200f360214200f210d20060d03"
            "200c0d042001200b36021c200f210d0c030b200d200f6a2212200d490d03201220034f0d032004200f6a21"
            "13200f41016a2214210f20132d000041ff0171200220126a2d0000460d000b2014417f6a221220086b220f"
            "20124b0d02200f41016a2212200f490d02200d20126a220f200d490d022001200f360214200f210d20060d"
            "012001410036021c200f210d0c010b200d20056a220f200d490d012001200f360214200f210d20060d0020"
            "01410036021c200f210d0c000b0b00000b2000200f3602000bad0201027f230041106b2202240002400240"
            "024002400240200141ff004b0d000240200028020822032000280204470d00200041011015200028020821"
            "030b200028020020036a20013a0000200341016a22012003490d01200020013602080c040b200241003602"
            "0c2001418010490d0102402001418080044f0d0020022001413f71418001723a000e20022001410c7641e0"
            "01723a000c20022001410676413f71418001723a000d410321010c030b20022001413f71418001723a000f"
            "2002200141127641f001723a000c20022001410676413f71418001723a000e20022001410c76413f714180"
            "01723a000d410421010c020b00000b20022001413f71418001723a000d2002200141067641c001723a000c"
            "410221010b20002002410c6a200110160b200241106a240041000bb20101037f230041206b220224000240"
            "024020002802042203200028020822046b20014f0d00200420016a22012004490d01200320036a22042003"
            "490d0120042001200420014b1b22014108200141084b1b2101024002402003450d00200241106a41086a41"
            "0136020020022003360214200220002802003602100c010b200241003602100b200220014101200241106a"
            "101c20022802004101460d01200020022902043702000b200241206a24000f0b00000b3701017f20002002"
            "10152000280200200028020822036a2001200210a0011a0240200320026a220220034f0d0000000b200020"
            "023602080b0c00200020012002101641000bb00201047f230041306b220224000240024002402000280208"
            "22032000280204470d0020";
        bytes input;
        boost::algorithm::unhex(errorBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        std::string addressString = addressCreate.hex().substr(0, 40);

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(addressString);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(2);

        std::promise<void> nextPromise;
        executor->nextBlockHeader(blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
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
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::Unknown);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), addressString);
        BOOST_CHECK(result->to() == sender);

        TwoPCParams commitParams{};
        commitParams.number = 2;

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
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
