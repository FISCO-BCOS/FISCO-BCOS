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

/// A legacy value-transfer transaction. `to` is stored as the "0x"-prefixed
/// lowercase hex address string (exactly the encoding every real tx path uses:
/// Web3Transaction::takeToTarsTransaction() writes hexPrefixed(), and the RPC
/// /txpool paths all treat `to` as a hex string — see
/// TxValidator::isValidToField() and ReceiptResponse). `sender` is forced to
/// `from` as raw 20 bytes (what TxValidator::verify() writes for web3 txs), and
/// `value`/`gasPrice` as hex strings.
///
/// Regression anchor: before the recipient-credit fix, bcosTransactionToEvmone
/// treated a hex-string `to` as raw address bytes and credited the ASCII chars
/// (e.g. "0x307863336561..." instead of the real 20-byte address), leaving the
/// recipient's balance at 0.
std::shared_ptr<bcostars::protocol::TransactionImpl> EEMakeTransferTx(
    bcostars::protocol::TransactionFactoryImpl& factory,
    std::shared_ptr<bcos::crypto::CryptoSuite> const& cryptoSuite, evmc_address const& from,
    evmc_address const& to, uint64_t value, std::string const& nonce)
{
    auto toHexStr = bcos::toHexStringWithPrefix(
        bcos::bytes(std::begin(to.bytes), std::end(to.bytes)));
    auto tx = factory.createTransaction(1 /* V1 */, toHexStr, {}, nonce,
        1000 /* blockLimit */, "0x1" /* chainId */, "" /* groupId */, 0 /* importTime */,
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

/// Persist the committed current block height into SYS_CURRENT_STATE/current_number —
/// the row ledger::getCurrentBlockNumber reads. StorageBlockHashes uses it to bound
/// BLOCKHASH lookups to the last 256 ancestors.
task::Task<void> EEWriteCurrentNumber(EEBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
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
    // Committed height = 0 (the parent of the block being executed, number 1),
    // so the StorageBlockHashes 256-ancestor bound is exercised and block 0
    // (the parent) stays resolvable.
    co_await EEWriteCurrentNumber(backendStorage, 0);

    bcostars::protocol::BlockHeaderImpl blockHeader;
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

// Regression: `to` is uniformly a "0x"-prefixed hex address string on every
// real tx path (Web3Transaction::takeToTarsTransaction() writes hexPrefixed();
// TxValidator::isValidToField() requires exactly 40 hex chars). Before the fix,
// bcosTransactionToEvmone copied the first 20 ASCII chars as the address, so
// the recipient was credited at a mangled account (the ASCII bytes) and the
// real recipient's balance stayed 0. EEMakeTransferTx (used by every transfer
// test below) stores the same "0x..." hex encoding; this case is an explicit
// anchor that a hex-encoded `to` must credit the real recipient.
BOOST_AUTO_TEST_CASE(serialExecuteBlockWithHexTo)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialHexToGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEMakeAddress(41);
        auto recipient0 = EEMakeAddress(51);
        auto recipient1 = EEMakeAddress(52);

        co_await EEFundAccount(backendStorage, sender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient0, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient1, 200, "1"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender, EEFunding - 300}, {recipient0, 100}, {recipient1, 200}});
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

// Discriminating cases: these must FAIL with the pre-fix executor (where
// applyStateDiff ran in finish() and prepare() threw on any validation
// failure) and PASS after state is applied in execute() with re-validation.
//   * Same sender, sequential nonces — the second transaction's nonce must be
//     validated against the first transaction's applied nonce bump.
//   * Two senders, one shared recipient — the second transaction must read the
//     first transaction's credit (absolute-value diffs would overwrite it).

BOOST_AUTO_TEST_CASE(serialSameSenderSequentialNonces)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialSameSenderGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEMakeAddress(21);
        auto recipient0 = EEMakeAddress(31);
        auto recipient1 = EEMakeAddress(32);

        co_await EEFundAccount(backendStorage, sender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient0, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient1, 200, "1"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender, EEFunding - 300}, {recipient0, 100}, {recipient1, 200}});
    }());
}

