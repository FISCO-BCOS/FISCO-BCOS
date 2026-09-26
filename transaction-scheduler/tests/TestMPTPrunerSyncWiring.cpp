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
 * @file TestMPTPrunerSyncWiring.cpp
 * @brief End-to-end wiring of the MPT pruner into the DEVP2P-SYNC commit path — the twin of
 *        TestMPTPrunerWiring (PBFT lane) and TestMPTPrunerEngineWiring (Engine API lane). A
 *        REAL bcos::ledger::mpt::MPTPruner (window N=1, not a spy) is wired as the
 *        EthereumBlockVerifier's CommitObserver and blocks produced locally with real
 *        executed roots (the TestEthereumBlockSync harness idiom: synthetic Ethereum headers,
 *        funded accounts, decoder map, FakeLedger, memory-backed MultiLayerStorage) are
 *        driven through verifyAndCommit — the commit path Ethereum EL-mode devp2p block sync
 *        uses. Asserts:
 *          (a) every committed block fires the pruner: the just-committed root reads count 1
 *              with no deadline, and the watermark advances to the head;
 *          (b) past the window the committed "/mpt/" node-row count plateaus (bounded) —
 *              deletions land PHYSICALLY via the tombstones riding each block's WriteBatch;
 *          (c) roots inside [head-N, head] keep their node rows, older roots are gone from
 *              the backend (existsOne against the committed backend, not observer
 *              bookkeeping);
 *          (d) the sync-lane height guard is unaffected: replaying an already-committed
 *              block still throws the typed StaleOrOutOfOrderBlock before any state fork and
 *              the pruner sees nothing of it;
 *          (e) the committed balances match the executed transfers (committing works).
 *        Deletions land synchronously inside verifyAndCommit (coPreparePruneRows' batch
 *        merges with the block's prewriteStorage), so every assertion runs against the
 *        committed state with no worker to drain.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include "bcos-ledger/mpt/MPTPruner.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/EthereumHost.h"
#include "EthereumBlockHashLookup.h"
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

// Anonymous namespace + MPS prefix: this TU is compiled standalone (it defines the same
// MultiLayerStorage aliases as TestEthereumBlockSync.cpp / TestEthereumExecutorScheduler.cpp).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;
namespace mpt = bcos::ledger::mpt;

using MPSMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using MPSBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using MPSCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, MPSBackendStorage>;
using MPSMultiLayerStorage = MultiLayerStorage<MPSMutableStorage, void, MPSCheckpointBackend>;

/// The committed-state backend type: MultiLayerStorage::latestBackend() is the checkpoint
/// storage's OPENED handle — what the pruner is parameterized on (same decltype idiom as
/// TestMPTPrunerWiring).
using MPSBackend =
    std::remove_cvref_t<decltype(std::declval<MPSMultiLayerStorage&>().latestBackend())>;
using MPSPruner = mpt::MPTPruner<MPSBackend>;

constexpr int64_t c_pruneWindow = 1;

static const u256 MPSFunding = u256(1000000000000000000ULL);  // 1 ETH
static const u256 MPSBaseFee = u256(1000000000);              // 1 gwei

evmc_address MPSAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

class MPSTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

task::Task<void> MPSFundAccount(MPSBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<MPSBackendStorage> acc(storage, addr, AddressTableMode::Hex);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> MPSReadBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, AddressTableMode::Hex);
    co_return co_await acc.balance();
}

task::Task<void> MPSWriteBlockHash(
    MPSBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
}

task::Task<void> MPSWriteCurrentNumber(MPSBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
}

