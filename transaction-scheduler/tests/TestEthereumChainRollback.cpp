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
 * @file TestEthereumChainRollback.cpp
 * @brief Phase 3 EL shallow-reorg (EthereumChainRollback.h) end-to-end over the
 *        devp2p-sync commit lane — same harness idiom as TestMPTPrunerSyncWiring
 *        (synthetic Ethereum headers, funded accounts, decoder map, FakeLedger,
 *        memory-backed MultiLayerStorage). Asserts:
 *          (a) the stale-reorg retry: a fork block at a committed height whose parent IS
 *              the canonical block at number-1 rolls the chain back inside
 *              verifyAndCommit and commits on top — flat balances, the ledger head and
 *              the number/hash ledger rows all reflect the fork afterwards;
 *          (b) the explicit rollbackChain entry point restores the target block's state
 *              (balances back to the target height, rolled-back blocks' rows deleted,
 *              consumed journals removed);
 *          (c) refusals: a target beyond the reorg window, a target not behind the head,
 *              and a target whose journal the window already pruned all throw
 *              RollbackRefused WITHOUT touching the committed state;
 *          (d) a stale block whose parent is NOT canonical still throws
 *              StaleOrOutOfOrderBlock (no rollback happens);
 *          (e) window 0 keeps the pre-Phase-3 behavior: no journal rows are written and a
 *              stale replay throws StaleOrOutOfOrderBlock;
 *          (f) a rolled-back blob block's SYS_NUMBER_2_BLOBS row is deleted with the rest
 *              of the number-keyed rows, and a fork block at the same height lands its own.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/EthereumChainRollback.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "EthereumBlockHashLookup.h"
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

// Anonymous namespace + RB prefix: this TU is compiled standalone (it defines the same
// MultiLayerStorage aliases as the other verifier harness TUs).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using RBMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using RBBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using RBCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, RBBackendStorage>;
using RBMultiLayerStorage = MultiLayerStorage<RBMutableStorage, void, RBCheckpointBackend>;

static const u256 RBFunding = u256(1000000000000000000ULL);  // 1 ETH
static const u256 RBBaseFee = u256(1000000000);              // 1 gwei

evmc_address RBAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

class RBTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

task::Task<void> RBFundAccount(RBBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<RBBackendStorage> acc(storage, addr, nodeAddressTableMode());
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

task::Task<u256> RBReadBalance(RBBackendStorage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<RBBackendStorage> acc(storage, addr, nodeAddressTableMode());
    co_return co_await acc.balance();
}

task::Task<void> RBWriteBlockHash(
    RBBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
    storage::Entry numberEntry;
    numberEntry.set(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)},
        std::move(numberEntry));
}

task::Task<void> RBWriteCurrentNumber(RBBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
}

task::Task<void> RBWriteSystemConfig(
    RBBackendStorage& storage, std::string_view key, std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

/// Web3-shaped EIP-1559 value-transfer tx (same builder idiom as TestMPTPrunerSyncWiring).
std::shared_ptr<RBTestTransactionImpl> RBMakeWeb3TransferTx(
    evmc_address const& sender, evmc_address const& recipient, uint64_t value,
    std::string const& nonce, uint64_t maxFeePerGas)
{
    auto tx = std::make_shared<RBTestTransactionImpl>();
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
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));             // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));             // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));             // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(maxFeePerGas));  // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));        // gasLimit
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
bcos::protocol::EthBlockHeaderData RBBaseHeader(
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
    // Shanghai is active from genesis in this suite: every block commits to the
    // empty withdrawals trie and carries an empty withdrawals list.
    header.withdrawalsHash = ledger::mpt::emptyRootHash();
    return header;
}

/// An EMPTY fork block on top of @p parent (no transactions): the state root is the
/// parent's, every deterministic root is the empty value, and the base fee follows
/// EIP-1559 from the parent header.
bcos::protocol::EthBlockHeaderData RBEmptyChildHeader(
    bcos::protocol::EthBlockHeaderData const& parent, uint64_t gasLimit)
{
    auto header = RBBaseHeader(parent.number + 1, parent.timestamp + 1,
        bcos::protocol::ethHeaderHash(parent), gasLimit,
        bcos::devp2p::sync::computeNextBaseFee(parent));
    header.stateRoot = parent.stateRoot;
    return header;
}

