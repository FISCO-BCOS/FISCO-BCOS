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

#include "../mock/MockLedger.h"
#include "../mock/MockTransactionalStorage.h"
#include "../mock/MockTxPool.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlockHeader.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageFactory.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "evmc/evmc.h"
#include "executor/TransactionExecutorFactory.h"
#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/protocol/Protocol.h>
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
        boost::log::core::get()->set_logging_enabled(false);
        hashImpl = std::make_shared<Keccak256>();
        assert(hashImpl);
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        ledger = std::make_shared<MockLedger>();
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(backend, false);
        auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
        executor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
            backend, executionResultFactory, stateStorageFactory, hashImpl, false, false);


        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        memcpy(keyPair->secretKey()->mutableData(),
            fromHexString("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e")
                ->data(),
            32);
        // address: "11ac3ca85a307ae2aff614e83949ab691ba019c5"
        memcpy(keyPair->publicKey()->mutableData(),
            fromHexString(
                "ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
                "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae")
                ->data(),
            64);
        eoa = keyPair->address(hashImpl).hex();

        codec = std::make_unique<bcos::CodecWrapper>(hashImpl, false);
    }
    ~TransactionExecutorFixture() { boost::log::core::get()->set_logging_enabled(true); }

    TransactionExecutor::Ptr executor;
    CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<MockLedger> ledger;
    std::shared_ptr<Keccak256> hashImpl;
    std::string eoa;

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
    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
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

    ledger->setBlockHeader(blockHeader);
    std::promise<void> commitPromise;
    executor->commit(commitParams, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        commitPromise.set_value();
    });
    commitPromise.get_future().get();
    auto tableName = std::string("/apps/") + std::string(result->newEVMContractAddress());


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
    auto blockHeader2 = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader2->setNumber(2);

    parentInfos = {{{blockHeader2->number() - 1, h256(blockHeader2->number() - 1)}}};
    blockHeader2->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader2->number() - 1);
    blockHeader2->calculateHash(*cryptoSuite->hashImpl());

    std::promise<void> nextPromise2;
    executor->nextBlockHeader(0, std::move(blockHeader2), [&](bcos::Error::Ptr&& error) {
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
    executor->dmcExecuteTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();

    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->status(), 0);
    BOOST_CHECK_EQUAL(result2->evmStatus(), 0);
    BOOST_CHECK_EQUAL(result2->message(), "");
    BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), "");
    BOOST_CHECK_LT(result2->gasAvailable(), gas);
    ledger->setBlockHeader(blockHeader2);

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
    executor->dmcExecuteTransaction(std::move(params3),
        [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise3.set_value(std::move(result));
        });
    auto result3 = executePromise3.get_future().get();

    BOOST_CHECK(result3);
    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK_EQUAL(result3->evmStatus(), 0);
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

    ledger::SystemConfigEntry se = {"1", 0};
    storage::Entry entry;
    entry.setObject(se);
    backend->setRow(ledger::SYS_CONFIG, "feature_evm_address", entry);
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
    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Create contract A
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    auto address = result->newEVMContractAddress();
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
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
    executor->dmcExecuteTransaction(std::move(params2),
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
    BOOST_CHECK_EQUAL(result2->keyLocks().size(), 2);  // code,codeHash
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
    executor->dmcExecuteTransaction(std::move(result2),
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
    BOOST_CHECK_EQUAL(result3->evmStatus(), 0);
    BOOST_CHECK(result3->logEntries().size() == 0);
    BOOST_CHECK(result3->keyLocks().empty());

    // --------------------------------
    // Message 2: Create contract B success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result3->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise4;
    executor->dmcExecuteTransaction(std::move(result3),
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
    BOOST_CHECK_EQUAL(result4->to(), std::string(addressString2));
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
    executor->dmcExecuteTransaction(std::move(result4),
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
    BOOST_CHECK_EQUAL(result5->from(), std::string(addressString2));
    BOOST_CHECK_EQUAL(result5->to(), std::string(address));
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK_EQUAL(result5->evmStatus(), 0);
    BOOST_CHECK(result5->message().empty());
    BOOST_CHECK_EQUAL(result5->keyLocks().size(), 0);

    // --------------------------------
    // Message 4: A call B's success return, set previous seq 1001
    // B -> A
    // --------------------------------
    result5->setSeq(1001);
    std::promise<ExecutionMessage::UniquePtr> executePromise6;
    executor->dmcExecuteTransaction(std::move(result5),
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
    BOOST_CHECK_EQUAL(result6->evmStatus(), 0);
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
    ledger->setBlockHeader(blockHeader);
    executor->commit(commitParams, [](bcos::Error::Ptr error) { BOOST_CHECK(!error); });


    // execute a call request
    auto callParam = std::make_unique<NativeExecutionMessage>();
    callParam->setType(executor::NativeExecutionMessage::MESSAGE);
    callParam->setContextID(500);
    callParam->setSeq(7778);
    callParam->setDepth(0);
    callParam->setFrom(std::string(sender));
    callParam->setTo(std::string(addressString2));
    callParam->setData(codec->encodeWithSig("value()"));
    callParam->setOrigin(std::string(sender));
    callParam->setStaticCall(true);
    callParam->setGasAvailable(gas);
    callParam->setCreate(false);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> callResultPromise;
    executor->dmcCall(std::move(callParam),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResultPromise.set_value(std::move(response));
        });

    bcos::protocol::ExecutionMessage::UniquePtr callResult = callResultPromise.get_future().get();


    BOOST_CHECK_EQUAL(callResult->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult->status(), 0);
    BOOST_CHECK_EQUAL(callResult->evmStatus(), 0);

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
    callParam2->setTo(std::string(addressString2));
    callParam2->setData(codec->encodeWithSig("value()"));
    callParam2->setOrigin(std::string(sender));
    callParam2->setStaticCall(true);
    callParam2->setGasAvailable(gas);
    callParam2->setCreate(false);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> callResult2Promise;
    executor->dmcCall(std::move(callParam2),
        [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr response) {
            BOOST_CHECK(!error);
            callResult2Promise.set_value(std::move(response));
        });

    bcos::protocol::ExecutionMessage::UniquePtr callResult2 = callResult2Promise.get_future().get();


    BOOST_CHECK_EQUAL(callResult2->type(), protocol::ExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(callResult2->status(), 0);
    BOOST_CHECK_EQUAL(callResult2->evmStatus(), 0);

    auto expectResult2 = codec->encode(s256(1000));
    BOOST_CHECK(callResult2->data().toBytes() == expectResult);

    std::string tableName;
    tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
    tableName.append(address);

    {
        auto [e, nonceEntry] = backend->getRow(tableName, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
        BOOST_CHECK(!e);
        BOOST_CHECK(nonceEntry);
        BOOST_CHECK_EQUAL(nonceEntry->get(), "1");
    }
}

BOOST_AUTO_TEST_CASE(performance)
{
    size_t count = 100;

    bcos::crypto::HashType hash;
    for (size_t blockNumber = 1; blockNumber < 3; ++blockNumber)
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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
        h256 addressCreate(hashImpl->hash(bytesConstRef(addressSeed)));
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

        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
        blockHeader->setNumber(blockNumber);

        std::vector<bcos::protocol::ParentInfo> parentInfos{
            {blockHeader->number() - 1, h256(blockHeader->number() - 1)}};
        blockHeader->setParentInfo(parentInfos);
        ledger->setBlockNumber(blockHeader->number() - 1);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());
        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();

        // --------------------------------
        // Create contract ParallelOk
        // --------------------------------
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->dmcExecuteTransaction(
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
            executor->dmcExecuteTransaction(std::move(params),
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

            executor->dmcExecuteTransaction(
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
        std::vector<u256> values = {};
        values.reserve(count);
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
            executor->dmcExecuteTransaction(
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
            values.push_back(value);
            //
            //            if (i < count - 1)
            //            {
            //                BOOST_CHECK_EQUAL(value, u256(1000000 - 10));
            //            }
            //            else
            //            {
            //                BOOST_CHECK_EQUAL(value, u256(1000000 + 10 * (count - 1)));
            //            }
        }
        size_t c = std::count(values.begin(), values.end(), u256(1000000 - 10));
        BOOST_CHECK(c == count - 1);
        BOOST_CHECK_EQUAL(values.at(values.size() - 1), u256(1000000 + 10 * (count - 1)));

        std::cout << "Check elapsed: "
                  << (std::chrono::system_clock::now() - now).count() / 1000 / 1000 << std::endl;

        executor->getHash(
            blockNumber, [&hash](bcos::Error::UniquePtr error, crypto::HashType resultHash) {
                BOOST_CHECK(!error);
                BOOST_CHECK_NE(resultHash, h256());
                hash = resultHash;
            });
    }
}

BOOST_AUTO_TEST_CASE(multiDeploy)
{
    size_t count = 10;
    std::vector<NativeExecutionMessage::UniquePtr> paramsList;

    for (size_t i = 0; i < count; ++i)
    {
        auto helloworld = string(helloBin);
        bytes input;
        boost::algorithm::unhex(helloworld, std::back_inserter(input));
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", input, std::to_string(100 + i), 100001, "1", "1");

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

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();


    std::vector<std::tuple<bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr>>
        responses(count);
    for (size_t i = 0; i < paramsList.size(); ++i)
    {
        // not support multi-thread executeTransaction
        boost::latch latch(1);
        executor->dmcExecuteTransaction(std::move(std::move(paramsList[i])),
            [&](bcos::Error::UniquePtr error, bcos::protocol::ExecutionMessage::UniquePtr result) {
                responses[i] = std::make_tuple(std::move(error), std::move(result));
                latch.count_down();
            });
        latch.wait();
    }


    for (auto& it : responses)
    {
        auto& [error, result] = it;

        BOOST_CHECK(!error);
        if (error)
        {
            std::cout << boost::diagnostic_information(*error) << std::endl;
        }

        BOOST_CHECK_EQUAL(result->status(), 0);
        BOOST_CHECK_EQUAL(result->evmStatus(), 0);
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
        blockHeader->setNumber(1);

        std::vector<bcos::protocol::ParentInfo> parentInfos{
            {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
        blockHeader->setParentInfo(parentInfos);
        ledger->setBlockNumber(blockHeader->number() - 1);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());
        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
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
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::OutOfGas);
        BOOST_CHECK_EQUAL(result->evmStatus(), (int32_t)evmc_status_code::EVMC_OUT_OF_GAS);
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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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

        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
        blockHeader->setNumber(2);

        std::vector<bcos::protocol::ParentInfo> parentInfos{
            {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
        blockHeader->setParentInfo(parentInfos);
        ledger->setBlockNumber(blockHeader->number() - 1);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());

        std::promise<void> nextPromise;
        executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
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
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::Unknown);
        BOOST_CHECK_EQUAL(result->evmStatus(), (int32_t)evmc_status_code::EVMC_SUCCESS);
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

BOOST_AUTO_TEST_CASE(delegateCall)
{
    /*
pragma solidity>=0.6.10 <0.8.20;

contract DelegateCallTest {
    int public value = 0;
    address public sender;

    function add() public returns(bytes memory) {
        value += 1;
        return "1";
    }

    function testFailed() public {
        address addr = address(0x1001);

        dCall(addr, "add()");
    }

    function testSuccess() public {
        address addr = address(this);
        dCall(addr, "add()");
    }

    function dCall(address addr, string memory func) private returns(bytes memory) {
        bool success;
        bytes memory ret;
        (success, ret) = addr.delegatecall(abi.encodeWithSignature(func));
        require(success, "delegatecall failed");

        return ret;
    }
}

{
    "4f2be91f": "add()",
    "67e404ce": "sender()",
    "0ddbcc82": "testFailed()",
    "713d3e3e": "testSuccess()",
    "3fa4f245": "value()"
}

     */

    std::string codeBin =
        "60806040526000805534801561001457600080fd5b50610361806100246000396000f3fe608060405234801561"
        "001057600080fd5b50600436106100575760003560e01c80630ddbcc821461005c5780633fa4f2451461006657"
        "80634f2be91f1461008257806367e404ce14610097578063713d3e3e146100c2575b600080fd5b6100646100ca"
        "565b005b61006f60005481565b6040519081526020015b60405180910390f35b61008a6100fc565b6040516100"
        "799190610285565b6001546100aa906001600160a01b031681565b6040516001600160a01b0390911681526020"
        "01610079565b610064610132565b600061100190506100f8816040518060400160405280600581526020016461"
        "6464282960d81b81525061015a565b5050565b6060600160008082825461011091906102ce565b909155505060"
        "40805180820190915260018152603160f81b6020820152919050565b60003090506100f8816040518060400160"
        "4052806005815260200164616464282960d81b8152505b60408051600481526024810191829052606091600091"
        "83916001600160a01b038716919061018990879061030f565b6040805191829003909120602083018051600160"
        "0160e01b03166001600160e01b0319909216919091179052516101c0919061030f565b60006040518083038185"
        "5af49150503d80600081146101fb576040519150601f19603f3d011682016040523d82523d6000602084013e61"
        "0200565b606091505b5090925090508161024d5760405162461bcd60e51b815260206004820152601360248201"
        "527219195b1959d85d1958d85b1b0819985a5b1959606a1b604482015260640160405180910390fd5b94935050"
        "5050565b60005b83811015610270578181015183820152602001610258565b8381111561027f57600084840152"
        "5b50505050565b60208152600082518060208401526102a4816040850160208701610255565b601f01601f1916"
        "9190910160400192915050565b634e487b7160e01b600052601160045260246000fd5b60008082128015600160"
        "0160ff1b03849003851316156102f0576102f06102b8565b600160ff1b83900384128116156103095761030961"
        "02b8565b50500190565b60008251610321818460208701610255565b919091019291505056fea2646970667358"
        "22122064d7211bf906a8ecf7041b98d7e2d2c243d6d8b7a79b19bb1e0968091916398e64736f6c634300080b00"
        "33";


    bytes input;
    boost::algorithm::unhex(codeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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
    std::string address = addressCreate.hex().substr(0, 40);
    params->setTo(address);

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::MESSAGE);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Deploy
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);


    // --------------------------------
    // Get code
    // --------------------------------
    std::promise<bcos::bytes> executePromise1;
    executor->getCode(address, [&](Error::Ptr error, bcos::bytes code) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!code.empty());
        BOOST_CHECK_GT(code.size(), 0);
        executePromise1.set_value(code);
    });
    auto code = executePromise1.get_future().get();

    // --------------------------------
    // DelegateCall
    // --------------------------------
    std::string testFailedSelector = "0ddbcc82";
    input = bcos::bytes();
    boost::algorithm::unhex(testFailedSelector, std::back_inserter(input));

    tx = fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(102), 100002, "1", "1");
    sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(101);
    params->setSeq(1001);
    params->setDepth(0);
    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));
    // The contract address
    params->setTo(address);

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::MESSAGE);
    params->setTransactionHash(hash);
    params->setCreate(false);
    params->setDelegateCall(true);
    params->setDelegateCallCode(code);
    params->setDelegateCallSender(sender);
    params->setDelegateCallAddress(address);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    result = executePromise2.get_future().get();

    BOOST_CHECK_EQUAL(result->delegateCall(), true);
    BOOST_CHECK_EQUAL(result->delegateCallAddress(), "0000000000000000000000000000000000001001");
    BOOST_CHECK_EQUAL(result->delegateCallSender(), sender);
    BOOST_CHECK_EQUAL(result->keyLocks().size(), 0);  // no need to access codeHash flag
}

BOOST_AUTO_TEST_CASE(selfdestruct)
{
    // test-bcos-executor -t TestTransactionExecutor/selfdestruct

    /*
   pragma solidity>=0.6.10 <0.8.20;

contract HelloWorld {
    string name;

    constructor() public {
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }

    function selfdestructTest() public {
        selfdestruct(payable(address(0)));
    }
}

{
    "6d4ce63c": "get()",
    "fa967e1f": "selfdestructTest()",
    "4ed3885e": "set(string)"
}
        */

    std::string codeBin =
        "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c2057"
        "6f726c6421000000000000000000000000000000000000008152506000908051906020019061005c9291906100"
        "62565b50610166565b82805461006e90610105565b90600052602060002090601f016020900481019282610090"
        "57600085556100d7565b82601f106100a957805160ff19168380011785556100d7565b82800160010185558215"
        "6100d7579182015b828111156100d65782518255916020019190600101906100bb565b5b5090506100e4919061"
        "00e8565b5090565b5b808211156101015760008160009055506001016100e9565b5090565b6000600282049050"
        "600182168061011d57607f821691505b6020821081141561013157610130610137565b5b50919050565b7f4e48"
        "7b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b61"
        "04d7806101756000396000f3fe608060405234801561001057600080fd5b50600436106100415760003560e01c"
        "80634ed3885e146100465780636d4ce63c14610062578063fa967e1f14610080575b600080fd5b610060600480"
        "360381019061005b9190610263565b61008a565b005b61006a6100a4565b60405161007791906102e5565b6040"
        "5180910390f35b610088610136565b005b80600090805190602001906100a0929190610150565b5050565b6060"
        "600080546100b3906103bb565b80601f0160208091040260200160405190810160405280929190818152602001"
        "8280546100df906103bb565b801561012c5780601f106101015761010080835404028352916020019161012c56"
        "5b820191906000526020600020905b81548152906001019060200180831161010f57829003601f168201915b50"
        "50505050905090565b600073ffffffffffffffffffffffffffffffffffffffff16ff5b82805461015c906103bb"
        "565b90600052602060002090601f01602090048101928261017e57600085556101c5565b82601f106101975780"
        "5160ff19168380011785556101c5565b828001600101855582156101c5579182015b828111156101c457825182"
        "55916020019190600101906101a9565b5b5090506101d291906101d6565b5090565b5b808211156101ef576000"
        "8160009055506001016101d7565b5090565b60006102066102018461032c565b610307565b9050828152602081"
        "0184848401111561022257610221610481565b5b61022d848285610379565b509392505050565b600082601f83"
        "011261024a5761024961047c565b5b813561025a8482602086016101f3565b91505092915050565b6000602082"
        "840312156102795761027861048b565b5b600082013567ffffffffffffffff8111156102975761029661048656"
        "5b5b6102a384828501610235565b91505092915050565b60006102b78261035d565b6102c18185610368565b93"
        "506102d1818560208601610388565b6102da81610490565b840191505092915050565b60006020820190508181"
        "0360008301526102ff81846102ac565b905092915050565b6000610311610322565b905061031d82826103ed56"
        "5b919050565b6000604051905090565b600067ffffffffffffffff8211156103475761034661044d565b5b6103"
        "5082610490565b9050602081019050919050565b600081519050919050565b6000828252602082019050929150"
        "50565b82818337600083830152505050565b60005b838110156103a65780820151818401526020810190506103"
        "8b565b838111156103b5576000848401525b50505050565b600060028204905060018216806103d357607f8216"
        "91505b602082108114156103e7576103e661041e565b5b50919050565b6103f682610490565b810181811067ff"
        "ffffffffffffff821117156104155761041461044d565b5b80604052505050565b7f4e487b7100000000000000"
        "000000000000000000000000000000000000000000600052602260045260246000fd5b7f4e487b710000000000"
        "0000000000000000000000000000000000000000000000600052604160045260246000fd5b600080fd5b600080"
        "fd5b600080fd5b600080fd5b6000601f19601f830116905091905056fea2646970667358221220b86cdade4230"
        "cd8c479fc0822250b79a3f731a0e6d2dd04df1f043536198e02464736f6c63430008070033";


    bytes input;
    boost::algorithm::unhex(codeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310f");
    std::string address = addressCreate.hex().substr(0, 40);
    params->setTo(address);

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::MESSAGE);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {blockHeader->number() - 1, h256(blockHeader->number() - 1)}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------------
    // Deploy
    // --------------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });

    auto result = executePromise.get_future().get();

    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);


    // --------------------------------
    // Get code
    // --------------------------------
    std::promise<bcos::bytes> executePromise1;
    executor->getCode(address, [&](Error::Ptr error, bcos::bytes code) {
        BOOST_CHECK(!error);
        BOOST_CHECK(!code.empty());
        BOOST_CHECK_GT(code.size(), 0);
        executePromise1.set_value(code);
    });
    auto code = executePromise1.get_future().get();

    // --------------------------------
    // selfdestruct
    // --------------------------------
    std::string testFailedSelector = "fa967e1f";
    input = bcos::bytes();
    boost::algorithm::unhex(testFailedSelector, std::back_inserter(input));

    tx = fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(102), 100002, "1", "1");
    sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(101);
    params->setSeq(1001);
    params->setDepth(0);
    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));
    // The contract address
    params->setTo(address);

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::MESSAGE);
    params->setTransactionHash(hash);
    params->setCreate(false);
    params->setDelegateCall(false);
    params->setDelegateCallCode(code);
    params->setDelegateCallSender(sender);
    params->setDelegateCallAddress(address);

    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });
    executePromise2.get_future().get();

    // --------------------------------
    // Get code again(should not get)
    // --------------------------------
    std::promise<bcos::bytes> executePromise3;
    executor->getHash(1, [&](bcos::Error::UniquePtr, crypto::HashType) {
        executor->getCode(address, [&](Error::Ptr error, bcos::bytes code) {
            BOOST_CHECK(!error);
            BOOST_CHECK(code.empty());
            BOOST_CHECK_EQUAL(code.size(), 0);
            executePromise3.set_value(code);
        });
    });

    executePromise3.get_future().get();
}