task::Task<void> MPSWriteSystemConfig(
    MPSBackendStorage& storage, std::string_view key, std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

/// Web3-shaped EIP-1559 value-transfer tx (maxFeePerGas must cover the block base fee).
std::shared_ptr<MPSTestTransactionImpl> MPSMakeWeb3TransferTx(
    evmc_address const& sender, evmc_address const& recipient, uint64_t value,
    std::string const& nonce, uint64_t maxFeePerGas)
{
    auto tx = std::make_shared<MPSTestTransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.data.version = 1;
    inner.data.to = bcos::toHexStringWithPrefix(
        bcos::bytes(std::begin(recipient.bytes), std::end(recipient.bytes)));
    inner.data.blockLimit = 1000;
    inner.data.chainID = "0x1";
    inner.data.nonce = nonce;
    inner.data.value = [&] {
        std::ostringstream oss;
        oss << "0x" << std::hex << value;
        return oss.str();
    }();
    inner.data.gasPrice = "0x0";
    inner.data.gasLimit = 100000;
    inner.data.maxFeePerGas = [&] {
        std::ostringstream oss;
        oss << "0x" << std::hex << maxFeePerGas;
        return oss.str();
    }();
    inner.data.maxPriorityFeePerGas = "0x0";
    inner.type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = 2;  // EIP-1559

    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));                // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));                // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));                // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(maxFeePerGas));     // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));           // gasLimit
    bcos::codec::rlp::encode(
        body, bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes))));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(value));  // value
    bcos::codec::rlp::encode(body, bcos::bytes{});                 // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);              // empty accessList
    bcos::bytes payloadBytes;
    payloadBytes.push_back(0x02);
    bcos::codec::rlp::encodeHeader(payloadBytes,
        bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payloadBytes.insert(payloadBytes.end(), body.begin(), body.end());
    inner.extraTransactionBytes.assign(payloadBytes.begin(), payloadBytes.end());

    bcos::bytes signature(65, 0);
    signature[31] = 0x12;
    signature[63] = 0x34;
    signature[64] = 0x01;
    inner.signature.assign(signature.begin(), signature.end());

    tx->forceSender(bcos::bytes(std::begin(sender.bytes), std::end(sender.bytes)));
    tx->calculateHash(*bcos::test::createNormalCryptoSuite()->hashImpl());
    tx->markClean();
    return tx;
}

/// A PoS header skeleton with real per-block roots filled in by the caller.
bcos::protocol::EthBlockHeaderData MPSBaseHeader(
    int64_t number, int64_t timestamp, bcos::h256 parentHash, uint64_t gasLimit, bcos::u256 baseFee)
{
    bcos::protocol::EthBlockHeaderData header;
    header.number = number;
    header.timestamp = timestamp;
    header.parentInfo.blockNumber = number - 1;
    header.parentInfo.blockHash = parentHash;
    header.difficulty = 0;
    header.uncleHash = bcos::protocol::c_emptyOmmersHash;
    header.gasLimit = gasLimit;
    header.gasUsed = 0;
    header.baseFee = baseFee;
    header.prevRandao = bcos::h256{};
    header.coinbase = bcos::Address{};
    header.nonce = bcos::h64{};
    header.stateRoot = ledger::mpt::emptyRootHash();
    header.txsRoot = ledger::mpt::emptyRootHash();
    header.receiptsRoot = ledger::mpt::emptyRootHash();
    return header;
}