/// Whether a row physically exists in the committed backend.
bool rowInBackend(RBBackendStorage& backend, std::string_view table, std::string_view key)
{
    return task::syncWait(storage2::existsOne(backend, StateKeyView{table, key}));
}

class RBFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    RBBackendStorage backendStorage;
    RBCheckpointBackend checkpointBackend{backendStorage};
    RBMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    RBFixture()
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

/// Everything one chain-building run needs, bundled so several tests can share the
/// scenario: a funded sender, genesis rows, and a driveChain() that produces + commits
/// `count` transfer blocks through the verifier (the devp2p-sync lane), returning the
/// per-block headers/roots for later assertions.
struct RBChain
{
    evmc_address sender = RBAddress(7);
    evmc_address recipient = RBAddress(0x21);
    bcos::protocol::EthBlockHeaderData genesisHeader;
    bcos::h256 genesisHash;
    std::vector<bcos::protocol::EthBlockHeaderData> headers;  // headers[i-1] = block i
    std::map<protocol::BlockNumber, bcos::h256> hashes;
    std::map<protocol::BlockNumber, bcos::h256> roots;
    std::map<bcos::bytes, protocol::Transaction::Ptr> rawToTx;
    u256 gasCost = 0;
};

constexpr uint64_t kGasLimit = 30000000;

task::Task<void> RBSetupGenesis(RBFixture& fixture, RBChain& chain)
{
    co_await RBFundAccount(fixture.backendStorage, chain.sender, RBFunding);
    co_await RBFundAccount(fixture.backendStorage, chain.recipient, 0);
    chain.genesisHeader = RBBaseHeader(0, 1600000000, bcos::h256{}, kGasLimit, RBBaseFee);
    chain.genesisHash = bcos::protocol::ethHeaderHash(chain.genesisHeader);
    co_await RBWriteBlockHash(fixture.backendStorage, 0, chain.genesisHash);
    co_await RBWriteCurrentNumber(fixture.backendStorage, 0);
    co_await RBWriteSystemConfig(fixture.backendStorage,
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    co_await RBWriteSystemConfig(fixture.backendStorage,
        std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");
    chain.roots[0] = chain.genesisHeader.stateRoot;
    chain.hashes[0] = chain.genesisHash;
}

/// Produce + commit blocks 1..count through @p verifier (each one sender->recipient
/// transfer of 100 wei), exactly like TestMPTPrunerSyncWiring's loop: the production side
/// runs on a SEPARATE MultiLayerStorage over the same backend, then the block replays
/// through verifyAndCommit on the fixture's storage.
task::Task<void> RBDriveChain(RBFixture& fixture, SchedulerSerialImpl& scheduler,
    EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>& verifier,
    bcos::test::FakeLedger& fakeLedger, RBChain& chain, protocol::BlockNumber count)
{
    scheduler_v1::EvmcForkTimestamps forks;
    forks.londonTime = 0;
    forks.parisTime = 0;
    forks.shanghaiTime = 0;
    using ViewType = RBMultiLayerStorage::ViewType;
    EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::StateRootCalculator<ViewType>
        stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto decoder = [&chain](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return chain.rawToTx.at(raw);
    };

    RBCheckpointBackend prodCheckpoint{fixture.backendStorage};
    RBMultiLayerStorage prodStorage{prodCheckpoint};

    auto prevEthHeader = chain.genesisHeader;
    for (protocol::BlockNumber number = 1; number <= count; ++number)
    {
        auto const timestamp = 1600000000 + number;
        auto tx = RBMakeWeb3TransferTx(
            chain.sender, chain.recipient, 100, std::to_string(number - 1), 1000000000);
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());
        chain.rawToTx[raw] = tx;

        auto baseFee = bcos::devp2p::sync::computeNextBaseFee(prevEthHeader);
        chain.gasCost += u256(21000) * baseFee;

        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig.setGasLimit({kGasLimit, number});
        prodConfig.setGasPrice({scheduler_v1::u256ToHexString(baseFee), number});
        prodConfig.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(number);
        prodHeader.setTimestamp(timestamp * 1000L);
        prodHeader.setParentInfo({number - 1, bcos::protocol::ethHeaderHash(prevEthHeader)});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*fixture.cryptoSuite->hashImpl());

        auto view = prodStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *fixture.executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);
        auto comp = co_await std::remove_reference_t<decltype(verifier)>::computeEthereumRoots(
            receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});
        auto stateRoot =
            co_await std::remove_reference_t<decltype(verifier)>::computeMptStateRoot(view,
                prevEthHeader.stateRoot, prodConfig);
        prodStorage.pushView(std::move(view));

        auto ethHeader = RBBaseHeader(number, timestamp,
            bcos::protocol::ethHeaderHash(prevEthHeader), kGasLimit, baseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = comp.txsRoot;
        ethHeader.receiptsRoot = comp.receiptsRoot;
        ethHeader.gasUsed = comp.gasUsed;
        ethHeader.logsBloom = comp.logsBloom;

        auto result = co_await verifier.verifyAndCommit(fixture.multiLayerStorage, fakeLedger,
            ethHeader, prevEthHeader, std::vector<bcos::bytes>{raw},
            std::vector<bcos::bytes>{}, forks, 1, std::vector<bcos::bytes>{}, 0,
            decoder, stateRootCalc);
        BOOST_REQUIRE_MESSAGE(result.valid, "block " << number << " invalid: " << result.error);
        // FakeLedger::asyncPrewriteBlock is a no-op: write the ledger metadata rows the real
        // Ledger::prewriteBlock would have written (same as TestMPTPrunerSyncWiring).
        co_await RBWriteCurrentNumber(fixture.backendStorage, number);
        co_await RBWriteBlockHash(
            fixture.backendStorage, number, bcos::protocol::ethHeaderHash(ethHeader));

        chain.headers.push_back(ethHeader);
        chain.hashes[number] = bcos::protocol::ethHeaderHash(ethHeader);
        chain.roots[number] = stateRoot;
        prevEthHeader = ethHeader;
    }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumChainRollbackSuite)