BOOST_AUTO_TEST_CASE(parallelSameSenderSequentialNonces)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthParallelSameSenderGC");
        SchedulerParallelImpl<EEMutableStorage> scheduler(ioServicePool);

        auto sender = EEMakeAddress(23);
        auto recipient0 = EEMakeAddress(33);
        auto recipient1 = EEMakeAddress(34);

        co_await EEFundAccount(backendStorage, sender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient0, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient1, 200, "1"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender, EEFunding - 300}, {recipient0, 100}, {recipient1, 200}});
    }());
}

BOOST_AUTO_TEST_CASE(serialSharedRecipient)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialSharedRecipGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender0 = EEMakeAddress(41);
        auto sender1 = EEMakeAddress(42);
        auto recipient = EEMakeAddress(51);

        co_await EEFundAccount(backendStorage, sender0, EEFunding);
        co_await EEFundAccount(backendStorage, sender1, EEFunding);
        co_await EEFundAccount(backendStorage, recipient, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender0, recipient, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender1, recipient, 200, "0"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender0, EEFunding - 100}, {sender1, EEFunding - 200}, {recipient, 300}});
    }());
}

BOOST_AUTO_TEST_CASE(parallelSharedRecipient)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthParallelSharedRecipGC");
        SchedulerParallelImpl<EEMutableStorage> scheduler(ioServicePool);

        auto sender0 = EEMakeAddress(43);
        auto sender1 = EEMakeAddress(44);
        auto recipient = EEMakeAddress(52);

        co_await EEFundAccount(backendStorage, sender0, EEFunding);
        co_await EEFundAccount(backendStorage, sender1, EEFunding);
        co_await EEFundAccount(backendStorage, recipient, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender0, recipient, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender1, recipient, 200, "0"));

        co_await EERunTransfers(scheduler, *executor, multiLayerStorage, backendStorage,
            cryptoSuite, txs,
            {{sender0, EEFunding - 100}, {sender1, EEFunding - 200}, {recipient, 300}});
    }());
}

// A transaction that fails validation for real (insufficient funds) must yield
// a failure receipt and must NOT abort the whole block, matching
// TransactionExecutorImpl's "bad transaction does not block the block"
// semantics (R1 in the review). The transaction after it must still succeed.
BOOST_AUTO_TEST_CASE(serialInvalidTxDoesNotAbortBlock)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialInvalidGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto badSender = EEMakeAddress(61);
        auto goodSender = EEMakeAddress(62);
        auto recipient = EEMakeAddress(71);

        // badSender is deliberately never funded → INSUFFICIENT_FUNDS at validation.
        co_await EEFundAccount(backendStorage, goodSender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, badSender, recipient, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, goodSender, recipient, 50, "0"));

        co_await EEWriteBlockHash(
            backendStorage, 0, cryptoSuite->hashImpl()->hash(std::string("genesis")));
        co_await EEWriteBlockHash(
            backendStorage, 1, cryptoSuite->hashImpl()->hash(std::string("block-1")));

        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setNumber(1);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);

        auto view = multiLayerStorage.fork();
        view.newMutable();

        auto transactions =
            txs | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, blockHeader, transactions, ledgerConfig);

        BOOST_CHECK_EQUAL(receipts.size(), 2u);
        BOOST_CHECK(receipts[0]->status() != 0);  // invalid tx → failure receipt
        BOOST_CHECK_EQUAL(receipts[1]->status(), 0);  // good tx still succeeds
        auto goodBalance = co_await EEReadBalance(view, goodSender);
        BOOST_CHECK(goodBalance == EEFunding - 50);
    }());
}