BOOST_AUTO_TEST_CASE(transientStorageTest)
{
    // test opcode tstore and tload, contracts:
    // https://github.com/OpenZeppelin/openzeppelin-contracts/blob/master/contracts/utils/StorageSlot.sol
    std::string transientCodeBin =
        "60806040526100337fbad128a9c9f118267291de46fed9cb24d9fbbe19a927cfee43cdc3b8e4eba17161010e60"
        "201b60201c565b5f556100647fe51529ae218841954601d43f697e9bb24b282c1a2ddf95745a7e79ee1b4b7b7d"
        "61011760201b60201c565b6001556100967f5d4010ae4473cd3ede543746d54ec6f990232434c2d4884f06e3cc"
        "4ac77168e561012060201b60201c565b6002556100c87fb734117ebc01eac75f020b05b2620ab71735dfa2175a"
        "c8e98f85bd7f529bb96f61012960201b60201c565b6003556100fa7feb7753d6e9a22bf47d9682cdc6f111b5de"
        "fde6c206047689bad23120af3743bd61013260201b60201c565b600455348015610108575f80fd5b5061013b56"
        "5b5f819050919050565b5f819050919050565b5f819050919050565b5f819050919050565b5f81905091905056"
        "5b6105e3806101485f395ff3fe608060405234801561000f575f80fd5b506004361061009c575f3560e01c8063"
        "c2b12a7311610064578063c2b12a7314610134578063d2282dc514610150578063e30081a01461016c578063f5"
        "b53e1714610188578063f8462a0f146101a65761009c565b80631f903037146100a05780632d1be94d146100be"
        "57806338cc4831146100dc57806368895979146100fa578063a53b1c1e14610118575b5f80fd5b6100a86101c2"
        "565b6040516100b591906102fa565b60405180910390f35b6100c66101d3565b6040516100d3919061032d565b"
        "60405180910390f35b6100e46101e4565b6040516100f19190610385565b60405180910390f35b6101026101f4"
        "565b60405161010f91906103b6565b60405180910390f35b610132600480360381019061012d9190610406565b"
        "610205565b005b61014e6004803603810190610149919061045b565b61021d565b005b61016a60048036038101"
        "9061016591906104b0565b610235565b005b61018660048036038101906101819190610505565b61024d565b00"
        "5b610190610264565b60405161019d919061053f565b60405180910390f35b6101c060048036038101906101bb"
        "9190610582565b610275565b005b5f6101ce60025461028d565b905090565b5f6101df600154610297565b9050"
        "90565b5f6101ef5f546102a1565b905090565b5f6102006003546102ab565b905090565b61021a816004546102"
        "b590919063ffffffff16565b50565b610232816002546102bc90919063ffffffff16565b50565b61024a816003"
        "546102c390919063ffffffff16565b50565b610261815f546102ca90919063ffffffff16565b50565b5f610270"
        "6004546102d1565b905090565b61028a816001546102db90919063ffffffff16565b50565b5f815c9050919050"
        "565b5f815c9050919050565b5f815c9050919050565b5f815c9050919050565b80825d5050565b80825d505056"
        "5b80825d5050565b80825d5050565b5f815c9050919050565b80825d5050565b5f819050919050565b6102f481"
        "6102e2565b82525050565b5f60208201905061030d5f8301846102eb565b92915050565b5f8115159050919050"
        "565b61032781610313565b82525050565b5f6020820190506103405f83018461031e565b92915050565b5f73ff"
        "ffffffffffffffffffffffffffffffffffffff82169050919050565b5f61036f82610346565b9050919050565b"
        "61037f81610365565b82525050565b5f6020820190506103985f830184610376565b92915050565b5f81905091"
        "9050565b6103b08161039e565b82525050565b5f6020820190506103c95f8301846103a7565b92915050565b5f"
        "80fd5b5f819050919050565b6103e5816103d3565b81146103ef575f80fd5b50565b5f81359050610400816103"
        "dc565b92915050565b5f6020828403121561041b5761041a6103cf565b5b5f610428848285016103f2565b9150"
        "5092915050565b61043a816102e2565b8114610444575f80fd5b50565b5f8135905061045581610431565b9291"
        "5050565b5f602082840312156104705761046f6103cf565b5b5f61047d84828501610447565b91505092915050"
        "565b61048f8161039e565b8114610499575f80fd5b50565b5f813590506104aa81610486565b92915050565b5f"
        "602082840312156104c5576104c46103cf565b5b5f6104d28482850161049c565b91505092915050565b6104e4"
        "81610365565b81146104ee575f80fd5b50565b5f813590506104ff816104db565b92915050565b5f6020828403"
        "121561051a576105196103cf565b5b5f610527848285016104f1565b91505092915050565b610539816103d356"
        "5b82525050565b5f6020820190506105525f830184610530565b92915050565b61056181610313565b81146105"
        "6b575f80fd5b50565b5f8135905061057c81610558565b92915050565b5f602082840312156105975761059661"
        "03cf565b5b5f6105a48482850161056e565b9150509291505056fea2646970667358221220c24f2807e50107ea"
        "7f1b14927e683f13801dba0387067d73ff19776123092a7d64736f6c63430008190033";

    bytes input;
    boost::algorithm::unhex(transientCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setTo(std::string(sender));
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);

    // feature_evm_cancun is off
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(result->status(), 10);
    BOOST_CHECK_EQUAL(result->evmStatus(), 5);
}