// (a) The stale-reorg retry: commit chain A1..A4, then feed the verifier a fork block B3
// whose parent is the canonical A2 — the verifier must rewind to 2 and commit B3, all
// inside verifyAndCommit.
BOOST_FIXTURE_TEST_CASE(staleReorgRetryRollsBackAndCommits, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBStale");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        constexpr int64_t c_reorgWindow = 4;
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, c_reorgWindow);
        co_await RBDriveChain(*this, scheduler, verifier, *fakeLedger, chain, 4);

        // Pre-reorg sanity: head 4, the recipient holds 4x100 wei, journals 1..4 exist.
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));
        for (protocol::BlockNumber n = 1; n <= 4; ++n)
        {
            BOOST_CHECK_MESSAGE(
                rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, std::to_string(n)),
                "journal missing before the reorg at block " << n);
        }

        // The fork block: EMPTY block B3 on the canonical A2 (state root = root(A2)).
        auto forkHeader = RBEmptyChildHeader(chain.headers[1], kGasLimit);
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        using ViewType = RBMultiLayerStorage::ViewType;
        Verifier::StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };
        auto decoder = [&chain](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return chain.rawToTx.at(raw);
        };
        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger,
            forkHeader, chain.headers[1], std::vector<bcos::bytes>{},
            std::vector<bcos::bytes>{}, forks, 1, std::vector<bcos::bytes>{}, 0,
            decoder, stateRootCalc);
        BOOST_REQUIRE_MESSAGE(result.valid, "fork block invalid: " << result.error);
        BOOST_CHECK_EQUAL(result.stateRoot, chain.roots[2]);

        // The rollback rewound flat state to block 2: the recipient is back to 200 wei.
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(200));

        // The rolled-back blocks' number-keyed rows are gone: A3/A4's number->hash,
        // A3/A4's hash->number (the engine idempotence invariant) and A4's journal.
        // Journal row 3 EXISTS again — it is B3's own journal, written by the fork block's
        // commit after the rollback consumed A3's.
        for (protocol::BlockNumber n = 3; n <= 4; ++n)
        {
            BOOST_CHECK_MESSAGE(
                !rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH, std::to_string(n)),
                "number->hash of rolled-back block " << n << " survived");
            auto const hashBinary = chain.hashes[n].asBytes();
            BOOST_CHECK_MESSAGE(!rowInBackend(backendStorage, ledger::SYS_HASH_2_NUMBER,
                                    std::string_view(reinterpret_cast<char const*>(
                                                         hashBinary.data()),
                                        hashBinary.size())),
                "hash->number of rolled-back block " << n << " survived");
        }
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "3"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "4"));
        // Blocks 1..2 keep their rows.
        for (protocol::BlockNumber n = 1; n <= 2; ++n)
        {
            BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH,
                std::to_string(n)));
            BOOST_CHECK(rowInBackend(
                backendStorage, ledger::SYS_ROLLBACK_JOURNAL, std::to_string(n)));
        }
        // FakeLedger never wrote current_number for the fork block — the rollback set it to
        // 2 and the (no-op) FakeLedger prewrite left it there; the next block's height guard
        // must see head 3 only after the test writes the row the real ledger would. Here
        // the rollback's own write is what's stored:
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 2);
        }
    }());
}

