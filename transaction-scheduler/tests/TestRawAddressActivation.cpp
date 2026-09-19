/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file TestRawAddressActivation.cpp
 * @brief Scheduler-level e2e for mid-chain activation of the raw-address hex
 *        fallback (feature_raw_address).
 *
 * Unlike the sibling BaselineScheduler tests, this TU deliberately defines NO
 * getLedgerConfig tag_invoke stub, so BaselineScheduler's per-block
 * ledger::getLedgerConfig(view, number, blockFactory) resolves to the REAL
 * storage2 implementation (bcos-ledger LedgerMethods.h), which loads the
 * features through Features::readFromStorage — SYS_CONFIG rows with an
 * enableNumber, exactly what the governance setSystemConfig path persists.
 * That is the difference this test exists to pin: the fallback arms only when
 * the activation block is filled by the real storage-load path (a bare
 * set()/setActivationBlock fixture would bypass the mechanism under test).
 *
 * Scenario: block 100 executes with the flag off and writes an account
 * balance into the legacy "/apps/<40-hex>" table; the governance row
 * (enableNumber = 101) then lands in the committed state; block 101 executes and
 * its ledgerConfig must report BinaryWithHexFallback with
 * activationBlockOf(feature_raw_address) == 101, and the balance written at
 * block 100 must be readable through the fallback.
 *
 * Compiled standalone (SKIP_UNITY_BUILD_INCLUSION): it pulls bcos-ledger
 * LedgerMethods.h, whose namespace-scope entities collide with the other
 * unity-merged TUs — same reason as TestEthereumExecutorScheduler.cpp.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

using RAMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
struct RABackendStorage
  : memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>
{
};
using RACheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, RABackendStorage>;
using RAMultiLayerStorage = MultiLayerStorage<RAMutableStorage, void, RACheckpointBackend>;

/// Probe scheduler: drives the account through EVMAccount with the mode derived
/// from the ledgerConfig BaselineScheduler hands in — the exact derivation the
/// production executor performs — and records what it saw for the test to assert.
struct RAProbingScheduler
{
    evmc_address m_account{};
    protocol::BlockNumber m_activationBlock{};
    u256 m_balance{};

    std::vector<std::pair<protocol::BlockNumber, ledger::account::AddressTableMode>> m_modes;
    std::optional<protocol::BlockNumber> m_rawAddressActivation;
    std::optional<u256> m_fallbackBalance;
    std::optional<u256> m_binaryOnlyBalance;

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        auto const mode = ledger::account::accountTableMode(ledgerConfig.features());
        m_modes.emplace_back(blockHeader.number(), mode);
        ledger::account::EVMAccount account(storage, m_account, mode);
        if (blockHeader.number() < m_activationBlock)
        {
            // Pre-activation block: feature_raw_address is off, so the mode must be Hex
            // and the balance lands in the legacy "/apps/<40-hex>" table — the data a
            // mid-chain activation leaves behind.
            if (!co_await account.exists())
            {
                co_await account.create();
            }
            co_await account.setBalance(m_balance);
        }
        else
        {
            // Post-activation block: record the activation block the real
            // readFromStorage filled, and read the pre-activation balance back.
            m_rawAddressActivation = ledgerConfig.features().activationBlockOf(
                ledger::Features::Flag::feature_raw_address);
            m_fallbackBalance = co_await account.balance();
            // Control: without the fallback (plain Binary) the hex row is invisible.
            ledger::account::EVMAccount binaryOnly(
                storage, m_account, ledger::account::AddressTableMode::Binary);
            m_binaryOnlyBalance = co_await binaryOnly.balance();
        }

        auto receipts = ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
                        ::ranges::views::transform([](size_t) -> protocol::TransactionReceipt::Ptr {
                            auto receipt =
                                std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
                            constexpr static std::string_view str = "abc";
                            auto& inner = receipt->inner();
                            inner.dataHash.assign(str.begin(), str.end());
                            inner.data.gasUsed = "100";
                            return receipt;
                        }) |
                        ::ranges::to<std::vector<protocol::TransactionReceipt::Ptr>>();
        co_return receipts;
    }
};