/// Whether the node row of @p hash physically exists in the committed backend.
bool nodeRowInBackend(MPSBackend& backend, h256 const& hash)
{
    return task::syncWait(storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
}

/// Committed "/mpt/" node-row count in the backend — the physical-deletion oracle. The table
/// name is taken from a probe key (mptNodeStateKey is the single source of the key layout).
task::Task<size_t> mptNodeRowCount(MPSBackend& backend)
{
    auto const probe = bcos::ledger::mptNodeStateKey(h256{});
    auto const mptTable = executor_v1::StateKeyView{probe}.m_table;
    size_t count = 0;
    auto iterator = co_await storage2::range(backend);
    while (auto item = co_await iterator.next())
    {
        if (executor_v1::StateKeyView{std::get<0>(*item)}.m_table == mptTable)
        {
            ++count;
        }
    }
    co_return count;
}

class MPSFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    MPSBackendStorage backendStorage;
    MPSCheckpointBackend checkpointBackend{backendStorage};
    MPSMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    MPSFixture()
    {
        blockHashLookup = [&backend = backendStorage](
                              int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
            return initializer::ethBlockHashLookupFromStorage(
                backend, blockNumber, currentHeight);
        };
        executor = std::make_shared<EthereumExecutor>(receiptFactory, blockHashLookup);
        blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(MPTPrunerSyncWiringSuite)

// Produce a 5-block chain locally with real executed roots (separate MultiLayerStorage over
// the same backend, so the production layer stack cannot leak into the verification side),
// then replay it through EthereumBlockVerifier::verifyAndCommit with a live MPTPruner
// (window N=1) as the CommitObserver. Every block is one sender->recipient transfer, so every
// block produces a fresh state root and obsoletes the previous one — a new trie version per
// block.
BOOST_FIXTURE_TEST_CASE(prunerWiredIntoSyncCommitPath, MPSFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testMPSSync");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = MPSAddress(7);
        auto recipient = MPSAddress(0x21);
        co_await MPSFundAccount(backendStorage, sender, MPSFunding);
        co_await MPSFundAccount(backendStorage, recipient, 0);

        // The genesis (block-0) Ethereum header: its keccak(rlp) is the chain anchor.
        auto genesisHeader = MPSBaseHeader(0, 1600000000, bcos::h256{}, 30000000, MPSBaseFee);
        genesisHeader.gasUsed = 0;
        auto genesisHash = bcos::protocol::ethHeaderHash(genesisHeader);
        co_await MPSWriteBlockHash(backendStorage, 0, genesisHash);
        {
            storage::Entry entry;
            entry.set("0");
            co_await storage2::writeOne(backendStorage,
                executor_v1::StateKey{
                    ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(genesisHash)},
                std::move(entry));
        }
        co_await MPSWriteCurrentNumber(backendStorage, 0);
        co_await MPSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await MPSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        // ---- The REAL pruner over the committed-state backend, wired as the verifier's
        //      CommitObserver. Boot at genesis on the Ethereum lane (the executor_version
        //      SYS_CONFIG row above): firstMptBlock is 0, so init walks the head root — the
        //      EMPTY genesis trie (emptyRootHash) — and ends with trackedCount 0; the first
        //      block's full build seeds the counts through the ordinary delta path, exactly
        //      as in the PBFT-lane wiring test. The sync lane's headers are served by peers,
        //      not read from the local ledger, so the boot lookup is backed by the roots this
        //      test commits.
        auto& backend = multiLayerStorage.latestBackend();
        auto pruner = std::make_shared<MPSPruner>(backend, c_pruneWindow);
        std::map<protocol::BlockNumber, h256> committedRoots;
        committedRoots[0] = genesisHeader.stateRoot;
        MPSPruner::StateRootLookup stateRootAt =
            [&committedRoots](protocol::BlockNumber number) -> task::Task<std::optional<h256>> {
            auto const it = committedRoots.find(number);
            co_return it != committedRoots.end() ? std::optional<h256>{it->second} : std::nullopt;
        };
        co_await pruner->init(
            0, ledger::ETHEREUM_EXECUTOR_VERSION, stateRootAt, /*sweepGarbage=*/false);
        BOOST_CHECK_EQUAL(pruner->trackedCount(), 0U);

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, pruner);

        std::map<bcos::bytes, protocol::Transaction::Ptr> rawToTx;
        auto decoder = [&rawToTx](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return rawToTx.at(raw);
        };

        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;    // London/Paris/Shanghai active from genesis (explicit 0;
        forks.parisTime = 0;     // unset fields default to UINT64_MAX = never active)
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        using ViewType = MPSMultiLayerStorage::ViewType;
        // v2 (executor_version=2) must NOT reach the injected legacy fold — the verifier
        // computes the MPT state root itself. Throwing here proves the v2 branch won.
        Verifier::StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        // ---- Production side: build the chain with real executed values. ----
        MPSCheckpointBackend prodCheckpoint{backendStorage};
        MPSMultiLayerStorage prodStorage{prodCheckpoint};

        constexpr protocol::BlockNumber c_head = 5;
        const uint64_t kGasLimit = 30000000;
        std::map<protocol::BlockNumber, h256> roots;
        std::map<protocol::BlockNumber, size_t> nodeCounts;
        std::vector<bcos::protocol::EthBlockHeaderData> ethHeaders;
        std::vector<bcos::bytes> raws;
        u256 gasCost = 0;

        auto prevEthHeader = genesisHeader;
        auto prevHash = genesisHash;
        for (protocol::BlockNumber number = 1; number <= c_head; ++number)
        {
            auto const timestamp = 1600000000 + number;
            auto tx = MPSMakeWeb3TransferTx(
                sender, recipient, 100, std::to_string(number - 1), 1000000000);
            auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
                tx->extraTransactionBytes(), tx->signatureData());
            rawToTx[raw] = tx;

            // Base fee recomputed per EIP-1559 from the parent header.
            auto baseFee = bcos::devp2p::sync::computeNextBaseFee(prevEthHeader);
            gasCost += u256(21000) * baseFee;

            ledger::LedgerConfig prodConfig;
            prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
            prodConfig.setEVMCRevision(EVMC_SHANGHAI);
            prodConfig.setGasLimit({kGasLimit, number});
            prodConfig.setGasPrice({scheduler_v1::u256ToHexString(baseFee), number});
            prodConfig.setDifficulty(0);

            bcostars::protocol::BlockHeaderImpl prodHeader;
            prodHeader.setNumber(number);
            prodHeader.setTimestamp(timestamp * 1000L);
            prodHeader.setParentInfo({number - 1, prevHash});
            prodHeader.setGasLimit(u256(kGasLimit));
            prodHeader.calculateHash(*cryptoSuite->hashImpl());

            auto view = prodStorage.fork();
            view.newMutable();
            std::vector<protocol::Transaction::Ptr> txs{tx};
            auto receipts = co_await scheduler.executeBlock(
                view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
            BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
            BOOST_CHECK_EQUAL(receipts[0]->status(), 0);
            auto comp = co_await Verifier::computeEthereumRoots(
                receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});
            // MPT state root from the parent's root; the trie nodes written by the previous
            // block's build are reachable through the pushed production layers.
            auto stateRoot =
                co_await Verifier::computeMptStateRoot(view, prevEthHeader.stateRoot, prodConfig);
            prodStorage.pushView(std::move(view));

            auto ethHeader = MPSBaseHeader(number, timestamp, prevHash, kGasLimit, baseFee);
            ethHeader.stateRoot = stateRoot;
            ethHeader.txsRoot = comp.txsRoot;
            ethHeader.receiptsRoot = comp.receiptsRoot;
            ethHeader.gasUsed = comp.gasUsed;
            ethHeader.logsBloom = comp.logsBloom;

            // ---- Verification side: the devp2p-sync commit path, with the real pruner. ----
            auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger,
                ethHeader, prevEthHeader, std::vector<bcos::bytes>{raw},
                std::optional<std::vector<bcos::bytes>>{}, forks, 1, std::vector<bcos::bytes>{},
                0, decoder, stateRootCalc);
            BOOST_REQUIRE_MESSAGE(result.valid,
                "block " << number << " invalid: " << result.error);
            // FakeLedger::asyncPrewriteBlock is a no-op, so the commit does not advance
            // SYS_KEY_CURRENT_NUMBER the way the real Ledger::prewriteBlock
            // (Ledger.cpp:296-300) does — write the row the real commit would have written,
            // so the verifier's head+1 guard sees the advanced head for the next block.
            co_await MPSWriteCurrentNumber(backendStorage, number);

            roots[number] = stateRoot;
            committedRoots[number] = stateRoot;
            nodeCounts[number] = co_await mptNodeRowCount(backend);
            ethHeaders.push_back(ethHeader);
            raws.push_back(raw);
            prevHash = bcos::protocol::ethHeaderHash(ethHeader);
            prevEthHeader = ethHeader;

            // (a) The just-committed root: exactly one reference, no deadline — the block's
            //     commit really fired the pruner's hooks.
            BOOST_CHECK(pruner->countOf(roots[number]) == std::optional<uint64_t>{1});
            BOOST_CHECK(!pruner->deadlineOf(roots[number]).has_value());
        }
        BOOST_CHECK_EQUAL(pruner->watermark(), c_head);
        BOOST_CHECK_GT(pruner->trackedCount(), 0U);

        // (b) Bounded, converged node count: with N=1 the root of block r is obsoleted at
        //     block r+1 and deleted when block r+2 commits, so deletions start at block 3;
        //     from then on each block adds one trie version and deletes the one that fell
        //     out of the window, so the committed node-row count plateaus.
        BOOST_REQUIRE_EQUAL(nodeCounts[3], nodeCounts[4]);
        BOOST_REQUIRE_EQUAL(nodeCounts[4], nodeCounts[5]);
        BOOST_CHECK_LE(nodeCounts[5], nodeCounts[2]);  // never exceeds the pre-deletion level
        BOOST_CHECK_GT(nodeCounts[5], 0);

        // (c) Window guarantee with N=1 at head 5: roots of blocks 4..5 (head-N .. head)
        //     keep their node rows; roots 1..3 were deleted at blocks 3..5 and are
        //     PHYSICALLY gone from the backend.
        for (protocol::BlockNumber number = 4; number <= 5; ++number)
        {
            BOOST_CHECK_MESSAGE(nodeRowInBackend(backend, roots[number]),
                "in-window root of block " + std::to_string(number) + " was pruned");
        }
        for (protocol::BlockNumber number = 1; number <= 3; ++number)
        {
            BOOST_CHECK_MESSAGE(!nodeRowInBackend(backend, roots[number]),
                "out-of-window root of block " + std::to_string(number) + " still on disk");
        }
        // The root of block 1 was obsoleted at block 2 and deleted at block 3 — its
        // in-memory entry is erased with the deletion; a still-live root reads count 1.
        BOOST_CHECK(!pruner->countOf(roots[1]).has_value());
        BOOST_CHECK(pruner->countOf(roots[5]) == std::optional<uint64_t>{1});

        // (d) The sync-lane height guard is unaffected: replaying an already-committed
        //     block (a stale resume point from a second peer) throws the TYPED
        //     StaleOrOutOfOrderBlock before any state fork, and the pruner sees nothing.
        auto const trackedBefore = pruner->trackedCount();
        bool staleRejected = false;
        try
        {
            co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeaders[1],
                ethHeaders[0], std::vector<bcos::bytes>{raws[1]},
                std::optional<std::vector<bcos::bytes>>{}, forks, 1, std::vector<bcos::bytes>{},
                0, decoder, stateRootCalc);
        }
        catch (StaleOrOutOfOrderBlock const& e)
        {
            BOOST_CHECK(std::string(e.what()).find("not the ledger head + 1") !=
                        std::string::npos);
            staleRejected = true;
        }
        BOOST_CHECK(staleRejected);
        BOOST_CHECK_EQUAL(pruner->watermark(), c_head);
        BOOST_CHECK_EQUAL(pruner->trackedCount(), trackedBefore);
        BOOST_CHECK_EQUAL(co_await mptNodeRowCount(backend), nodeCounts[c_head]);
        {
            auto headView = multiLayerStorage.fork();
            auto head = co_await ledger::getCurrentBlockNumber(headView, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, c_head);
        }

        // (e) Committing worked: the five transfers and their gas fees landed.
        auto balanceRecipient = co_await MPSReadBalance(backend, recipient);
        BOOST_CHECK_EQUAL(balanceRecipient, u256(500));
        auto balanceSender = co_await MPSReadBalance(backend, sender);
        BOOST_CHECK_EQUAL(balanceSender, MPSFunding - u256(500) - gasCost);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
