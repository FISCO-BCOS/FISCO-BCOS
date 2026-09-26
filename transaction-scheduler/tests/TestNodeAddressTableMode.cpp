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
 * @file TestNodeAddressTableMode.cpp
 * @brief Scheduler-level e2e for the node-local account-table encoding
 *        (ledger::account::nodeAddressTableMode), the successor of the
 *        feature-gated raw-address activation test.
 *
 * The encoding is a node-local physical layout now: a block's executor derives
 * the mode from the process-wide singleton (set once at startup by the
 * storage-layout detection), NOT from the block's feature set. This test drives
 * blocks through a real BaselineScheduler whose probing scheduler uses exactly
 * the production derivation (nodeAddressTableMode()), and flips the singleton
 * between blocks the way a node restart after migration would:
 *
 *   - encoding-agnostic root: the same committed state migrated in place from
 *     hex to binary table names folds the SAME xorStateRoot, and a Binary-mode
 *     block then reads the migrated rows directly. There is no runtime mixed
 *     mode: a node is all-hex or all-binary, and the one-shot boot-time
 *     migration (libinitializer/AccountTableMigration) is the only way between.
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
#include "bcos-framework/testutils/ScopedNodeAddressTableMode.h"
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
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
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

/// Probe scheduler: drives the account through EVMAccount with the mode taken from the
/// node-local singleton — the exact derivation the production executor performs — and
/// records what it saw for the test to assert. Blocks below m_writeBlock write m_balance;
/// blocks from m_writeBlock on read the balance back (recording it) and write m_newBalance.
struct NMProbingScheduler
{
    evmc_address m_account{};
    protocol::BlockNumber m_writeBlock{};
    u256 m_balance{};
    u256 m_newBalance{};

    std::vector<std::pair<protocol::BlockNumber, ledger::account::AddressTableMode>> m_modes;
    std::optional<u256> m_readBackBalance;

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        auto const mode = ledger::account::nodeAddressTableMode();
        m_modes.emplace_back(blockHeader.number(), mode);
        ledger::account::EVMAccount account(storage, m_account, mode);
        if (blockHeader.number() < m_writeBlock)
        {
            // Unmigrated-layout block: the node is in Hex mode and the balance lands in the
            // legacy "/apps/<40-hex>" table — the data a migration leaves behind until the
            // account is touched.
            if (!co_await account.exists())
            {
                co_await account.create();
            }
            co_await account.setBalance(m_balance);
        }
        else
        {
            m_readBackBalance = co_await account.balance();
            co_await account.setBalance(m_newBalance);
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
        ledger::LedgerConfig const& /*ledgerConfig*/,
        bool /*call*/) -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
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

class NodeAddressTableModeFixture
{
public:
    NodeAddressTableModeFixture()
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

    std::optional<storage::Entry> backendRow(std::string_view table, std::string_view key)
    {
        return task::syncWait(storage2::readOne(backendStorage, StateKey{table, key}));
    }

    h256 committedXorRoot()
    {
        ledger::Features features;
        return task::syncWait(xorStateRoot(backendStorage, blockVersion, *hashImpl, features));
    }

    /// What the migration tool does, row by row: rewrite every row of the hex account
    /// table (and its s_tables registration) under the binary table name, then delete the
    /// hex originals.
    void migrateAccountRowsToBinary(std::string_view hexTable)
    {
        auto const binTable = ledger::account::hexToBinaryAccountTableName(hexTable);
        BOOST_REQUIRE(!binTable.empty());
        task::syncWait([&]() -> task::Task<void> {
            auto range = co_await storage2::range(backendStorage);
            std::vector<std::pair<std::string, storage::Entry>> rows;
            while (auto keyValue = co_await range.next())
            {
                auto& [key, value] = *keyValue;
                auto* entry = std::get_if<storage::Entry>(std::addressof(value));
                if (entry == nullptr)
                {
                    continue;
                }
                StateKeyView view(key);
                auto&& [table, rowKey] = view.get();
                if (table == hexTable)
                {
                    rows.emplace_back(std::string(rowKey), *entry);
                }
            }
            for (auto& [rowKey, entry] : rows)
            {
                co_await storage2::writeOne(
                    backendStorage, StateKey{binTable, rowKey}, std::move(entry));
                co_await storage2::removeOne(backendStorage, StateKey{hexTable, rowKey});
            }
            co_await storage2::writeOne(backendStorage, StateKey{ledger::SYS_TABLES, binTable},
                storage::Entry{std::string_view{"value"}});
            co_await storage2::removeOne(backendStorage, StateKey{ledger::SYS_TABLES, hexTable});
        }());
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

    NMProbingScheduler probingScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    RAMultiLayerStorage multiLayerStorage;
    RAExecutor mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), RAExecutor, NMProbingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestNodeAddressTableMode, NodeAddressTableModeFixture)

// The XOR state root is a function of the logical state alone: the state block 100
// commits in the hex layout folds the same root after the account rows are migrated in
// place to the binary layout — and a Binary-mode node then reads the migrated rows
// directly (no runtime fallback exists anymore).
BOOST_AUTO_TEST_CASE(xorRootConsistentAfterInPlaceMigration)
{
    namespace account = ledger::account;
    // The guard restores the pre-test mode on the way out; the explicit flips below
    // mimic a node restart after migration, as the file header describes.
    bcos::test::ScopedNodeAddressTableMode const modeGuard(account::AddressTableMode::Hex);
    probingScheduler.m_account = unhexAddress("0x4200000000000000000000000000000000005678");
    probingScheduler.m_writeBlock = 101;
    probingScheduler.m_balance = u256(12345);
    probingScheduler.m_newBalance = u256(54321);
    std::string const hexTable = "/apps/4200000000000000000000000000000000005678";
    std::string const binTable = account::hexToBinaryAccountTableName(hexTable);
    BOOST_REQUIRE(!binTable.empty());

    commitOneBlock(executeOneBlock(100));
    auto const rootHex = committedXorRoot();
    BOOST_CHECK_NE(rootHex, h256{});

    migrateAccountRowsToBinary(hexTable);
    auto const rootBinary = committedXorRoot();
    BOOST_CHECK_EQUAL(rootHex, rootBinary);

    // A fully migrated node (Binary, no fallback) reads the migrated rows directly.
    account::setNodeAddressTableMode(account::AddressTableMode::Binary);
    executeOneBlock(101);
    BOOST_REQUIRE(probingScheduler.m_readBackBalance.has_value());
    BOOST_CHECK_EQUAL(*probingScheduler.m_readBackBalance, u256(12345));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