BOOST_AUTO_TEST_CASE(transientStorageTest2)
{
    // turn on feature_evm_cuncan swtich
    std::shared_ptr<MockTransactionalStorage> newStorage =
        std::make_shared<MockTransactionalStorage>(hashImpl);
    Entry entry;
    bcos::protocol::BlockNumber blockNumber = 0;
    entry.setObject(
        ledger::SystemConfigEntry{boost::lexical_cast<std::string>((int)1), blockNumber});
    newStorage->asyncSetRow(ledger::SYS_CONFIG, "feature_evm_cancun", entry,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    // check feature_evm_cancun whether is on
    auto entry1 = newStorage->getRow(ledger::SYS_CONFIG, "feature_evm_cancun");
    //    BOOST_CHECK_EQUAL(value, "1");
    //    BOOST_CHECK_EQUAL(enableNumber, 0);

    auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
    auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
    auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(newStorage, false);
    auto newExecutor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
        newStorage, executionResultFactory, stateStorageFactory, hashImpl, false, false);

    std::string transientCodeBin =
        "6080604052348015600e575f80fd5b506108748061001c5f395ff3fe608060405234801561000f575f80fd5b50"
        "60043610610029575f3560e01c8063fa3e317e1461002d575b5f80fd5b61004760048036038101906100429190"
        "610194565b61005d565b60405161005491906101d9565b60405180910390f35b5f808260405161006c90610150"
        "565b6100769190610201565b604051809103905ff08015801561008f573d5f803e3d5ffd5b5090505f8173ffff"
        "ffffffffffffffffffffffffffffffffffff16636428f3dc6040518163ffffffff1660e01b8152600401602060"
        "4051808303815f875af11580156100dd573d5f803e3d5ffd5b505050506040513d601f19601f82011682018060"
        "405250810190610101919061022e565b9050838114610145576040517f08c379a0000000000000000000000000"
        "00000000000000000000000000000000815260040161013c906102d9565b60405180910390fd5b600192505050"
        "919050565b610547806102f883390190565b5f80fd5b5f819050919050565b61017381610161565b811461017d"
        "575f80fd5b50565b5f8135905061018e8161016a565b92915050565b5f602082840312156101a9576101a86101"
        "5d565b5b5f6101b684828501610180565b91505092915050565b5f8115159050919050565b6101d3816101bf56"
        "5b82525050565b5f6020820190506101ec5f8301846101ca565b92915050565b6101fb81610161565b82525050"
        "565b5f6020820190506102145f8301846101f2565b92915050565b5f815190506102288161016a565b92915050"
        "565b5f602082840312156102435761024261015d565b5b5f6102508482850161021a565b91505092915050565b"
        "5f82825260208201905092915050565b7f73746f72652076616c7565206e6f7420657175616c20746c6f616420"
        "726573755f8201527f6c7400000000000000000000000000000000000000000000000000000000000060208201"
        "5250565b5f6102c3602283610259565b91506102ce82610269565b604082019050919050565b5f602082019050"
        "8181035f8301526102f0816102b7565b905091905056fe6080604052348015600e575f80fd5b50604051610547"
        "3803806105478339818101604052810190602e9190607b565b603d5f5482604260201b60201c565b5060a1565b"
        "80825d5050565b5f80fd5b5f819050919050565b605d81604d565b81146066575f80fd5b50565b5f8151905060"
        "75816056565b92915050565b5f60208284031215608d57608c6049565b5b5f6098848285016069565b91505092"
        "915050565b610499806100ae5f395ff3fe608060405234801561000f575f80fd5b5060043610610034575f3560"
        "e01c80633bc5de30146100385780636428f3dc14610056575b5f80fd5b610040610074565b60405161004d9190"
        "61015c565b60405180910390f35b61005e610084565b60405161006b919061015c565b60405180910390f35b5f"
        "61007f5f5461012d565b905090565b5f8060405161009290610137565b604051809103905ff0801580156100ab"
        "573d5f803e3d5ffd5b5090508073ffffffffffffffffffffffffffffffffffffffff1663c1df0f483060405182"
        "63ffffffff1660e01b81526004016100e791906101b4565b6020604051808303815f875af1158015610103573d"
        "5f803e3d5ffd5b505050506040513d601f19601f8201168201806040525081019061012791906101fb565b9150"
        "5090565b5f815c9050919050565b61023d8061022783390190565b5f819050919050565b61015681610144565b"
        "82525050565b5f60208201905061016f5f83018461014d565b92915050565b5f73ffffffffffffffffffffffff"
        "ffffffffffffffff82169050919050565b5f61019e82610175565b9050919050565b6101ae81610194565b8252"
        "5050565b5f6020820190506101c75f8301846101a5565b92915050565b5f80fd5b6101da81610144565b811461"
        "01e4575f80fd5b50565b5f815190506101f5816101d1565b92915050565b5f602082840312156102105761020f"
        "6101cd565b5b5f61021d848285016101e7565b9150509291505056fe6080604052348015600e575f80fd5b5061"
        "02218061001c5f395ff3fe608060405234801561000f575f80fd5b5060043610610029575f3560e01c8063c1df"
        "0f481461002d575b5f80fd5b6100476004803603810190610042919061013a565b61005d565b60405161005491"
        "9061017d565b60405180910390f35b5f808290505f8173ffffffffffffffffffffffffffffffffffffffff1663"
        "3bc5de306040518163ffffffff1660e01b8152600401602060405180830381865afa1580156100ac573d5f803e"
        "3d5ffd5b505050506040513d601f19601f820116820180604052508101906100d091906101c0565b9050809250"
        "5050919050565b5f80fd5b5f73ffffffffffffffffffffffffffffffffffffffff82169050919050565b5f6101"
        "09826100e0565b9050919050565b610119816100ff565b8114610123575f80fd5b50565b5f8135905061013481"
        "610110565b92915050565b5f6020828403121561014f5761014e6100dc565b5b5f61015c84828501610126565b"
        "91505092915050565b5f819050919050565b61017781610165565b82525050565b5f6020820190506101905f83"
        "018461016e565b92915050565b61019f81610165565b81146101a9575f80fd5b50565b5f815190506101ba8161"
        "0196565b92915050565b5f602082840312156101d5576101d46100dc565b5b5f6101e2848285016101ac565b91"
        "50509291505056fea2646970667358221220b74855b53bcf2b3cc2261d3f57e16507da8b24f81ff519699191e3"
        "4ce025bc8664736f6c63430008190033a26469706673582212201eb1e02998c84b08c3db1bdb2487184ca8357f"
        "1d2739ec65daa47ffe3e89316a64736f6c63430008190033a2646970667358221220bad0e7bec379be9bb4f446"
        "b845050f59959e8bd9da49d83f3c9f669a63e5e51e64736f6c63430008190033";
    bytes input;
    boost::algorithm::unhex(transientCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(100), 100000, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);


    auto params1 = std::make_unique<NativeExecutionMessage>();
    params1->setContextID(101);
    params1->setSeq(1001);
    params1->setDepth(0);
    params1->setOrigin(std::string(sender));
    params1->setFrom(std::string(sender));

    // The contract address
    params1->setTo(sender);
    params1->setStaticCall(false);
    params1->setGasAvailable(gas);
    params1->setData(input);
    params1->setType(NativeExecutionMessage::TXHASH);
    params1->setTransactionHash(hash);
    params1->setCreate(true);

    NativeExecutionMessage paramsBak1 = *params1;

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    newExecutor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    newExecutor->executeTransaction(std::move(params1),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);

    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(mcopy_opcode_test)
{
    // test opcode mcopy
    /*  solidity code
    pragma solidity 0.8.25;

    contract mcopy {
    function memoryCopy() external pure returns (bytes32 x) {
    assembly {
        mstore(0x20, 0x50)  // Store 0x50 at word 1 in memory
        mcopy(0, 0x20, 0x20)  // Copies 0x50 to word 0 in memory
        x := mload(0)    // Returns 32 bytes "0x50"
        }
    }
    }*/
    std::string mcopyCodeBin =
        "6080604052348015600e575f80fd5b5060b980601a5f395ff3fe6080604052348015600e575f80fd5b50600436"
        "106026575f3560e01c80632dbaeee914602a575b5f80fd5b60306044565b604051603b9190606c565b60405180"
        "910390f35b5f60506020526020805f5e5f51905090565b5f819050919050565b6066816056565b82525050565b"
        "5f602082019050607d5f830184605f565b9291505056fea2646970667358221220c16107fa00317d2d630d4d01"
        "9754eb2bae42e96482d0050308e60ec21c69d7eb64736f6c63430008190033";

    bytes input;
    boost::algorithm::unhex(mcopyCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setTo(std::string(sender));
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy on executor which feature_evm_cancun is off
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);

    // feature_evm_cancun is off
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(result->status(), 10);
    BOOST_CHECK_EQUAL(result->evmStatus(), 5);
}

BOOST_AUTO_TEST_CASE(mcopy_opcode_test_1)
{
    // turn on feature_evm_cuncan swtich
    std::shared_ptr<MockTransactionalStorage> newStorage =
        std::make_shared<MockTransactionalStorage>(hashImpl);
    Entry entry;
    bcos::protocol::BlockNumber blockNumber = 0;
    entry.setObject(
        ledger::SystemConfigEntry{boost::lexical_cast<std::string>((int)1), blockNumber});
    newStorage->asyncSetRow(ledger::SYS_CONFIG, "feature_evm_cancun", entry,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    // check feature_evm_cancun whether is on
    auto entry1 = newStorage->getRow(ledger::SYS_CONFIG, "feature_evm_cancun");
    //    BOOST_CHECK_EQUAL(value, "1");
    //    BOOST_CHECK_EQUAL(enableNumber, 0);

    auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
    auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
    auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(newStorage, false);
    auto newExecutor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
        newStorage, executionResultFactory, stateStorageFactory, hashImpl, false, false);

    std::string mcopyCodeBin =
        "6080604052348015600e575f80fd5b5060b980601a5f395ff3fe6080604052348015600e575f80"
        "fd5b50600436106026575f3560e01c80632dbaeee914602a575b5f80fd5b60306044565b604051603b9190606c"
        "565b60405180"
        "910390f35b5f60506020526020805f5e5f51905090565b5f819050919050565b6066816056565b82525050565b"
        "5f6020820190"
        "50607d5f830184605f565b9291505056fea2646970667358221220c16107fa00317d2d630d4d019754eb2bae42"
        "e96482d00503"
        "08e60ec21c69d7eb64736f6c63430008190033";

    bytes input;
    boost::algorithm::unhex(mcopyCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setTo(std::string(sender));
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    newExecutor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy on executor which feature_evm_cancun is off
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    newExecutor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(blobBaseFee_test)
{
    /*
     pragma solidity 0.8.25;

    contract blobBaseFee {
    function getBlobBaseFeeYul() external view returns (uint256 blobBaseFee) {
        assembly {
           blobBaseFee := blobbasefee()
       }
    }

    function getBlobBaseFeeSolidity() external view returns (uint256 blobBaseFee) {
        blobBaseFee = block.blobbasefee;
        }
    }
     */
    // turn on feature_evm_cuncan swtich
    std::shared_ptr<MockTransactionalStorage> newStorage =
        std::make_shared<MockTransactionalStorage>(hashImpl);
    Entry entry;
    bcos::protocol::BlockNumber blockNumber = 0;
    entry.setObject(
        ledger::SystemConfigEntry{boost::lexical_cast<std::string>((int)1), blockNumber});
    newStorage->asyncSetRow(ledger::SYS_CONFIG, "feature_evm_cancun", entry,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    // check feature_evm_cancun whether is on
    auto entry1 = newStorage->getRow(ledger::SYS_CONFIG, "feature_evm_cancun");
    //    BOOST_CHECK_EQUAL(value, "1");
    //    BOOST_CHECK_EQUAL(enableNumber, 0);

    auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
    auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
    auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(newStorage, false);
    auto newExecutor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
        newStorage, executionResultFactory, stateStorageFactory, hashImpl, false, false);

    std::string blobBaseFeeCodeBin =
        "6080604052348015600e575f80fd5b5060d980601a5f395ff3fe6080"
        "604052348015600e575f80fd5b50600436106030575f3560e01c806348a35d4e1460345780635b85fe9814"
        "604e575b5f80fd5b603a6068565b60405160459190608c565b60405180910390f35b6054606f565b604051"
        "605f9190608c565b60405180910390f35b5f4a905090565b5f4a905090565b5f819050919050565b608681"
        "6076565b82525050565b5f602082019050609d5f830184607f565b9291505056fea2646970667358221220"
        "954cfa4357894c6c07e251b6fe89f193c6348d4425388a3cae9a77548e09789964736f6c63430008190033";

    bytes input;
    boost::algorithm::unhex(blobBaseFeeCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setTo(std::string(sender));
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    newExecutor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy on executor which feature_evm_cancun is off
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    newExecutor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(blobHash_test)
{
    /*
    pragma solidity 0.8.25;
    contract blobhash {
        function storeBlobHash(uint256 index) external {
            assembly {
                sstore(0, blobhash(index))
            }
        }
    }
     */

    // turn on feature_evm_cuncan swtich
    std::shared_ptr<MockTransactionalStorage> newStorage =
        std::make_shared<MockTransactionalStorage>(hashImpl);
    Entry entry;
    bcos::protocol::BlockNumber blockNumber = 0;
    entry.setObject(
        ledger::SystemConfigEntry{boost::lexical_cast<std::string>((int)1), blockNumber});
    newStorage->asyncSetRow(ledger::SYS_CONFIG, "feature_evm_cancun", entry,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    // check feature_evm_cancun whether is on
    auto entry1 = newStorage->getRow(ledger::SYS_CONFIG, "feature_evm_cancun");
    //    BOOST_CHECK_EQUAL(value, "1");
    //    BOOST_CHECK_EQUAL(enableNumber, 0);

    auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
    auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
    auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(newStorage, false);
    auto newExecutor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
        newStorage, executionResultFactory, stateStorageFactory, hashImpl, false, false);

    std::string blobHashCodeBin =
        "6080604052348015600e575f80fd5b5060d780601a5f395ff3fe6080604052348015600e575f80fd5b50600436"
        "106026575f3560e01c8063ae67ac9b14602a575b5f80fd5b60406004803603810190603c9190607b565b604256"
        "5b005b80495f5550565b5f80fd5b5f819050919050565b605d81604d565b81146066575f80fd5b50565b5f8135"
        "90506075816056565b92915050565b5f60208284031215608d57608c6049565b5b5f6098848285016069565b91"
        "50509291505056fea2646970667358221220ad2b619fbd8e82b272adfa7e9278d31e0c2f6e109361b2206b5b03"
        "84aee5700f64736f6c63430008190033";
    bytes input;
    boost::algorithm::unhex(blobHashCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setTo(std::string(sender));
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;
    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    newExecutor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy on executor which feature_evm_cancun is off
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    newExecutor->dmcExecuteTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(getTransientStorageTest)
{
    // turn on feature_evm_cuncan swtich
    std::shared_ptr<MockTransactionalStorage> newStorage =
        std::make_shared<MockTransactionalStorage>(hashImpl);
    Entry entry;
    bcos::protocol::BlockNumber blockNumber = 0;
    entry.setObject(
        ledger::SystemConfigEntry{boost::lexical_cast<std::string>((int)1), blockNumber});
    newStorage->asyncSetRow(ledger::SYS_CONFIG, "feature_evm_cancun", entry,
        [](Error::UniquePtr error) { BOOST_CHECK_EQUAL(error.get(), nullptr); });
    // check feature_evm_cancun whether is on
    auto entry1 = newStorage->getRow(ledger::SYS_CONFIG, "feature_evm_cancun");
    //    BOOST_CHECK_EQUAL(value, "1");
    //    BOOST_CHECK_EQUAL(enableNumber, 0);

    auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
    auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
    auto lruStorage = std::make_shared<bcos::storage::LRUStateStorage>(newStorage, false);
    auto newExecutor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage,
        newStorage, executionResultFactory, stateStorageFactory, hashImpl, false, false);

    std::string transientCodeBin =
        "60806040527fe3598e46f24394be411dcf68a978c22ef80d97a0ef7c630d9b8e35d241c0210060015534801560"
        "32575f80fd5b506106a9806100405f395ff3fe608060405234801561000f575f80fd5b5060043610610055575f"
        "3560e01c806320965255146100595780632a130724146100775780635524107714610093578063613d81e11461"
        "00af57806374f7ca87146100b9575b5f80fd5b6100616100d7565b60405161006e91906103a4565b6040518091"
        "0390f35b610091600480360381019061008c919061041b565b6100eb565b005b6100ad60048036038101906100"
        "a89190610470565b61012d565b005b6100b761013a565b005b6100c1610369565b6040516100ce91906104aa56"
        "5b60405180910390f35b5f8060015490505f815c9050809250505090565b805f806101000a81548173ffffffff"
        "ffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217"
        "90555050565b5f600154905081815d5050565b5f73ffffffffffffffffffffffffffffffffffffffff165f8054"
        "906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffff"
        "ffffffffff16036101c7576040517f08c379a00000000000000000000000000000000000000000000000000000"
        "000081526004016101be9061051d565b60405180910390fd5b5f805f9054906101000a900473ffffffffffffff"
        "ffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16604051602401604051"
        "6020818303038152906040527fb7fd278600000000000000000000000000000000000000000000000000000000"
        "7bffffffffffffffffffffffffffffffffffffffffffffffffffffffff19166020820180517bffffffffffffff"
        "ffffffffffffffffffffffffffffffffffffffffff838183161783525050505060405161028f919061058d565b"
        "5f604051808303815f865af19150503d805f81146102c8576040519150601f19603f3d011682016040523d8252"
        "3d5f602084013e6102cd565b606091505b5050905080610311576040517f08c379a00000000000000000000000"
        "00000000000000000000000000000000008152600401610308906105ed565b60405180910390fd5b5f61031a61"
        "00d7565b90506103246100d7565b8114610365576040517f08c379a00000000000000000000000000000000000"
        "0000000000000000000000815260040161035c90610655565b60405180910390fd5b5050565b5f805490610100"
        "0a900473ffffffffffffffffffffffffffffffffffffffff1681565b5f819050919050565b61039e8161038c56"
        "5b82525050565b5f6020820190506103b75f830184610395565b92915050565b5f80fd5b5f73ffffffffffffff"
        "ffffffffffffffffffffffffff82169050919050565b5f6103ea826103c1565b9050919050565b6103fa816103"
        "e0565b8114610404575f80fd5b50565b5f81359050610415816103f1565b92915050565b5f6020828403121561"
        "04305761042f6103bd565b5b5f61043d84828501610407565b91505092915050565b61044f8161038c565b8114"
        "610459575f80fd5b50565b5f8135905061046a81610446565b92915050565b5f60208284031215610485576104"
        "846103bd565b5b5f6104928482850161045c565b91505092915050565b6104a4816103e0565b82525050565b5f"
        "6020820190506104bd5f83018461049b565b92915050565b5f82825260208201905092915050565b7f4220636f"
        "6e74726163742061646472657373206e6f74207365740000000000005f82015250565b5f610507601a836104c3"
        "565b9150610512826104d3565b602082019050919050565b5f6020820190508181035f830152610534816104fb"
        "565b9050919050565b5f81519050919050565b5f81905092915050565b8281835e5f83830152505050565b5f61"
        "05678261053b565b6105718185610545565b935061058181856020860161054f565b8084019150509291505056"
        "5b5f610598828461055d565b915081905092915050565b7f43616c6c20746f204220636f6e7472616374206661"
        "696c6564000000000000005f82015250565b5f6105d76019836104c3565b91506105e2826105a3565b60208201"
        "9050919050565b5f6020820190508181035f830152610604816105cb565b9050919050565b7f53746f72656420"
        "76616c756520646f6573206e6f74206d6174636800000000005f82015250565b5f61063f601b836104c3565b91"
        "5061064a8261060b565b602082019050919050565b5f6020820190508181035f83015261066c81610633565b90"
        "5091905056fea264697066735822122032dafff81aab17ab191d852a4391e85e6339bf0c051c273ddc9ac41348"
        "25fb5464736f6c63430008190033";
    bytes input;
    boost::algorithm::unhex(transientCodeBin, std::back_inserter(input));

    auto tx =
        fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(100), 100000, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
    // The contract address
    h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
    std::string address = addressCreate.hex().substr(0, 40);

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);


    auto params1 = std::make_unique<NativeExecutionMessage>();
    params1->setContextID(101);
    params1->setSeq(1001);
    params1->setDepth(0);
    params1->setOrigin(std::string(sender));
    params1->setFrom(std::string(sender));

    // The contract address
    params1->setTo(sender);
    params1->setStaticCall(false);
    params1->setGasAvailable(gas);
    params1->setData(input);
    params1->setType(NativeExecutionMessage::TXHASH);
    params1->setTransactionHash(hash);
    params1->setCreate(true);

    NativeExecutionMessage paramsBak1 = *params1;

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);
    blockHeader->setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    newExecutor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // --------------------------
    // Deploy
    // --------------------------
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    newExecutor->executeTransaction(std::move(params1),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK(result);

    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::FINISHED);
    BOOST_CHECK_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(result->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(testLegacyContractAddress)
{
    /**
    *
contract HelloFactory {
    string name;
    constructor() {
        name = "Hello, World!";
    }
    function newHello() public returns (HelloWorld) {
        HelloWorld newH = new HelloWorld();
        return newH;
    }
}
     */
    // clang-format off
    constexpr std::string_view helloFactory = "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610166565b82805461006e90610134565b90600052602060002090601f01602090048101928261009057600085556100d7565b82601f106100a957805160ff19168380011785556100d7565b828001600101855582156100d7579182015b828111156100d65782518255916020019190600101906100bb565b5b5090506100e491906100e8565b5090565b5b808211156101015760008160009055506001016100e9565b5090565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061014c57607f821691505b602082108114156101605761015f610105565b5b50919050565b610a29806101756000396000f3fe608060405234801561001057600080fd5b506004361061002b5760003560e01c80637567e67114610030575b600080fd5b61003861004e565b604051610045919061010f565b60405180910390f35b60008060405161005d90610083565b604051809103906000f080158015610079573d6000803e3d6000fd5b5090508091505090565b6108c98061012b83390190565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000819050919050565b60006100d56100d06100cb84610090565b6100b0565b610090565b9050919050565b60006100e7826100ba565b9050919050565b60006100f9826100dc565b9050919050565b610109816100ee565b82525050565b60006020820190506101246000830184610100565b9291505056fe608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50610166565b82805461006e90610134565b90600052602060002090601f01602090048101928261009057600085556100d7565b82601f106100a957805160ff19168380011785556100d7565b828001600101855582156100d7579182015b828111156100d65782518255916020019190600101906100bb565b5b5090506100e491906100e8565b5090565b5b808211156101015760008160009055506001016100e9565b5090565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061014c57607f821691505b602082108114156101605761015f610105565b5b50919050565b610754806101756000396000f3fe60806040526004361061003f5760003560e01c806329e99f07146100445780634ed3885e146100815780636d4ce63c1461009d578063de10cd7c146100c8575b600080fd5b34801561005057600080fd5b5061006b60048036038101906100669190610457565b610105565b604051610078919061051d565b60405180910390f35b61009b60048036038101906100969190610674565b610210565b005b3480156100a957600080fd5b506100b261022a565b6040516100bf919061051d565b60405180910390f35b3480156100d457600080fd5b506100ef60048036038101906100ea9190610457565b6102bc565b6040516100fc919061051d565b60405180910390f35b60606000600160008481526020019081526020016000209050600073ffffffffffffffffffffffffffffffffffffffff168160000160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff161461017b57600080fd5b80600201805461018a906106ec565b80601f01602080910402602001604051908101604052809291908181526020018280546101b6906106ec565b80156102035780601f106101d857610100808354040283529160200191610203565b820191906000526020600020905b8154815290600101906020018083116101e657829003601f168201915b5050505050915050919050565b806000908051906020019061022692919061036a565b5050565b606060008054610239906106ec565b80601f0160208091040260200160405190810160405280929190818152602001828054610265906106ec565b80156102b25780601f10610287576101008083540402835291602001916102b2565b820191906000526020600020905b81548152906001019060200180831161029557829003601f168201915b5050505050905090565b606060006001600084815260200190815260200160002090508060010180546102e4906106ec565b80601f0160208091040260200160405190810160405280929190818152602001828054610310906106ec565b801561035d5780601f106103325761010080835404028352916020019161035d565b820191906000526020600020905b81548152906001019060200180831161034057829003601f168201915b5050505050915050919050565b828054610376906106ec565b90600052602060002090601f01602090048101928261039857600085556103df565b82601f106103b157805160ff19168380011785556103df565b828001600101855582156103df579182015b828111156103de5782518255916020019190600101906103c3565b5b5090506103ec91906103f0565b5090565b5b808211156104095760008160009055506001016103f1565b5090565b6000604051905090565b600080fd5b600080fd5b6000819050919050565b61043481610421565b811461043f57600080fd5b50565b6000813590506104518161042b565b92915050565b60006020828403121561046d5761046c610417565b5b600061047b84828501610442565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b838110156104be5780820151818401526020810190506104a3565b838111156104cd576000848401525b50505050565b6000601f19601f8301169050919050565b60006104ef82610484565b6104f9818561048f565b93506105098185602086016104a0565b610512816104d3565b840191505092915050565b6000602082019050818103600083015261053781846104e4565b905092915050565b600080fd5b600080fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b610581826104d3565b810181811067ffffffffffffffff821117156105a05761059f610549565b5b80604052505050565b60006105b361040d565b90506105bf8282610578565b919050565b600067ffffffffffffffff8211156105df576105de610549565b5b6105e8826104d3565b9050602081019050919050565b82818337600083830152505050565b6000610617610612846105c4565b6105a9565b90508281526020810184848401111561063357610632610544565b5b61063e8482856105f5565b509392505050565b600082601f83011261065b5761065a61053f565b5b813561066b848260208601610604565b91505092915050565b60006020828403121561068a57610689610417565b5b600082013567ffffffffffffffff8111156106a8576106a761041c565b5b6106b484828501610646565b91505092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000600282049050600182168061070457607f821691505b60208210811415610718576107176106bd565b5b5091905056fea2646970667358221220078b71dbd60319b782bcbee4173e74b9db8c081e4ee20ea12cfbfb455b11964864736f6c634300080b0033a2646970667358221220a5b919e15def59a078d2162700e6c35a303db099f1817a77ed8dd4ecccbbcc2064736f6c634300080b0033";
    // clang-format on

    ledger::SystemConfigEntry se = {"1", 0};
    storage::Entry entry;
    entry.setObject(se);
    backend->setRow(ledger::SYS_CONFIG, "feature_evm_address", entry);
    bytes input;
    boost::algorithm::unhex(helloFactory, std::back_inserter(input));
    u256 nonce = 10086;
    auto tx = fakeTransaction(
        cryptoSuite, keyPair, "", input, nonce.convert_to<std::string>(), 100001, "1", "1");
    auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(1000);
    params->setDepth(0);
    auto senderBytes = keyPair->address(cryptoSuite->hashImpl());
    auto newAddress = newLegacyEVMAddressString(senderBytes.ref(), nonce);
    params->setTo(newAddress);

    params->setOrigin(std::string(sender));
    params->setFrom(std::string(sender));

    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(hash);
    params->setCreate(true);

    NativeExecutionMessage paramsBak = *params;

    auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
        [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
    blockHeader->setNumber(1);

    std::vector<bcos::protocol::ParentInfo> parentInfos{
        {{blockHeader->number() - 1, h256(blockHeader->number() - 1)}}};
    blockHeader->setParentInfo(parentInfos);
    ledger->setBlockNumber(blockHeader->number() - 1);
    blockHeader->calculateHash(*cryptoSuite->hashImpl());
    std::promise<void> nextPromise;
    executor->nextBlockHeader(0, blockHeader, [&](bcos::Error::Ptr&& error) {
        BOOST_CHECK(!error);
        nextPromise.set_value();
    });
    nextPromise.get_future().get();

    // deploy contract HelloFactory
    std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise.set_value(std::move(result));
        });
    auto result = executePromise.get_future().get();
    BOOST_CHECK_EQUAL(result->newEVMContractAddress(), newAddress);

    // // check nonce
    // {
    //     std::string tableName;
    //     tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
    //     tableName.append(newAddress);
    //     auto [e, nonceEntry] = backend->getRow(tableName, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
    //     BOOST_CHECK(!e);
    //     BOOST_CHECK(nonceEntry);
    //     BOOST_CHECK_EQUAL(nonceEntry->get(), "1");
    // }

    // call newHello
    auto params2 = std::make_unique<NativeExecutionMessage>();
    params2->setContextID(101);
    params2->setSeq(1001);
    params2->setDepth(0);
    params2->setFrom(std::string(sender));
    params2->setTo(std::string(newAddress));
    params2->setOrigin(std::string(sender));
    params2->setStaticCall(false);
    params2->setGasAvailable(gas);
    params2->setCreate(false);

    params2->setData(codec->encodeWithSig("newHello()"));
    params2->setType(NativeExecutionMessage::MESSAGE);
    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(params2),
        [&](bcos::Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            BOOST_CHECK(!error);
            executePromise2.set_value(std::move(result));
        });

    auto result2 = executePromise2.get_future().get();
    Address newHello;
    codec->decode(result2->data(), newHello);

    auto helloFactoryAddress = fromHex(newAddress);
    auto const expectNewHelloAddress =
        newLegacyEVMAddressString(bcos::ref(helloFactoryAddress), u256(0));
    BOOST_CHECK_EQUAL(newHello.hex(), expectNewHelloAddress);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
