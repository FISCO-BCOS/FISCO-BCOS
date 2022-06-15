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
 * @file PreCompiledFixture.h
 * @author: kyonRay
 * @date 2021-06-19
 */

#pragma once
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-protocol/testutils/protocol/FakeBlock.h"
#include "bcos-protocol/testutils/protocol/FakeBlockHeader.h"
#include "executive/BlockContext.h"
#include "executive/TransactionExecutive.h"
#include "executor/TransactionExecutorFactory.h"
#include "mock/MockLedger.h"
#include "mock/MockTransactionalStorage.h"
#include "mock/MockTxPool.h"
#include "precompiled/extension/UserPrecompiled.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2.h>
#include <bcos-framework/interfaces/executor/NativeExecutionMessage.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <string>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;

namespace bcos::test
{
class PrecompiledFixture : public TestPromptFixture
{
public:
    PrecompiledFixture()
    {
        hashImpl = std::make_shared<Keccak256>();
        assert(hashImpl);
        smHashImpl = std::make_shared<SM3>();
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        auto sm2Sign = std::make_shared<SM2Crypto>();
        assert(signatureImpl);
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        assert(cryptoSuite);
        smCryptoSuite = std::make_shared<CryptoSuite>(smHashImpl, sm2Sign, nullptr);
        txpool = std::make_shared<MockTxPool>();
        assert(DEFAULT_PERMISSION_ADDRESS);
    }

    virtual ~PrecompiledFixture() {}

    /// must set isWasm
    void setIsWasm(bool _isWasm, bool _isCheckAuth = false)
    {
        isWasm = _isWasm;
        storage = std::make_shared<MockTransactionalStorage>(hashImpl);
        memoryTableFactory = std::make_shared<StateStorage>(storage);

        blockFactory = createBlockFactory(cryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);
        ledger = std::make_shared<MockLedger>();
        ledger->setBlockNumber(header->number() - 1);

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        executor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, nullptr,
            storage, executionResultFactory, hashImpl, _isWasm, _isCheckAuth, false);

        createSysTable();
        codec = std::make_shared<CodecWrapper>(hashImpl, _isWasm);
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
    }

    void setSM(bool _isWasm)
    {
        isWasm = _isWasm;
        storage = std::make_shared<MockTransactionalStorage>(smHashImpl);
        memoryTableFactory = std::make_shared<StateStorage>(storage);

        blockFactory = createBlockFactory(smCryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();
        executor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, nullptr,
            storage, executionResultFactory, smHashImpl, _isWasm, false, false);

        createSysTable();
        codec = std::make_shared<CodecWrapper>(smHashImpl, _isWasm);

        keyPair = smCryptoSuite->signatureImpl()->generateKeyPair();
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
    }

    void createSysTable()
    {
        // create sys table
        {
            std::promise<std::optional<Table>> promise1;
            storage->asyncCreateTable(ledger::SYS_CONFIG, "value",
                [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                    BOOST_CHECK(!_error);
                    promise1.set_value(std::move(_table));
                });
            auto table = promise1.get_future().get();
            auto entry = table->newEntry();

            entry.setObject(SystemConfigEntry{"3000000", 0});

            table->setRow(SYSTEM_KEY_TX_GAS_LIMIT, std::move(entry));
        }

        // create / table
        {
            std::promise<std::optional<Table>> promise2;
            storage->asyncCreateTable(
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
            storage->asyncCreateTable(
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
            storage->asyncCreateTable(
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

    void nextBlock(int64_t blockNumber)
    {
        auto blockHeader = std::make_shared<bcos::protocol::PBBlockHeader>(cryptoSuite);
        blockHeader->setNumber(blockNumber);
        ledger->setBlockNumber(blockNumber - 1);
        std::promise<void> nextPromise;
        executor->nextBlockHeader(
            0, blockHeader, [&](bcos::Error::Ptr&& error) { nextPromise.set_value(); });
        nextPromise.get_future().get();
    }

    void commitBlock(protocol::BlockNumber blockNumber)
    {
        TwoPCParams commitParams{};
        commitParams.number = blockNumber;

        std::promise<void> preparePromise;
        executor->prepare(
            commitParams, [&](bcos::Error::Ptr&& error) { preparePromise.set_value(); });
        preparePromise.get_future().get();

        std::promise<void> commitPromise;
        executor->commit(
            commitParams, [&](bcos::Error::Ptr&& error) { commitPromise.set_value(); });
        commitPromise.get_future().get();
    }

protected:
    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    protocol::BlockFactory::Ptr blockFactory;
    CryptoSuite::Ptr cryptoSuite = nullptr;
    CryptoSuite::Ptr smCryptoSuite = nullptr;

    std::shared_ptr<MockTransactionalStorage> storage;
    StateStorage::Ptr memoryTableFactory;
    TransactionExecutor::Ptr executor;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockLedger> ledger;
    KeyPairInterface::Ptr keyPair;

    CodecWrapper::Ptr codec;
    int64_t gas = 300000000;
    bool isWasm = false;
};
}  // namespace bcos::test