// Round-2 discriminating case (comment on PR #5391): execute() must validate
// *every* transaction against the state it actually runs on, not only those
// that failed prepare(). One sender funded for exactly one transfer, two
// transactions both carrying nonce "0". The second transaction is valid at
// prepare() (storage nonce is still 0) but invalid by the time it executes —
// the first bumped the nonce. Without unconditional validation in execute(),
// transition() runs it anyway: it bumps the nonce again and wraps the sender's
// balance to ~2^256 (ether created from nothing, consensus divergence). With
// the fix the second transaction is rejected as NONCE_TOO_LOW, gets a failure
// receipt, and the sender keeps its remaining balance.

BOOST_AUTO_TEST_CASE(serialSameNonceSecondRejected)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEthSerialSameNonceGC");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEMakeAddress(81);
        auto recipient0 = EEMakeAddress(91);
        auto recipient1 = EEMakeAddress(92);

        // Funded for exactly one transfer (nonce "0" is the only valid one).
        co_await EEFundAccount(backendStorage, sender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient0, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient1, 100, "0"));

        co_await EEWriteBlockHash(
            backendStorage, 0, cryptoSuite->hashImpl()->hash(std::string("genesis")));
        co_await EEWriteBlockHash(
            backendStorage, 1, cryptoSuite->hashImpl()->hash(std::string("block-1")));
        co_await EEWriteCurrentNumber(backendStorage, 0);

        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setNumber(1);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);

        auto view = multiLayerStorage.fork();
        view.newMutable();

        auto transactions =
            txs | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, blockHeader, transactions, ledgerConfig);

        BOOST_CHECK_EQUAL(receipts.size(), 2u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);  // first nonce-"0" tx succeeds
        BOOST_CHECK(receipts[1]->status() != 0);  // second same-nonce tx rejected

        // The sender must NOT be wrapped to ~2^256: only the first transfer is
        // debited, and the rejected second one writes nothing.
        auto senderBalance = co_await EEReadBalance(view, sender);
        BOOST_CHECK(senderBalance == EEFunding - 100);
        auto recipient0Balance = co_await EEReadBalance(view, recipient0);
        BOOST_CHECK(recipient0Balance == u256(100));
        auto recipient1Balance = co_await EEReadBalance(view, recipient1);
        BOOST_CHECK(recipient1Balance == u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(parallelSameNonceSecondRejected)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool =
            std::make_shared<bcos::IOServicePool>(1, "testEthParallelSameNonceGC");
        SchedulerParallelImpl<EEMutableStorage> scheduler(ioServicePool);

        auto sender = EEMakeAddress(82);
        auto recipient0 = EEMakeAddress(93);
        auto recipient1 = EEMakeAddress(94);

        // Funded for exactly one transfer (nonce "0" is the only valid one).
        co_await EEFundAccount(backendStorage, sender, EEFunding);
        co_await EEFundAccount(backendStorage, recipient0, 0);
        co_await EEFundAccount(backendStorage, recipient1, 0);

        std::vector<protocol::Transaction::Ptr> txs;
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient0, 100, "0"));
        txs.emplace_back(
            EEMakeTransferTx(transactionFactory, cryptoSuite, sender, recipient1, 100, "0"));

        co_await EEWriteBlockHash(
            backendStorage, 0, cryptoSuite->hashImpl()->hash(std::string("genesis")));
        co_await EEWriteBlockHash(
            backendStorage, 1, cryptoSuite->hashImpl()->hash(std::string("block-1")));
        co_await EEWriteCurrentNumber(backendStorage, 0);

        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setNumber(1);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);

        auto view = multiLayerStorage.fork();
        view.newMutable();

        auto transactions =
            txs | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, blockHeader, transactions, ledgerConfig);

        BOOST_CHECK_EQUAL(receipts.size(), 2u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);  // first nonce-"0" tx succeeds
        BOOST_CHECK(receipts[1]->status() != 0);  // second same-nonce tx rejected

        auto senderBalance = co_await EEReadBalance(view, sender);
        BOOST_CHECK(senderBalance == EEFunding - 100);
        auto recipient0Balance = co_await EEReadBalance(view, recipient0);
        BOOST_CHECK(recipient0Balance == u256(100));
        auto recipient1Balance = co_await EEReadBalance(view, recipient1);
        BOOST_CHECK(recipient1Balance == u256(0));
    }());
}

