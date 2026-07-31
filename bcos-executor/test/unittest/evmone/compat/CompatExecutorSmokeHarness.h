/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Minimal TransactionExecutor harness for FC-S deploy / MCOPY smoke tests.
 *  @file CompatExecutorSmokeHarness.h
 */
#pragma once

#include "../../mock/MockLedger.h"
#include "../../mock/MockTransactionalStorage.h"
#include "../../mock/MockTxPool.h"
#include "CompatTestFixture.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-framework/executor/NativeExecutionMessage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-table/src/StateStorageFactory.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-task/Wait.h"
#include "executor/TransactionExecutorFactory.h"
#include <boost/algorithm/hex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <future>
#include <memory>
#include <string>

namespace bcos::test
{
using bcos::executor::NativeExecutionMessage;
using bcos::executor::NativeExecutionMessageFactory;

struct ExecutorSmokeEnv
{
    std::shared_ptr<executor::TransactionExecutor> executor;
    std::shared_ptr<MockTransactionalStorage> storage;
};

struct CompatExecutorSmokeFixture
{
    CompatExecutorSmokeFixture()
    {
        boost::log::core::get()->set_logging_enabled(false);
        hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
        txpool = std::make_shared<MockTxPool>();
        backend = std::make_shared<MockTransactionalStorage>(hashImpl);
        ledger = std::make_shared<MockLedger>();
        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
        auto lruStorage = std::make_shared<storage::LRUStateStorage>(backend, false);
        auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
        executor = executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage, backend,
            executionResultFactory, stateStorageFactory, hashImpl, false);

        keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
        const auto secretKeyBytes =
            fromHex("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        memcpy(keyPair->secretKey()->mutableData(), secretKeyBytes.data(), 32);
        const auto publicKeyBytes = fromHex(
            "ccd8de502ac45462767e649b462b5f4ca7eadd69c7e1f1b410bdf754359be29b1b88ffd79744"
            "03f56e250af52b25682014554f7b3297d6152401e85d426a06ae");
        memcpy(keyPair->publicKey()->mutableData(), publicKeyBytes.data(), 64);
    }

    ~CompatExecutorSmokeFixture() { boost::log::core::get()->set_logging_enabled(true); }

    ExecutorSmokeEnv buildExecutorWithFeatures(ledger::Features const& features)
    {
        auto storage = std::make_shared<MockTransactionalStorage>(hashImpl);
        task::syncWait(ledger::writeToStorage(features, *storage, 1));

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
        auto stateStorageFactory = std::make_shared<storage::StateStorageFactory>(0);
        auto lruStorage = std::make_shared<storage::LRUStateStorage>(storage, false);
        auto exec = executor::TransactionExecutorFactory::build(ledger, txpool, lruStorage, storage,
            executionResultFactory, stateStorageFactory, hashImpl, false);
        return {std::move(exec), std::move(storage)};
    }

    void advanceBlock(
        ExecutorSmokeEnv& env, protocol::BlockNumber number, ledger::Features const& features)
    {
        task::syncWait(ledger::writeToStorage(features, *env.storage, number));
        nextBlockHeader(*env.executor, number);
    }

    void nextBlockHeader(executor::TransactionExecutor& exec, protocol::BlockNumber number)
    {
        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
        blockHeader->setNumber(number);
        bcos::protocol::ParentInfo parentInfo{{number - 1, h256(number - 1)}};
        blockHeader->setParentInfo(parentInfo);
        ledger->setBlockNumber(number - 1);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());
        std::promise<void> nextPromise;
        exec.nextBlockHeader(0, blockHeader, [&](Error::Ptr&& error) {
            BOOST_CHECK(!error);
            nextPromise.set_value();
        });
        nextPromise.get_future().get();
    }

    protocol::ExecutionMessage::UniquePtr dmcExecute(
        executor::TransactionExecutor& exec, NativeExecutionMessage::UniquePtr params)
    {
        std::promise<protocol::ExecutionMessage::UniquePtr> executePromise;
        exec.dmcExecuteTransaction(std::move(params),
            [&](Error::UniquePtr&& error, protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        return executePromise.get_future().get();
    }

    static constexpr const char* helloWorldCreationBin =
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

    static constexpr const char* mcopyContractBin =
        "6080604052348015600e575f80fd5b5060b980601a5f395ff3fe6080604052348015600e575f80fd5b50600436"
        "106026575f3560e01c80632dbaeee914602a575b5f80fd5b60306044565b604051603b9190606c565b60405180"
        "910390f35b5f60506020526020805f5e5f51905090565b5f819050919050565b6066816056565b82525050565b"
        "5f602082019050607d5f830184605f565b9291505056fea2646970667358221220c16107fa00317d2d630d4d01"
        "9754eb2bae42e96482d0050308e60ec21c69d7eb64736f6c63430008190033";

    std::shared_ptr<executor::TransactionExecutor> executor;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockTransactionalStorage> backend;
    std::shared_ptr<MockLedger> ledger;
    std::shared_ptr<crypto::Keccak256> hashImpl;
    crypto::KeyPairInterface::Ptr keyPair;
    int64_t gas = 3'000'000;
};

}  // namespace bcos::test
