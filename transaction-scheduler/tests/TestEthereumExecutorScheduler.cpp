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
 * @file TestEthereumExecutorScheduler.cpp
 * @brief Wires the EthereumExecutor (ethereum-executor) into the transaction
 *        schedulers, exercising the TransactionExecutor concept's three-phase
 *        prepare()/execute()/finish() lifecycle under both the serial and the
 *        parallel (pipelined, concurrent) schedulers — the same integration the
 *        benchmark does for TransactionExecutorImpl, but with the pure-Ethereum
 *        executor.
 *
 * blockHashes is injected into the executor's constructor from a
 * StorageBlockHashes provider that resolves hashes out of the storage through
 * the ledger::getBlockHash LedgerMethod.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/SchedulerParallelImpl.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/StorageBlockHashes.h"
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Anonymous namespace + EE prefix: this TU is unity-merged with the other
// scheduler test TUs, so every namespace-scope entity gets a unique name.
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using EEMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using EEBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using EECheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, EEBackendStorage>;
using EEMultiLayerStorage = MultiLayerStorage<EEMutableStorage, void, EECheckpointBackend>;

// EthereumExecutor must satisfy the TransactionExecutor concept, so it can be
// driven through the scheduler's createExecuteContext + prepare/execute/finish
// pipeline.
static_assert(executor_v1::TransactionExecutor<EthereumExecutor, EEMutableStorage>);

static const u256 EEFunding = u256(1000000000000000000ULL);  // 1 ETH

evmc_address EEMakeAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

std::string EEQuantity(uint64_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << value;
    return oss.str();
}

/// A legacy value-transfer transaction. `to` is stored as the 20 raw address
/// bytes, `value`/`gasPrice` as hex strings, `sender` forced to `from`.
std::shared_ptr<bcostars::protocol::TransactionImpl> EEMakeTransferTx(
    bcostars::protocol::TransactionFactoryImpl& factory,
    std::shared_ptr<bcos::crypto::CryptoSuite> const& cryptoSuite, evmc_address const& from,
    evmc_address const& to, uint64_t value, std::string const& nonce)
{
    bcos::bytes toBytes(std::begin(to.bytes), std::end(to.bytes));
    auto tx = factory.createTransaction(1 /* V1 */, std::string(toBytes.begin(), toBytes.end()), {},
        nonce, 1000 /* blockLimit */, "0x1" /* chainId */, "" /* groupId */, 0 /* importTime */,
        {} /* abi */, EEQuantity(value) /* value */, "0x0" /* gasPrice */, 21000 /* gasLimit */);
    tx->forceSender(bcos::bytes(std::begin(from.bytes), std::end(from.bytes)));
    tx->calculateHash(*cryptoSuite->hashImpl());
    return std::static_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
}

task::Task<void> EEFundAccount(EEBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<EEBackendStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> EEReadBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_return co_await acc.balance();
}

/// Persist a committed block hash into SYS_NUMBER_2_HASH — the row the
/// ledger::getBlockHash LedgerMethod reads.
task::Task<void> EEWriteBlockHash(
    EEBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
}

class TestEthereumExecutorSchedulerFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    EEBackendStorage backendStorage;
    EECheckpointBackend checkpointBackend{backendStorage};
    EEMultiLayerStorage multiLayerStorage{checkpointBackend};

    // Storage-backed BlockHashes injected into the executor. It reads hashes
    // from `backendStorage` through the ledger::getBlockHash LedgerMethod.
    std::shared_ptr<StorageBlockHashes<EEBackendStorage>> blockHashes;
    std::shared_ptr<EthereumExecutor> executor;

    TestEthereumExecutorSchedulerFixture()
    {
        blockHashes = std::make_shared<StorageBlockHashes<EEBackendStorage>>(backendStorage);
        executor = std::make_shared<EthereumExecutor>(receiptFactory, hashImpl, blockHashes);
    }
};