// StorageBlockHashes only resolves the last 256 ancestor block hashes (Ethereum
// BLOCKHASH semantics). With a committed height of 300, block 50 is reachable
// (300-50=250) but block 5 is not (300-5=295 > 255) — even though its hash IS
// present in SYS_NUMBER_2_HASH, the provider must report it as unknown.
BOOST_AUTO_TEST_CASE(blockHashLookbackLimit)
{
    task::syncWait([&, this]() -> task::Task<void> {
        co_await EEWriteCurrentNumber(backendStorage, 300);
        co_await EEWriteBlockHash(
            backendStorage, 50, cryptoSuite->hashImpl()->hash(std::string("block-50")));
        co_await EEWriteBlockHash(
            backendStorage, 5, cryptoSuite->hashImpl()->hash(std::string("block-5")));

        // Within the last 256 ancestors — resolved straight from storage.
        auto h50 =
            task::tbb::syncWait(ledger::getBlockHash(backendStorage, 50, ledger::fromStorage));
        BOOST_CHECK(h50.has_value());
        auto resolved50 = blockHashes->get_block_hash(50);
        BOOST_CHECK_EQUAL(
            std::memcmp(resolved50.bytes, h50->data(), sizeof(evmc_bytes32)), 0);

        // Older than 256 ancestors — unknown (zero hash), despite the hash being stored.
        evmc::bytes32 zero{};
        auto resolved5 = blockHashes->get_block_hash(5);
        BOOST_CHECK_EQUAL(std::memcmp(resolved5.bytes, zero.bytes, sizeof(evmc_bytes32)), 0);

        // A future height (> current) is never an ancestor — unknown.
        auto resolved301 = blockHashes->get_block_hash(301);
        BOOST_CHECK_EQUAL(std::memcmp(resolved301.bytes, zero.bytes, sizeof(evmc_bytes32)), 0);
    }());
}

// Regression for the eth_call / eth_estimateGas dry-run path (review finding 1):
// ExecuteContext::call was stored but never read, so a call-style executeTransaction
// (call=true) ran the SAME validate_transaction as a real tx. The standard RPC call
// shape — empty nonce, omitted gas, no gasPrice — from a sender whose nonce is no
// longer 0 was therefore rejected with NONCE_TOO_LOW / INTRINSIC_GAS_TOO_LOW.
// The dry-run is normalized to Ethereum RPC semantics instead (nonce = sender's
// current nonce, gas = min(block gas limit, MAX_TX_GAS_LIMIT), base fee and gas
// price zeroed) and must succeed.
//
// Runs at EVMC_OSAKA deliberately: this is EVMC_REVISION_DEFAULT and the revision
// where EIP-7825 (MAX_TX_GAS_LIMIT = 2^24) bites — FISCO's block ceiling is 3e9, so
// without the per-tx cap the dry-run would fail MAX_GAS_LIMIT_EXCEEDED. The gas cap
// and unconditional price/base-fee zeroing are exactly what this case guards.
BOOST_AUTO_TEST_CASE(callDryRunSkipsNonceAndGasValidation)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto sender = EEMakeAddress(71);
        auto recipient = EEMakeAddress(81);

        co_await EEFundAccount(backendStorage, sender, EEFunding);
        // Sender is past its first transaction (nonce 3): a call with an empty nonce
        // would be NONCE_TOO_LOW if the executor validated it like a real tx.
        {
            using namespace bcos::ledger::account;
            EVMAccount<EEBackendStorage> acc(backendStorage, sender, false);
            co_await acc.setNonce("3");
        }
        co_await EEFundAccount(backendStorage, recipient, 0);

        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setNumber(1);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());

        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(EVMC_OSAKA);

        // Call-shaped tx: empty nonce, gas limit 0, no gasPrice — exactly what
        // CallRequest::takeToTransaction produces for an eth_call that omits them.
        auto toHexStr = bcos::toHexStringWithPrefix(
            bcos::bytes(std::begin(recipient.bytes), std::end(recipient.bytes)));
        auto tx = transactionFactory.createTransaction(1 /* V1 */, toHexStr, {},
            "" /*nonce*/, 1000 /*blockLimit*/, "0x1" /*chainId*/, "" /*groupId*/,
            0 /*importTime*/, {} /*abi*/, EEQuantity(100) /*value*/, "0x0" /*gasPrice*/,
            0 /*gasLimit*/);
        tx->forceSender(bcos::bytes(std::begin(sender.bytes), std::end(sender.bytes)));
        tx->calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        auto receipt = co_await executor->executeTransaction(
            view, blockHeader, *tx, 0, ledgerConfig, /*call=*/true);

        BOOST_CHECK_EQUAL(receipt->status(), 0);
        // The dry-run executed the value transfer on the throwaway view.
        auto balance = co_await EEReadBalance(view, recipient);
        BOOST_CHECK_EQUAL(balance, u256(100));
    }());
}