struct RAExecutor
{
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& /*storage*/,
        protocol::BlockHeader const& /*blockHeader*/, protocol::Transaction const& /*transaction*/,
        int /*contextID*/, ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
    {
        co_return {};
    }
    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
    };
    auto createExecuteContext(auto& storage, protocol::BlockHeader const& /*blockHeader*/,
        protocol::Transaction const& /*transaction*/, int32_t /*contextID*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};

task::Task<std::vector<protocol::Transaction::ConstPtr>> emptyTxsTaskRA()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> emptySystemConfigsTaskRA()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> emptyFeaturesTaskRA()
{
    co_return ledger::Features{};
}

class RawAddressActivationFixture
{
public:
    RawAddressActivationFixture()
      : checkpointBackend(backendStorage),
        cryptoSuite(std::make_shared<crypto::CryptoSuite>(
            std::make_shared<crypto::Keccak256>(), nullptr, nullptr)),
        blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory)),
        transactionSubmitResultFactory(
            std::make_shared<protocol::TransactionSubmitResultFactoryImpl>()),
        multiLayerStorage(checkpointBackend),
        baselineScheduler(multiLayerStorage, probingScheduler, mockExecutor, *blockFactory,
            mockLedger.get(), mockTxPool.get(), *transactionSubmitResultFactory, *hashImpl)
    {
        // The ledger commit path needs the same mocks as TestCommitSingleBatch.
        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo(
                [](storage::StorageInterface::Ptr storage, protocol::ConstTransactionsPtr,
                    protocol::Block::ConstPtr block,
                    std::function<void(std::string, Error::Ptr&&)> callback, bool,
                    std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>, bool) {
                    auto header = block->blockHeader();
                    auto blockNumberStr = boost::lexical_cast<std::string>(header->number());
                    auto hash = header->hash();
                    auto hashView =
                        std::string_view(reinterpret_cast<const char*>(hash.data()), hash.SIZE);

                    storage::Entry hashEntry;
                    hashEntry.set(hash.asBytes());
                    storage->asyncSetRow(ledger::SYS_NUMBER_2_HASH, blockNumberStr,
                        std::move(hashEntry), [](Error::UniquePtr) {});

                    storage::Entry hash2NumberEntry;
                    hash2NumberEntry.set(blockNumberStr);
                    storage->asyncSetRow(ledger::SYS_HASH_2_NUMBER, hashView,
                        std::move(hash2NumberEntry), [](Error::UniquePtr) {});

                    bytes headerBuffer;
                    header->encode(headerBuffer);
                    storage::Entry number2HeaderEntry;
                    number2HeaderEntry.set(std::move(headerBuffer));
                    storage->asyncSetRow(ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr,
                        std::move(number2HeaderEntry), [](Error::UniquePtr) {});

                    callback({}, nullptr);
                });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskRA();
        });
        fakeit::When(Method(mockLedger, asyncGetBlockNumber))
            .AlwaysDo([](std::function<void(Error::Ptr, protocol::BlockNumber)> callback) {
                callback(nullptr, -1);
            });
        fakeit::When(Method(mockLedger, asyncGetNodeListByType))
            .AlwaysDo([](std::string_view const&,
                          std::function<void(Error::Ptr, consensus::ConsensusNodeList)> callback) {
                callback(nullptr, {});
            });
        fakeit::When(Method(mockLedger, asyncGetBlockDataByNumber))
            .AlwaysDo([](protocol::BlockNumber, int32_t,
                          std::function<void(Error::Ptr, protocol::Block::Ptr)> callback) {
                callback(nullptr, nullptr);
            });
        fakeit::When(Method(mockLedger, asyncGetBlockHashByNumber))
            .AlwaysDo([](protocol::BlockNumber,
                          std::function<void(Error::Ptr, crypto::HashType)> callback) {
                callback(nullptr, crypto::HashType{});
            });
        fakeit::When(Method(mockLedger, fetchAllSystemConfigs)).AlwaysDo([](protocol::BlockNumber) {
            return emptySystemConfigsTaskRA();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return emptyFeaturesTaskRA();
        });

        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    protocol::BlockHeader::Ptr executeOneBlock(protocol::BlockNumber number)
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto blockHeader = block->blockHeader();
        blockHeader->setNumber(number);
        blockHeader->setVersion(blockVersion);
        blockHeader->calculateHash(*hashImpl);
        bytes input;
        block->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, std::to_string(number), 100, "chain", "group", 0));

        Error::Ptr execError;
        protocol::BlockHeader::Ptr executedHeader;
        baselineScheduler.executeBlock(
            block, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr header, bool) {
                execError = std::move(error);
                executedHeader = std::move(header);
            });
        BOOST_REQUIRE_MESSAGE(
            !execError, "executeBlock failed: " + (execError ? execError->errorMessage() : ""));
        BOOST_REQUIRE(executedHeader);
        return executedHeader;
    }

    void commitOneBlock(protocol::BlockHeader::Ptr const& header)
    {
        Error::Ptr commitError;
        baselineScheduler.commitBlock(header,
            [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
        BOOST_REQUIRE_MESSAGE(!commitError,
            "commitBlock failed: " + (commitError ? commitError->errorMessage() : ""));
    }

    /// Persist one SYS_CONFIG feature row into the committed backend — the row the
    /// governance setSystemConfig transaction writes, with the flag taking effect
    /// from @p enableNumber on (readFromStorage activates a flag whose
    /// enableNumber <= the executing block's number).
    void writeFeatureEntry(std::string_view flagName, protocol::BlockNumber enableNumber)
    {
        task::syncWait([&]() -> task::Task<void> {
            storage::Entry entry;
            entry.set(storage::serialize::encode(ledger::SystemConfigEntry{"1", enableNumber}));
            co_await storage2::writeOne(
                backendStorage, StateKey{ledger::SYS_CONFIG, flagName}, std::move(entry));
        }());
    }

    std::optional<storage::Entry> backendRow(std::string_view table, std::string_view key)
    {
        return task::syncWait(storage2::readOne(backendStorage, StateKey{table, key}));
    }

    static constexpr uint32_t blockVersion = 200;

    RABackendStorage backendStorage;
    RACheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    RAProbingScheduler probingScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    RAMultiLayerStorage multiLayerStorage;
    RAExecutor mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), RAExecutor, RAProbingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestRawAddressActivation, RawAddressActivationFixture)

BOOST_AUTO_TEST_CASE(midChainActivationEnablesHexFallback)
{
    constexpr protocol::BlockNumber activationBlock = 101;
    probingScheduler.m_account = unhexAddress("0x4200000000000000000000000000000000001234");
    probingScheduler.m_activationBlock = activationBlock;
    probingScheduler.m_balance = u256(12345);
    std::string const hexTable = "/apps/4200000000000000000000000000000000001234";

    // Block 100: no SYS_CONFIG feature rows, so the real readFromStorage leaves
    // feature_raw_address off and the block executes in Hex mode, writing the
    // legacy hex table.
    auto header100 = executeOneBlock(100);
    BOOST_REQUIRE_EQUAL(probingScheduler.m_modes.size(), 1u);
    BOOST_CHECK_EQUAL(probingScheduler.m_modes[0].first, 100);
    BOOST_CHECK(probingScheduler.m_modes[0].second == ledger::account::AddressTableMode::Hex);
    commitOneBlock(header100);
    auto committedBalance = backendRow(hexTable, "balance");
    BOOST_REQUIRE(committedBalance.has_value());
    BOOST_CHECK_EQUAL(std::string(committedBalance->get()), "12345");

    // Governance activates the feature from block 101 on: the SYS_CONFIG row carries
    // enableNumber = 101, the shape SystemConfigPrecompiled persists.
    writeFeatureEntry("feature_raw_address", activationBlock);

    // Block 101: getLedgerConfig reloads the features from the committed SYS_CONFIG
    // rows — the readFromStorage path fills the activation block — so the fallback
    // arms and the pre-activation hex balance is visible again.
    auto header101 = executeOneBlock(101);
    BOOST_REQUIRE_EQUAL(probingScheduler.m_modes.size(), 2u);
    BOOST_CHECK_EQUAL(probingScheduler.m_modes[1].first, activationBlock);
    BOOST_CHECK(probingScheduler.m_modes[1].second ==
                ledger::account::AddressTableMode::BinaryWithHexFallback);
    BOOST_REQUIRE(probingScheduler.m_rawAddressActivation.has_value());
    BOOST_CHECK_EQUAL(*probingScheduler.m_rawAddressActivation, activationBlock);
    BOOST_REQUIRE(probingScheduler.m_fallbackBalance.has_value());
    BOOST_CHECK_EQUAL(*probingScheduler.m_fallbackBalance, u256(12345));
    // The control proves the fallback did the work, not the binary table.
    BOOST_REQUIRE(probingScheduler.m_binaryOnlyBalance.has_value());
    BOOST_CHECK_EQUAL(*probingScheduler.m_binaryOnlyBalance, u256(0));
}

// Gate complement: a chain born with feature_raw_address (enableNumber = 0, the
// genesis-activation shape) has no pre-activation hex data, so it stays plain
// Binary — no fallback reads, even though the feature is on from the first block.
BOOST_AUTO_TEST_CASE(genesisActivationKeepsBinary)
{
    probingScheduler.m_account = unhexAddress("0x4200000000000000000000000000000000005678");
    // m_activationBlock = 0: every block takes the post-activation (read) branch.
    probingScheduler.m_activationBlock = 0;

    writeFeatureEntry("feature_raw_address", 0);

    auto header100 = executeOneBlock(100);
    BOOST_REQUIRE_EQUAL(probingScheduler.m_modes.size(), 1u);
    BOOST_CHECK(probingScheduler.m_modes[0].second == ledger::account::AddressTableMode::Binary);
    // The activation block IS recorded — it is 0, not a missing activation context,
    // that keeps the fallback off.
    BOOST_REQUIRE(probingScheduler.m_rawAddressActivation.has_value());
    BOOST_CHECK_EQUAL(*probingScheduler.m_rawAddressActivation, 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