/// Shared body: seed committed block hashes + funded accounts, build a header
/// and run the given transfers through @p scheduler, verifying receipts plus
/// the resulting balances.
template <class Scheduler>
task::Task<void> EERunTransfers(Scheduler& scheduler, EthereumExecutor& executor,
    EEMultiLayerStorage& multiLayerStorage, EEBackendStorage& backendStorage,
    std::shared_ptr<bcos::crypto::CryptoSuite> const& cryptoSuite,
    std::vector<protocol::Transaction::Ptr> const& txs,
    std::vector<std::pair<evmc_address, u256>> const& expectedBalances)
{
    // Committed block hashes — resolvable via ledger::getBlockHash from storage.
    co_await EEWriteBlockHash(
        backendStorage, 0, cryptoSuite->hashImpl()->hash(std::string("genesis")));
    co_await EEWriteBlockHash(
        backendStorage, 1, cryptoSuite->hashImpl()->hash(std::string("block-1")));

    bcostars::protocol::BlockHeaderImpl blockHeader(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    blockHeader.setNumber(1);
    blockHeader.calculateHash(*cryptoSuite->hashImpl());

    ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);

    auto view = multiLayerStorage.fork();
    view.newMutable();

    auto transactions =
        txs | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });

    auto receipts = co_await scheduler.executeBlock(
        view, executor, blockHeader, transactions, ledgerConfig);

    BOOST_CHECK_EQUAL(receipts.size(), txs.size());
    for (size_t i = 0; i < receipts.size(); ++i)
    {
        BOOST_CHECK_MESSAGE(receipts[i]->status() == 0,
            "tx#" << i << " failed with status " << receipts[i]->status());
    }

    // The writes landed in the view's mutable layer; read the resulting balances.
    for (auto const& [addr, expected] : expectedBalances)
    {
        auto actual = co_await EEReadBalance(view, addr);
        BOOST_CHECK_MESSAGE(actual == expected,
            "balance mismatch: expected " << expected << " got " << actual);
    }
}

BOOST_FIXTURE_TEST_SUITE(TestEthereumExecutorScheduler, TestEthereumExecutorSchedulerFixture)

BOOST_AUTO_TEST_CASE(serialExecuteBlock)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender0 = EEMakeAddress(1);
        auto sender1 = EEMakeAddress(2);
        auto recipient0 = EEMakeAddress(11);
        auto recipient1 = EEMakeAddress(12);

        co_await EEFundAccount(backendStorage, sender0, EEFunding);
        co_await EEFundAccount(backendStorage, sender1, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(EEMakeTransferTx(transactionFactory, cryptoSuite, sender0, recipient0, 100, "0"));
        txs.emplace_back(EEMakeTransferTx(transactionFactory, cryptoSuite, sender1, recipient1, 200, "0"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender0, EEFunding - 100}, {sender1, EEFunding - 200}, {recipient0, 100},
                {recipient1, 200}});

        // The storage-backed BlockHashes resolves committed hashes via LedgerMethod.
        auto h0 = task::tbb::syncWait(
            ledger::getBlockHash(backendStorage, 0, ledger::fromStorage));
        BOOST_CHECK(h0.has_value());
        auto resolved = blockHashes->get_block_hash(0);
        BOOST_CHECK_EQUAL(
            std::memcmp(resolved.bytes, h0->data(), sizeof(evmc_bytes32)), 0);
    }());
}

BOOST_AUTO_TEST_CASE(parallelExecuteBlock)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthParallelGC");
        SchedulerParallelImpl<EEMutableStorage> scheduler(ioServicePool);

        // Distinct sender→recipient pairs: no read/write overlap, so the
        // scheduler runs the prepare/execute/finish phases of the chunks
        // concurrently — exercising the executor's thread-safety.
        std::vector<std::pair<evmc_address, evmc_address>> transfers;
        std::vector<protocol::Transaction::Ptr> txs;
        std::vector<std::pair<evmc_address, u256>> expected;
        constexpr static uint64_t TRANSFER_COUNT = 8;
        for (uint64_t i = 0; i < TRANSFER_COUNT; ++i)
        {
            auto sender = EEMakeAddress(static_cast<uint8_t>(1 + i));
            auto recipient = EEMakeAddress(static_cast<uint8_t>(101 + i));
            transfers.emplace_back(sender, recipient);
            co_await EEFundAccount(backendStorage, sender, EEFunding);
            co_await EEFundAccount(backendStorage, recipient, 0);
            txs.emplace_back(EEMakeTransferTx(
                transactionFactory, cryptoSuite, sender, recipient, 10 + i, "0"));
            expected.emplace_back(sender, EEFunding - (10 + i));
            expected.emplace_back(recipient, 10 + i);
        }

        co_await EERunTransfers(
            scheduler, *executor, multiLayerStorage, backendStorage, cryptoSuite, txs, expected);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