// Unit-level coverage for bcosTransactionToEvmone's `to` decoding (BCOS2Evmone.cpp,
// review comment): only a well-formed 20-byte address — hex-string form or raw bytes —
// sets evmTx.to. A short or malformed value leaves `to` unset (contract creation),
// never a transfer to a left-aligned or all-zero fabricated address. This also covers
// the raw-bytes fallback branch, which otherwise has no production caller.
BOOST_AUTO_TEST_CASE(toDecodingOnlyWellFormedAddresses)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto recipient = EEMakeAddress(91);
        auto toHexStr = bcos::toHexStringWithPrefix(
            bcos::bytes(std::begin(recipient.bytes), std::end(recipient.bytes)));

        auto mkTx = [&](std::string const& toField) {
            auto tx = transactionFactory.createTransaction(1 /* V1 */, toField, {}, "0", 1000,
                "0x1", "", 0, {}, EEQuantity(1), "0x0", 21000);
            tx->forceSender(bcos::bytes(20, 0));
            tx->calculateHash(*cryptoSuite->hashImpl());
            return tx;
        };

        // Well-formed "0x"-prefixed hex -> the exact recipient address.
        {
            auto evmTx = bcosTransactionToEvmone(*mkTx(toHexStr));
            BOOST_REQUIRE(evmTx.to.has_value());
            BOOST_CHECK_EQUAL(
                std::memcmp(evmTx.to->bytes, recipient.bytes, sizeof(evmc_address)), 0);
        }

        // Raw 20 bytes (defensive fallback branch) -> the exact recipient address.
        {
            bcos::bytes raw(std::begin(recipient.bytes), std::end(recipient.bytes));
            auto evmTx = bcosTransactionToEvmone(*mkTx(std::string(raw.begin(), raw.end())));
            BOOST_REQUIRE(evmTx.to.has_value());
            BOOST_CHECK_EQUAL(
                std::memcmp(evmTx.to->bytes, recipient.bytes, sizeof(evmc_address)), 0);
        }

        // Short hex "0x1234" -> unset (contract creation), never 0x1234000...0.
        {
            auto evmTx = bcosTransactionToEvmone(*mkTx("0x1234"));
            BOOST_CHECK(!evmTx.to.has_value());
        }

        // Bare "0x" -> unset (contract creation), never a transfer to address(0).
        {
            auto evmTx = bcosTransactionToEvmone(*mkTx("0x"));
            BOOST_CHECK(!evmTx.to.has_value());
        }

        // Malformed hex "0xzz" -> unset (contract creation).
        {
            auto evmTx = bcosTransactionToEvmone(*mkTx("0xzz"));
            BOOST_CHECK(!evmTx.to.has_value());
        }
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