// (b)+(c) The explicit rollbackChain entry point and every refusal shape.
BOOST_FIXTURE_TEST_CASE(rollbackChainRestoresAndRefuses, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBExplicit");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        constexpr int64_t c_reorgWindow = 2;
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, c_reorgWindow);
        co_await RBDriveChain(*this, scheduler, verifier, *fakeLedger, chain, 4);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));

        // Window pruning: commits 3 and 4 deleted the journals of blocks 1 and 2
        // (expired = N - window).
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "1"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "2"));
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "3"));
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "4"));

        // (c1) Beyond the window: depth 3 > 2 — refused, state untouched.
        BOOST_CHECK_THROW(
            co_await verifier.rollbackChain(multiLayerStorage, 1), RollbackRefused);
        // (c2) Not behind the head — refused.
        BOOST_CHECK_THROW(
            co_await verifier.rollbackChain(multiLayerStorage, 4), RollbackRefused);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));

        // (b) The good rollback: 4 -> 2 (depth 2 == window, journals 3 and 4 present).
        auto result = co_await verifier.rollbackChain(multiLayerStorage, 2);
        BOOST_CHECK_EQUAL(result.oldHead, 4);
        BOOST_CHECK_EQUAL(result.newHead, 2);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(200));
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 2);
        }
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "3"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "4"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH, "3"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH, "4"));

        // (c3) Journal pruned by the window: rolling further to block 1 needs journal 2,
        //      which commit 4 already deleted — refused, and block 2's state stays.
        BOOST_CHECK_THROW(
            co_await verifier.rollbackChain(multiLayerStorage, 1), RollbackRefused);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(200));
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 2);
        }
    }());
}

// (d) A stale block whose parent is NOT canonical gets no retry: StaleOrOutOfOrderBlock,
// and the committed chain is untouched.
BOOST_FIXTURE_TEST_CASE(staleNonCanonicalParentStillThrows, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBNonCanon");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        constexpr int64_t c_reorgWindow = 4;
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, c_reorgWindow);
        co_await RBDriveChain(*this, scheduler, verifier, *fakeLedger, chain, 4);

        // A "block 3" that follows a NON-canonical parent cleanly: B2 is a plausible
        // block-2 header (A2 with different extraData, hence a different hash) that is
        // NOT the canonical A2. The stateless parent-relative check (step 1b) passes;
        // the retry's canonical-parent check (B2's hash != the canonical A2 at
        // number-1 = 2) fails, so the original stale throw must surface.
        auto nonCanonicalParent = chain.headers[1];
        nonCanonicalParent.extraData = bcos::bytes{bcos::byte{0x42}};
        auto bogus = RBEmptyChildHeader(nonCanonicalParent, kGasLimit);
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        using ViewType = RBMultiLayerStorage::ViewType;
        Verifier::StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };
        auto decoder = [&chain](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return chain.rawToTx.at(raw);
        };
        BOOST_CHECK_THROW(
            co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, bogus,
                nonCanonicalParent, std::vector<bcos::bytes>{},
                std::vector<bcos::bytes>{}, forks, 1, std::vector<bcos::bytes>{},
                0, decoder, stateRootCalc),
            StaleOrOutOfOrderBlock);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 4);
        }
    }());
}

