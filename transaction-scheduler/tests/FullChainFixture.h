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
 * @file FullChainFixture.h
 * @brief End-to-end chain fixture over the PRODUCTION persistence stack: a real
 *        CheckpointRocksDBStorage backend in a tmpdir (the exact storage
 *        GlobalStateStorageInitializer builds for a node), a real Ledger sharing the same
 *        ::rocksdb::DB through the legacy RocksDBStorage adapter (the Initializer.cpp
 *        arrangement), real Ledger::buildGenesisBlock — including the L2 genesis trie-node
 *        persistence — and the real BaselineScheduler execute/commit path on top.
 *
 *        Two deliberate substitutions, both OUTSIDE the code under test:
 *        - block deltas are injected through a plan-replaying scheduler impl instead of the
 *          EVM executor (the MPT build consumes the view's mutable layer either way);
 *        - the txpool is FakeTxPool: every test block carries its transactions inline, so
 *          getTransactions never consults the pool.
 *
 *        Features are NOT injected through any test hook: scenario flags activate through
 *        real SYS_CONFIG rows (genesis features via buildGenesisBlock, mid-chain activation
 *        via the same {value, enableNumber} row a governance action writes), and the
 *        scheduler reads them back through the production getLedgerConfig(view, ...) path.
 *
 *        Shared by FullChainFixtureSmokeTest / BaselineSchedulerMPTL1UpgradeTest /
 *        BaselineSchedulerMPTGenesisTest (test-transaction-scheduler) and
 *        EthGetProofIntegrationTest (test-bcos-rpc).
 */
#pragma once

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeTxPool.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/Classify.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include "bcos-ledger/mpt/StorageValueCodec.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-storage/CheckpointRocksDBStorage.h"
#include "bcos-storage/KeyPrefixes.h"
#include "bcos-storage/RocksDBStorage.h"
#include "bcos-storage/StateKVResolver.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bcos::test::fullchain
{

// The production state-storage stack, mirroring libinitializer/GlobalStateStorageInitializer.h
// (not included: its constructor lives in the init library, which tests do not link). The LRU
// cache layer is omitted (void): it is a read-through cache with no behavior of its own that
// these correctness tests could observe.
using FCMutableStorage =
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
using FCCheckpointStorage =
    storage2::rocksdb::CheckpointRocksDBStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::rocksdb::StateKeyResolver, storage2::rocksdb::StateValueResolver>;
using FCMultiLayerStorage =
    storage2::MultiLayerStorage<FCMutableStorage, void, FCCheckpointStorage>;

/// One flat-state row the plan-replaying scheduler writes for a block.
/// value == nullopt performs a storage-level logical deletion.
struct FCRowOp
{
    std::string table;
    std::string key;
    std::optional<std::string> value;
};

/// Scheduler impl replaying a per-block row plan into the storage BaselineScheduler hands it
/// (the execute view's mutable layer) — the production write path minus the EVM.
struct FCWritingScheduler
{
    std::map<protocol::BlockNumber, std::vector<FCRowOp>> const* m_plan{};

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        if (auto it = m_plan->find(blockHeader.number()); it != m_plan->end())
        {
            for (auto const& row : it->second)
            {
                if (row.value)
                {
                    storage::Entry entry;
                    entry.set(*row.value);
                    co_await storage2::writeOne(
                        storage, executor_v1::StateKey{row.table, row.key}, std::move(entry));
                }
                else
                {
                    co_await storage2::removeOne(
                        storage, executor_v1::StateKey{row.table, row.key});
                }
            }
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
                        ::ranges::to<std::vector>();
        co_return receipts;
    }
};

/// No-op executor satisfying BaselineScheduler's TransactionExecutor concept; never invoked
/// because FCWritingScheduler does not delegate per-transaction execution.
struct FCExecutor
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

class FullChainFixture
{
    /// Declared FIRST so its destructor runs LAST — after the RocksDB handles have closed.
    struct TmpDirGuard
    {
        std::filesystem::path m_path;
        explicit TmpDirGuard(std::string const& testName)
        {
            static std::atomic<uint64_t> counter{0};
            m_path = std::filesystem::temp_directory_path() /
                     ("fullchain-" + testName + "-" + std::to_string(counter.fetch_add(1)) + "-" +
                         std::to_string(static_cast<uint64_t>(
                             std::chrono::steady_clock::now().time_since_epoch().count())));
            std::filesystem::remove_all(m_path);
            std::filesystem::create_directories(m_path);
        }
        ~TmpDirGuard() noexcept
        {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
        TmpDirGuard(TmpDirGuard const&) = delete;
        TmpDirGuard& operator=(TmpDirGuard const&) = delete;
    };

public:
    explicit FullChainFixture(std::string testName)
      : m_tmpdir(testName),
        m_checkpointStorage(m_tmpdir.m_path.string()),
        m_multiLayerStorage(m_checkpointStorage),
        // The legacy StorageInterface shares CheckpointRocksDBStorage's ::rocksdb::DB with a
        // no-op deleter — byte-for-byte the Initializer.cpp wiring. Legacy keys ("table:key")
        // and StateKeyResolver encodings agree, so the Ledger and the scheduler stack
        // read/write the same physical rows.
        m_legacyStorage(std::make_shared<storage::RocksDBStorage>(
            std::unique_ptr<::rocksdb::DB, std::function<void(::rocksdb::DB*)>>(
                &m_multiLayerStorage.latestBackend().rocksDB(), [](::rocksdb::DB*) {}),
            nullptr)),
        m_cryptoSuite(std::make_shared<crypto::CryptoSuite>(
            std::make_shared<crypto::Keccak256>(), nullptr, nullptr)),
        m_blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(m_cryptoSuite)),
        m_transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(m_cryptoSuite)),
        m_receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(m_cryptoSuite)),
        m_blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            m_cryptoSuite, m_blockHeaderFactory, m_transactionFactory, m_receiptFactory)),
        m_ledger(std::make_shared<bcos::ledger::Ledger>(m_blockFactory, m_legacyStorage, 1)),
        m_txpool(std::make_shared<bcos::test::FakeTxPool>()),
        m_transactionSubmitResultFactory(
            std::make_shared<protocol::TransactionSubmitResultFactoryImpl>()),
        m_baselineScheduler(m_multiLayerStorage, m_schedulerImpl, m_executor, *m_blockFactory,
            *m_ledger, *m_txpool, *m_transactionSubmitResultFactory, *m_hashImpl)
    {
        m_schedulerImpl.m_plan = &m_plan;
        m_baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        m_baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    // --- genesis ---------------------------------------------------------------------------

    /// Minimal viable genesis (same shape as test_GenesisNodePersistence's baseConfig).
    /// Auth check off: initSysContract's block-0 deploy is EVM work outside this fixture.
    static ledger::GenesisConfig baseGenesis()
    {
        ledger::GenesisConfig genesis;
        genesis.m_txGasLimit = 3000000000;
        genesis.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        genesis.m_chainID = "901";
        genesis.m_groupID = "group0";
        genesis.m_isAuthCheck = false;
        return genesis;
    }

    /// Real chain init: Ledger::buildGenesisBlock over the shared RocksDB — writes the genesis
    /// header, SYS_CONFIG rows (features included) and, for L2 genesis configs, every genesis
    /// trie node as a "/mpt/" state row (cc51df624).
    void buildGenesis(ledger::GenesisConfig const& genesis)
    {
        ledger::LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(crypto::HashType(""));
        param.setBlockTxCountLimit(0);
        bool ok = task::syncWait(ledger::buildGenesisBlock(*m_ledger, genesis, param));
        BOOST_REQUIRE_MESSAGE(ok, "buildGenesisBlock failed");
    }

    /// Scenario-A style mid-chain activation: write the SYS_CONFIG feature row exactly as a
    /// governance action would — {value="1", enableNumber}. getLedgerConfig's readFromStorage
    /// turns it into set(flag) + activationBlock == enableNumber for every block >=
    /// enableNumber; blocks below never see the flag.
    void enableFeatureFromBlock(std::string_view flagName, protocol::BlockNumber enableNumber)
    {
        storage::Entry entry;
        entry.set(storage::serialize::encode(ledger::SystemConfigEntry{"1", enableNumber}));
        task::syncWait(storage2::writeOne(m_multiLayerStorage.latestBackend(),
            executor_v1::StateKey{ledger::SYS_CONFIG, flagName}, std::move(entry)));
    }

    // --- running blocks --------------------------------------------------------------------

    void planBlock(protocol::BlockNumber number, std::vector<FCRowOp> rows)
    {
        m_plan[number] = std::move(rows);
    }

    static FCRowOp balanceRow(Address const& addr, std::string value)
    {
        return {bcos::ledger::mpt::accountTableName(addr),
            std::string(bcos::ledger::mpt::ROW_BALANCE), std::move(value)};
    }
    static FCRowOp nonceRow(Address const& addr, std::string value)
    {
        return {bcos::ledger::mpt::accountTableName(addr),
            std::string(bcos::ledger::mpt::ROW_NONCE), std::move(value)};
    }
    /// A storage slot row: 32-raw-byte slot key, 32-raw-byte value.
    static FCRowOp slotRow(Address const& addr, h256 const& slot, h256 const& value)
    {
        return {bcos::ledger::mpt::accountTableName(addr),
            std::string(reinterpret_cast<char const*>(slot.data()), h256::SIZE),
            std::string(reinterpret_cast<char const*>(value.data()), h256::SIZE)};
    }

    std::tuple<Error::Ptr, protocol::BlockHeader::Ptr> executeBlockRaw(protocol::BlockNumber number)
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto blockHeader = block->blockHeader();
        blockHeader->setNumber(number);
        blockHeader->setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION));
        blockHeader->calculateHash(*m_hashImpl);
        // One inline transaction: getTransactions serves the block's own transactions, so the
        // FakeTxPool is never consulted, and the real Ledger prewrite has something to store.
        bytes input;
        block->appendTransaction(m_transactionFactory->createTransaction(
            0, "to", input, std::to_string(number), 100, "chain", "group", 0));

        Error::Ptr execError;
        protocol::BlockHeader::Ptr executedHeader;
        m_baselineScheduler.executeBlock(
            block, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr header, bool) {
                execError = std::move(error);
                executedHeader = std::move(header);
            });
        return {std::move(execError), std::move(executedHeader)};
    }

    protocol::BlockHeader::Ptr executeOneBlock(protocol::BlockNumber number)
    {
        auto [error, header] = executeBlockRaw(number);
        BOOST_REQUIRE_MESSAGE(
            !error, "executeBlock failed: " + (error ? error->errorMessage() : ""));
        BOOST_REQUIRE(header);
        return header;
    }

    void commitOneBlock(protocol::BlockHeader::Ptr const& header)
    {
        Error::Ptr commitError;
        m_baselineScheduler.commitBlock(header,
            [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
        BOOST_REQUIRE_MESSAGE(!commitError,
            "commitBlock failed: " + (commitError ? commitError->errorMessage() : ""));
    }

    /// Execute + commit one block; returns the executed header (stateRoot readable).
    protocol::BlockHeader::Ptr runBlock(protocol::BlockNumber number)
    {
        auto header = executeOneBlock(number);
        commitOneBlock(header);
        return header;
    }

    // --- assertion helpers -----------------------------------------------------------------

    /// The block header as COMMITTED on chain, read back through the real Ledger.
    protocol::BlockHeader::Ptr headerOnChain(protocol::BlockNumber number)
    {
        auto block = task::syncWait(ledger::getBlockData(*m_ledger, number, ledger::HEADER));
        BOOST_REQUIRE_MESSAGE(block && block->blockHeader(),
            "no block header on chain for " + std::to_string(number));
        return block->blockHeader();
    }

    /// One trie-node row from the RocksDB backend under its ordinary "/mpt/" StateKey.
    std::optional<bytes> backendNode(h256 const& hash)
    {
        auto entry = task::syncWait(storage2::readOne(
            m_multiLayerStorage.latestBackend(), storage2::mptNodeStateKey(hash)));
        if (!entry)
        {
            return std::nullopt;
        }
        auto raw = entry->get();
        return bytes(raw.begin(), raw.end());
    }

    /// Count of committed "/mpt/" rows in the RocksDB backend.
    size_t backendNodeCount()
    {
        return task::syncWait([this]() -> task::Task<size_t> {
            size_t count = 0;
            auto iterator = co_await storage2::range(m_multiLayerStorage.latestBackend());
            while (auto keyValue = co_await iterator.next())
            {
                auto&& [key, value] = *keyValue;
                if (executor_v1::StateKeyView{key}.m_table == storage2::kMPTTable)
                {
                    ++count;
                }
            }
            co_return count;
        }());
    }

    // --- oracles (independent of the scheduler's build) --------------------------------------

    /// Legacy XOR oracle over a block's row plan (same per-entry hash shape as
    /// calculateStateRoot). @p features must be the features ACTIVE at that block.
    h256 xorOracle(std::vector<FCRowOp> const& rows, ledger::Features const& active) const
    {
        const std::optional<ledger::Features> features{active};
        h256 expected;
        for (auto const& row : rows)
        {
            BOOST_REQUIRE(row.value);
            storage::Entry entry;
            entry.set(*row.value);
            expected ^= entry.hash(row.table, row.key, *m_hashImpl,
                static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION), features);
        }
        return expected;
    }

    /// The features the production read path yields at @p blockNumber — read from the same
    /// committed SYS_CONFIG rows the scheduler reads, activation blocks included.
    ledger::Features featuresAt(protocol::BlockNumber blockNumber)
    {
        ledger::Features features;
        task::syncWait(
            ledger::readFromStorage(features, m_multiLayerStorage.latestBackend(), blockNumber));
        return features;
    }

    /// Independent MPT oracle: one from-scratch commitTrie build over the expected FINAL
    /// account set (tries are history-independent).
    static h256 mptOracle(std::map<Address, bcos::ledger::mpt::Account> const& accounts)
    {
        namespace mpt = bcos::ledger::mpt;
        std::map<h256, std::optional<bytes>> accountChanges;
        for (auto const& [address, account] : accounts)
        {
            accountChanges[mpt::accountKeyHash(address)] = account.encode();
        }
        storage2::memory_storage::MemoryStorage<h256, bytes, storage2::memory_storage::ORDERED>
            scratch;
        auto merged =
            task::syncWait(mpt::commitTrie(scratch, mpt::emptyRootHash(), accountChanges));
        return merged.root;
    }

    /// Storage-trie oracle over (slot -> 32-byte raw value).
    static h256 storageTrieOracle(std::map<h256, bytes> const& slots)
    {
        namespace mpt = bcos::ledger::mpt;
        std::map<h256, std::optional<bytes>> changes;
        for (auto const& [slot, rawValue] : slots)
        {
            changes[mpt::slotKeyHash(slot)] =
                mpt::encodeStorageValue(bytesConstRef(rawValue.data(), rawValue.size()));
        }
        storage2::memory_storage::MemoryStorage<h256, bytes, storage2::memory_storage::ORDERED>
            scratch;
        auto merged = task::syncWait(mpt::commitTrie(scratch, mpt::emptyRootHash(), changes));
        return merged.root;
    }

    static Address makeAddress(uint8_t firstByte)
    {
        Address address{};
        address.data()[0] = firstByte;
        return address;
    }

    TmpDirGuard m_tmpdir;
    FCCheckpointStorage m_checkpointStorage;
    FCMultiLayerStorage m_multiLayerStorage;
    storage::StorageInterface::Ptr m_legacyStorage;
    crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> m_blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> m_transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> m_receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> m_blockFactory;
    std::shared_ptr<bcos::ledger::Ledger> m_ledger;
    std::shared_ptr<bcos::test::FakeTxPool> m_txpool;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> m_transactionSubmitResultFactory;
    crypto::Hash::Ptr m_hashImpl = std::make_shared<crypto::Keccak256>();
    std::map<protocol::BlockNumber, std::vector<FCRowOp>> m_plan;
    FCWritingScheduler m_schedulerImpl;
    FCExecutor m_executor;
    scheduler_v1::BaselineScheduler<FCMultiLayerStorage, FCExecutor, FCWritingScheduler,
        bcos::ledger::Ledger>
        m_baselineScheduler;
};

}  // namespace bcos::test::fullchain

// Explicitly instantiated once per test binary (SharedBaselineSchedulerInst.cpp in
// test-transaction-scheduler, unittests/rpc/FullChainFixtureInst.cpp in test-bcos-rpc)
// so each consumer TU — the MPT genesis / L1-upgrade / smoke tests — parses only the
// declaration header instead of instantiating the whole scheduler.
//
// Consumer-binary checklist: any test binary that includes this header links against
// the specialization below WITHOUT instantiating it, so it MUST contain an explicit
// instantiation TU — copy either of the two listed above (they are identical modulo
// the header paths). A binary that forgets fails at link time with undefined
// references to BaselineScheduler<FC...> members.
extern template class bcos::scheduler_v1::BaselineScheduler<
    bcos::test::fullchain::FCMultiLayerStorage, bcos::test::fullchain::FCExecutor,
    bcos::test::fullchain::FCWritingScheduler, bcos::ledger::Ledger>;
