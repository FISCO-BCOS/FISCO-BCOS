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
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "executive/BlockContext.h"
#include "executive/TransactionExecutive.h"
#include "executor/TransactionExecutorFactory.h"
#include "mock/MockKeyPageStorage.h"
#include "mock/MockLedger.h"
#include "mock/MockTransactionalStorage.h"
#include "mock/MockTxPool.h"
#include "precompiled/extension/UserPrecompiled.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-framework/storage/Table.h>
#include <bcos-tars-protocol/testutil/FakeBlock.h>
#include <bcos-tars-protocol/testutil/FakeBlockHeader.h>
#include <bcos-tool/BfsFileFactory.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <libinitializer/AuthInitializer.h>
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
    void setIsWasm(bool _isWasm, bool _isCheckAuth = false, bool _isKeyPage = true,
        protocol::BlockVersion version = BlockVersion::V3_1_VERSION)
    {
        isWasm = _isWasm;
        if (_isKeyPage)
        {
            storage = std::make_shared<MockKeyPageStorage>(hashImpl);
        }
        else
        {
            storage = std::make_shared<MockTransactionalStorage>(hashImpl);
        }
        blockFactory = createBlockFactory(cryptoSuite);
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader(1);
        header->setNumber(1);
        ledger = std::make_shared<MockLedger>();
        ledger->setBlockNumber(header->number() - 1);

        auto executionResultFactory = std::make_shared<NativeExecutionMessageFactory>();

        executor = bcos::executor::TransactionExecutorFactory::build(ledger, txpool, nullptr,
            storage, executionResultFactory, hashImpl, _isWasm, _isCheckAuth, false);

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
        boost::log::core::get()->set_logging_enabled(false);
        createSysTable(version);
        boost::log::core::get()->set_logging_enabled(true);
        if (_isCheckAuth)
        {
            boost::log::core::get()->set_logging_enabled(false);
            deployAuthSolidity(1);
            boost::log::core::get()->set_logging_enabled(true);
        }
    }

    void createSysTable(protocol::BlockVersion version)
    {
        // create sys table
        {
            std::promise<std::optional<Table>> promise1;
            storage->asyncCreateTable(std::string{ledger::SYS_CONFIG}, "value",
                [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                    BOOST_CHECK(!_error);
                    promise1.set_value(std::move(_table));
                });
            auto table = promise1.get_future().get();
            auto entry = table->newEntry();

            entry.setObject(SystemConfigEntry{"3000000", 0});

            table->setRow(SYSTEM_KEY_TX_GAS_LIMIT, std::move(entry));
        }

        m_blockVersion = version;
        if (m_blockVersion >= protocol::BlockVersion::V3_1_VERSION)
        {
            initBfs(1);
        }
        else
        {
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
                newSubMap.insert(std::make_pair("apps", executor::FS_TYPE_DIR));
                newSubMap.insert(std::make_pair("sys", executor::FS_TYPE_DIR));
                newSubMap.insert(std::make_pair("tables", executor::FS_TYPE_DIR));
                tEntry.importFields({executor::FS_TYPE_DIR});
                newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
                aclTypeEntry.importFields({"0"});
                aclWEntry.importFields({""});
                aclBEntry.importFields({""});
                extraEntry.importFields({""});
                rootTable->setRow(executor::FS_KEY_TYPE, std::move(tEntry));
                rootTable->setRow(executor::FS_KEY_SUB, std::move(newSubEntry));
                rootTable->setRow(executor::FS_ACL_TYPE, std::move(aclTypeEntry));
                rootTable->setRow(executor::FS_ACL_WHITE, std::move(aclWEntry));
                rootTable->setRow(executor::FS_ACL_BLACK, std::move(aclBEntry));
                rootTable->setRow(executor::FS_KEY_EXTRA, std::move(extraEntry));
            }

            // create /tables table
            {
                std::promise<std::optional<Table>> promise3;
                storage->asyncCreateTable("/tables", "value",
                    [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                        BOOST_CHECK(!_error);
                        promise3.set_value(std::move(_table));
                    });
                auto tablesTable = promise3.get_future().get();
                storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
                std::map<std::string, std::string> newSubMap;
                tEntry.importFields({executor::FS_TYPE_DIR});
                newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
                aclTypeEntry.importFields({"0"});
                aclWEntry.importFields({""});
                aclBEntry.importFields({""});
                extraEntry.importFields({""});
                tablesTable->setRow(executor::FS_KEY_TYPE, std::move(tEntry));
                tablesTable->setRow(executor::FS_KEY_SUB, std::move(newSubEntry));
                tablesTable->setRow(executor::FS_ACL_TYPE, std::move(aclTypeEntry));
                tablesTable->setRow(executor::FS_ACL_WHITE, std::move(aclWEntry));
                tablesTable->setRow(executor::FS_ACL_BLACK, std::move(aclBEntry));
                tablesTable->setRow(executor::FS_KEY_EXTRA, std::move(extraEntry));
            }

            // create /apps table
            {
                std::promise<std::optional<Table>> promise4;
                storage->asyncCreateTable("/apps", "value",
                    [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                        BOOST_CHECK(!_error);
                        promise4.set_value(std::move(_table));
                    });
                auto appsTable = promise4.get_future().get();
                storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
                std::map<std::string, std::string> newSubMap;
                tEntry.importFields({executor::FS_TYPE_DIR});
                newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
                aclTypeEntry.importFields({"0"});
                aclWEntry.importFields({""});
                aclBEntry.importFields({""});
                extraEntry.importFields({""});
                appsTable->setRow(executor::FS_KEY_TYPE, std::move(tEntry));
                appsTable->setRow(executor::FS_KEY_SUB, std::move(newSubEntry));
                appsTable->setRow(executor::FS_ACL_TYPE, std::move(aclTypeEntry));
                appsTable->setRow(executor::FS_ACL_WHITE, std::move(aclWEntry));
                appsTable->setRow(executor::FS_ACL_BLACK, std::move(aclBEntry));
                appsTable->setRow(executor::FS_KEY_EXTRA, std::move(extraEntry));
            }

            // create /usr table
            {
                std::promise<std::optional<Table>> promise4;
                storage->asyncCreateTable(
                    "/sys", "value", [&](Error::UniquePtr&& _error, std::optional<Table>&& _table) {
                        BOOST_CHECK(!_error);
                        promise4.set_value(std::move(_table));
                    });
                auto appsTable = promise4.get_future().get();
                storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
                std::map<std::string, std::string> newSubMap;
                tEntry.importFields({executor::FS_TYPE_DIR});
                newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
                aclTypeEntry.importFields({"0"});
                aclWEntry.importFields({""});
                aclBEntry.importFields({""});
                extraEntry.importFields({""});
                appsTable->setRow(executor::FS_KEY_TYPE, std::move(tEntry));
                appsTable->setRow(executor::FS_KEY_SUB, std::move(newSubEntry));
                appsTable->setRow(executor::FS_ACL_TYPE, std::move(aclTypeEntry));
                appsTable->setRow(executor::FS_ACL_WHITE, std::move(aclWEntry));
                appsTable->setRow(executor::FS_ACL_BLACK, std::move(aclBEntry));
                appsTable->setRow(executor::FS_KEY_EXTRA, std::move(extraEntry));
            }
        }
    }

    void nextBlock(
        int64_t blockNumber, protocol::BlockVersion version = protocol::BlockVersion::V3_1_VERSION)
    {
        std::cout << "next block: " << blockNumber << std::endl;
        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(cryptoSuite,
            [m_blockHeader = bcostars::BlockHeader()]() mutable { return &m_blockHeader; });
        blockHeader->setNumber(blockNumber);
        blockHeader->setParentInfo({{blockNumber - 1, h256(blockNumber - 1)}});
        blockHeader->setVersion((uint32_t)version);
        ledger->setBlockNumber(blockNumber - 1);
        std::promise<void> nextPromise;
        executor->nextBlockHeader(
            0, blockHeader, [&](bcos::Error::Ptr&& error) { nextPromise.set_value(); });
        nextPromise.get_future().get();
    }

    void commitBlock(protocol::BlockNumber blockNumber)
    {
        std::cout << "commit block: " << blockNumber << std::endl;
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

    void deployAuthSolidity(protocol::BlockNumber blockNumber)
    {
        static const char* committeeBin = bcos::initializer::committeeBin;

        // deploy CommitteeManager

        // hex bin code to bytes
        bytes code;
        boost::algorithm::unhex(committeeBin, std::back_inserter(code));

        // constructor (address[] initGovernors,    = [authAdminAddress]
        //        uint32[] memory weights,          = [0]
        //        uint8 participatesRate,           = 0
        //        uint8 winRate)                    = 0
        std::vector<Address> initGovernors({Address(admin)});
        std::vector<string32> weights({bcos::codec::toString32(h256(uint8_t(1)))});
        // bytes code + abi encode constructor params
        codec::abi::ContractABICodec abi(hashImpl);
        bytes constructor = abi.abiIn("", initGovernors, weights,
            codec::toString32(h256(uint8_t(0))), codec::toString32(h256(uint8_t(0))));
        bytes input = code + constructor;

        auto sender = admin;

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(precompiled::AUTH_COMMITTEE_ADDRESS);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::MESSAGE);
        params->setCreate(true);
        params->setData(input);

        nextBlock(blockNumber);
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

        result->setSeq(1001);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });

        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1000);
        /// callback get deploy committeeManager context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise3;
        executor->dmcExecuteTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });

        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->create(), true);
        BOOST_CHECK_EQUAL(result3->origin(), sender);
        BOOST_CHECK_EQUAL(result3->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result3->setSeq(1002);
        result3->setTo("1111111111111111111111111111111111111111");
        result3->setKeyLocks({});
        /// new committee
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise4;
        executor->dmcExecuteTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });

        auto result4 = executePromise4.get_future().get();

        result4->setSeq(1003);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise5;
        executor->dmcExecuteTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });

        auto result5 = executePromise5.get_future().get();

        result5->setSeq(1002);
        /// callback get deploy committee context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise6;
        executor->dmcExecuteTransaction(
            std::move(result5), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise6.set_value(std::move(result));
            });

        auto result6 = executePromise6.get_future().get();

        BOOST_CHECK_EQUAL(result6->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result6->contextID(), 99);
        BOOST_CHECK_EQUAL(result6->seq(), 1002);
        BOOST_CHECK_EQUAL(result6->create(), false);
        BOOST_CHECK_EQUAL(
            result6->newEVMContractAddress(), "1111111111111111111111111111111111111111");
        BOOST_CHECK_EQUAL(result6->origin(), sender);
        BOOST_CHECK_EQUAL(result6->to(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result6->setSeq(1000);
        // new committee address => committeeManager
        // new proposalManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise7;
        executor->dmcExecuteTransaction(
            std::move(result6), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise7.set_value(std::move(result));
            });

        auto result7 = executePromise7.get_future().get();

        BOOST_CHECK_EQUAL(result7->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result7->contextID(), 99);
        BOOST_CHECK_EQUAL(result7->seq(), 1000);
        BOOST_CHECK_EQUAL(result7->create(), true);
        BOOST_CHECK_EQUAL(result7->origin(), sender);
        BOOST_CHECK_EQUAL(result7->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        result7->setSeq(1004);
        result7->setTo("2222222222222222222222222222222222222222");
        result7->setKeyLocks({});

        // new proposalManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise8;
        executor->dmcExecuteTransaction(
            std::move(result7), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise8.set_value(std::move(result));
            });

        auto result8 = executePromise8.get_future().get();

        result8->setSeq(1005);
        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise9;
        executor->dmcExecuteTransaction(
            std::move(result8), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise9.set_value(std::move(result));
            });

        auto result9 = executePromise9.get_future().get();

        result9->setSeq(1004);
        /// callback get deploy committee context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise10;
        executor->dmcExecuteTransaction(
            std::move(result9), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise10.set_value(std::move(result));
            });

        auto result10 = executePromise10.get_future().get();

        result10->setSeq(1006);
        result10->setTo("3333333333333333333333333333333333333333");
        result10->setKeyLocks({});

        /// new voteComputer
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise11;
        executor->dmcExecuteTransaction(
            std::move(result10), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise11.set_value(std::move(result));
            });

        auto result11 = executePromise11.get_future().get();

        result11->setSeq(1007);

        /// call precompiled to get deploy auth
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise12;
        executor->dmcExecuteTransaction(
            std::move(result11), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise12.set_value(std::move(result));
            });

        auto result12 = executePromise12.get_future().get();

        result12->setSeq(1006);

        /// callback get deploy proposalMgr context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise13;
        executor->dmcExecuteTransaction(
            std::move(result12), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise13.set_value(std::move(result));
            });

        auto result13 = executePromise13.get_future().get();

        BOOST_CHECK_EQUAL(result13->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result13->contextID(), 99);
        BOOST_CHECK_EQUAL(result13->seq(), 1006);
        BOOST_CHECK_EQUAL(result13->create(), false);
        BOOST_CHECK_EQUAL(result13->origin(), sender);
        BOOST_CHECK_EQUAL(result13->to(), "1111111111111111111111111111111111111111");

        /// call to committee, getWeights
        result13->setSeq(1008);
        // to committee
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise14;
        executor->dmcExecuteTransaction(
            std::move(result13), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise14.set_value(std::move(result));
            });
        auto result14 = executePromise14.get_future().get();

        result14->setSeq(1006);

        // committee callback to voteComputer
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise15;
        executor->dmcExecuteTransaction(
            std::move(result14), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise15.set_value(std::move(result));
            });
        auto result15 = executePromise15.get_future().get();

        result15->setSeq(1004);

        // new voteCompute => new ProposalMgr context
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise16;
        executor->dmcExecuteTransaction(
            std::move(result15), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise16.set_value(std::move(result));
            });

        auto result16 = executePromise16.get_future().get();

        BOOST_CHECK_EQUAL(result16->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result16->contextID(), 99);
        BOOST_CHECK_EQUAL(result16->seq(), 1004);
        BOOST_CHECK_EQUAL(result16->create(), false);
        BOOST_CHECK_EQUAL(result16->origin(), sender);
        BOOST_CHECK_EQUAL(
            result16->newEVMContractAddress(), "2222222222222222222222222222222222222222");
        BOOST_CHECK_EQUAL(result16->to(), AUTH_COMMITTEE_ADDRESS);

        result16->setSeq(1000);
        // new proposalManager address => committeeManager
        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise17;
        executor->dmcExecuteTransaction(
            std::move(result16), [&](bcos::Error::UniquePtr&& error,
                                     bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise17.set_value(std::move(result));
            });

        auto result17 = executePromise17.get_future().get();
        BOOST_CHECK_EQUAL(result17->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result17->contextID(), 99);
        BOOST_CHECK_EQUAL(result17->seq(), 1000);
        BOOST_CHECK_EQUAL(result17->create(), false);
        BOOST_CHECK_EQUAL(result17->origin(), sender);
        BOOST_CHECK_EQUAL(result17->from(), precompiled::AUTH_COMMITTEE_ADDRESS);

        commitBlock(blockNumber);
    }

    ExecutionMessage::UniquePtr list(
        protocol::BlockNumber _number, std::string const& path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("list(string)", path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? BFS_NAME : BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            std::vector<BfsTuple> empty;
            BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode), empty));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr initBfs(protocol::BlockNumber _number, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("initBfs()");
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? BFS_NAME : BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

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

protected:
    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    protocol::BlockFactory::Ptr blockFactory;
    CryptoSuite::Ptr cryptoSuite = nullptr;
    CryptoSuite::Ptr smCryptoSuite = nullptr;

    TransactionalStorageInterface::Ptr storage;
    TransactionExecutor::Ptr executor;
    std::shared_ptr<MockTxPool> txpool;
    std::shared_ptr<MockLedger> ledger;
    KeyPairInterface::Ptr keyPair;

    CodecWrapper::Ptr codec;
    int64_t gas = MockLedger::TX_GAS_LIMIT;
    bool isWasm = false;
    std::string admin = "1111654b49838bd3e9466c85a4cc3428c9601111";
    protocol::BlockVersion m_blockVersion = protocol::BlockVersion::V3_1_VERSION;
};
}  // namespace bcos::test