// (e) Window 0 (the default) writes no journals and keeps the plain stale throw.
BOOST_FIXTURE_TEST_CASE(disabledWindowJournalsNothing, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBDisabled");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory);  // reorgWindow defaults to 0
        co_await RBDriveChain(*this, scheduler, verifier, *fakeLedger, chain, 2);

        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "1"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "2"));

        // A canonical-parent fork block gets NO retry with the window disabled.
        auto forkHeader = RBEmptyChildHeader(chain.headers[0], kGasLimit);
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        using ViewType = RBMultiLayerStorage::ViewType;
        Verifier::StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };
        auto decoder = [&chain](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return chain.rawToTx.at(raw);
        };
        BOOST_CHECK_THROW(
            co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, forkHeader,
                chain.headers[0], std::vector<bcos::bytes>{},
                std::vector<bcos::bytes>{}, forks, 1, std::vector<bcos::bytes>{},
                0, decoder, stateRootCalc),
            StaleOrOutOfOrderBlock);
        // And the explicit entry point refuses immediately.
        BOOST_CHECK_THROW(
            co_await verifier.rollbackChain(multiLayerStorage, 0), RollbackRefused);
    }());
}

// (f) A blob-carrying block's SYS_NUMBER_2_BLOBS row (written by the engine lane's commit
// for locally built blocks, served by engine_getBlobsV*) must not survive a rollback:
// the rolled-back heights lose the row, and a fork block committing at the same height
// afterwards lands its own row cleanly.
BOOST_FIXTURE_TEST_CASE(rollbackDeletesBlobRows, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBBlobs");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        constexpr int64_t c_reorgWindow = 4;
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, c_reorgWindow);
        co_await RBDriveChain(*this, scheduler, verifier, *fakeLedger, chain, 4);

        // Simulate the engine-lane commit rows for blob-carrying blocks 3 and 4
        // (FakeLedger::asyncPrewriteBlock is a no-op, so the test writes them the same
        // way it writes the number/hash rows).
        auto writeBlobRow = [this](protocol::BlockNumber number,
                                bcos::bytes payload) -> task::Task<void> {
            storage::Entry entry;
            entry.set(std::move(payload));
            co_await storage2::writeOne(backendStorage,
                StateKey{ledger::SYS_NUMBER_2_BLOBS, std::to_string(number)}, std::move(entry));
        };
        auto readBlobRow = [this](protocol::BlockNumber number) -> task::Task<bcos::bytes> {
            auto entry = co_await storage2::readOne(backendStorage,
                StateKeyView{ledger::SYS_NUMBER_2_BLOBS, std::to_string(number)});
            BOOST_REQUIRE(entry);
            co_return bcos::bytes(entry->get().begin(), entry->get().end());
        };
        co_await writeBlobRow(3, {bcos::byte{0xA3}});
        co_await writeBlobRow(4, {bcos::byte{0xA4}});
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_NUMBER_2_BLOBS, "3"));
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_NUMBER_2_BLOBS, "4"));

        auto result = co_await verifier.rollbackChain(multiLayerStorage, 2);
        BOOST_CHECK_EQUAL(result.oldHead, 4);
        BOOST_CHECK_EQUAL(result.newHead, 2);
        BOOST_CHECK_MESSAGE(!rowInBackend(backendStorage, ledger::SYS_NUMBER_2_BLOBS, "3"),
            "blob row of rolled-back block 3 survived");
        BOOST_CHECK_MESSAGE(!rowInBackend(backendStorage, ledger::SYS_NUMBER_2_BLOBS, "4"),
            "blob row of rolled-back block 4 survived");

        // The fork block committing at height 3 writes its own row; nothing stale
        // lingers underneath it.
        co_await writeBlobRow(3, {bcos::byte{0xB3}});
        BOOST_CHECK((co_await readBlobRow(3)) == bcos::bytes{bcos::byte{0xB3}});
    }());
}

BOOST_AUTO_TEST_SUITE_END()
