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
 * Shared reversible commits through the production Ethereum block verifier.
 */

#include "EthereumBlockHashLookup.h"
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
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/Ledger.h"
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
using bcos::executor_v1::eth::EthereumExecutor;
using namespace bcos::scheduler_v1;

using RBMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using RBMemoryStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;

struct RBBackendStorage : RBMemoryStorage
{
    bool rejectNextMerge = false;

    task::Task<void> merge(auto&... layers)
    {
        if (std::exchange(rejectNextMerge, false))
        {
            throw std::runtime_error("injected batch write failure");
        }
        co_await RBMemoryStorage::merge(layers...);
    }
};

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

task::Task<void> RBWriteSystemConfig(
    RBBackendStorage& storage, std::string_view key, std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

/// Web3-shaped EIP-1559 value-transfer tx (same builder idiom as TestMPTPrunerSyncWiring).
std::shared_ptr<RBTestTransactionImpl> RBMakeWeb3TransferTx(evmc_address const& sender,
    evmc_address const& recipient, uint64_t value, std::string const& nonce, uint64_t maxFeePerGas)
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
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));                   // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(std::stoull(nonce)));  // nonce
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
    bcos::codec::rlp::encodeHeader(
        payloadBytes, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
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
    auto header =
        RBBaseHeader(parent.number + 1, parent.timestamp + 1, bcos::protocol::ethHeaderHash(parent),
            gasLimit, bcos::devp2p::sync::computeNextBaseFee(parent));
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
    bcos::protocol::BlockFactory::Ptr blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    std::shared_ptr<bcos::storage::StorageInterface> durable =
        std::make_shared<bcos::storage::LegacyStorageWrapper<RBBackendStorage>>(backendStorage);
    bcos::ledger::Ledger chainLedger{blockFactory, durable, 1000};

    RBFixture()
    {
        blockHashLookup = [&backend = backendStorage](
                              int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
            return initializer::ethBlockHashLookupFromStorage(backend, blockNumber, currentHeight);
        };
        executor = std::make_shared<EthereumExecutor>(receiptFactory, blockHashLookup);
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
    auto header = fixture.blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setStateRoot(chain.genesisHeader.stateRoot);
    header->setRLPHash(chain.genesisHash);
    auto block = fixture.blockFactory->createBlock();
    block->setBlockHeader(header);
    co_await ledger::prewriteBlockToStorage(
        fixture.chainLedger, nullptr, block, false, fixture.durable);
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
    EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>& verifier, RBChain& chain,
    protocol::BlockNumber count)
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
        auto stateRoot = co_await std::remove_reference_t<decltype(verifier)>::computeMptStateRoot(
            view, prevEthHeader.stateRoot, prodConfig);
        prodStorage.pushView(std::move(view));

        auto ethHeader = RBBaseHeader(
            number, timestamp, bcos::protocol::ethHeaderHash(prevEthHeader), kGasLimit, baseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = comp.txsRoot;
        ethHeader.receiptsRoot = comp.receiptsRoot;
        ethHeader.gasUsed = comp.gasUsed;
        ethHeader.logsBloom = comp.logsBloom;

        auto result =
            co_await verifier.verifyAndCommit(fixture.multiLayerStorage, fixture.chainLedger,
                ethHeader, prevEthHeader, std::vector<bcos::bytes>{raw}, std::vector<bcos::bytes>{},
                forks, 1, std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc);
        BOOST_REQUIRE_MESSAGE(result.valid, "block " << number << " invalid: " << result.error);
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

        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, /*reversible=*/true);
        co_await RBDriveChain(*this, scheduler, verifier, chain, 4);

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
        auto invalid = forkHeader;
        invalid.stateRoot = chain.roots.at(4);
        auto rejected = co_await verifier.verifyAndCommit(multiLayerStorage, chainLedger, invalid,
            chain.headers[1], std::vector<bcos::bytes>{}, std::vector<bcos::bytes>{}, forks, 1,
            std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc);
        BOOST_CHECK(!rejected.valid);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));
        auto committed = multiLayerStorage.forkCommitted();
        BOOST_CHECK_EQUAL(
            co_await ledger::getCurrentBlockNumber(committed, ledger::fromStorage), 4);

        backendStorage.rejectNextMerge = true;
        BOOST_CHECK_THROW(
            co_await verifier.verifyAndCommit(multiLayerStorage, chainLedger, forkHeader,
                chain.headers[1], std::vector<bcos::bytes>{}, std::vector<bcos::bytes>{}, forks, 1,
                std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc),
            std::runtime_error);
        BOOST_CHECK_EQUAL(
            co_await ledger::getCurrentBlockNumber(committed, ledger::fromStorage), 4);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "4"));

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, chainLedger, forkHeader,
            chain.headers[1], std::vector<bcos::bytes>{}, std::vector<bcos::bytes>{}, forks, 1,
            std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc);
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
            BOOST_CHECK_MESSAGE(n == 3 || !rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH,
                                              std::to_string(n)),
                "number->hash of rolled-back block " << n << " survived");
            auto const hashBinary = chain.hashes[n].asBytes();
            BOOST_CHECK_MESSAGE(
                !rowInBackend(backendStorage, ledger::SYS_HASH_2_NUMBER,
                    std::string_view(
                        reinterpret_cast<char const*>(hashBinary.data()), hashBinary.size())),
                "hash->number of rolled-back block " << n << " survived");
        }
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "3"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "4"));
        // Blocks 1..2 keep their rows.
        for (protocol::BlockNumber n = 1; n <= 2; ++n)
        {
            BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_NUMBER_2_HASH, std::to_string(n)));
            BOOST_CHECK(
                rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, std::to_string(n)));
        }
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 3);
            auto counts = co_await ledger::getTransactionCount(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(counts.total, 2);
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

        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, /*reversible=*/true);
        co_await RBDriveChain(*this, scheduler, verifier, chain, 4);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));

        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "1"));
        BOOST_CHECK(rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "2"));
        auto view = multiLayerStorage.forkCommitted();
        auto meta = co_await ledger::loadEngineMeta(view);
        auto finalized = meta;
        finalized.safe = finalized.finalized = ledger::BlockAnchor{2, chain.hashes.at(2)};
        RBMutableStorage finalization;
        co_await stageFinalization(finalization, meta, finalized);
        co_await multiLayerStorage.mergeToBackends(finalization);
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "1"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "2"));
        BOOST_CHECK_THROW(co_await verifier.rollbackChain(multiLayerStorage, 1), RollbackRefused);
        // (c2) Not behind the head — refused.
        BOOST_CHECK_THROW(co_await verifier.rollbackChain(multiLayerStorage, 4), RollbackRefused);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));

        // Journals 3 and 4 can still rewind to the finalized anchor.
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

        // The finalized anchor remains the lower boundary after the rollback.
        BOOST_CHECK_THROW(co_await verifier.rollbackChain(multiLayerStorage, 1), RollbackRefused);
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

        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory, nullptr, /*reversible=*/true);
        co_await RBDriveChain(*this, scheduler, verifier, chain, 4);

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
            co_await verifier.verifyAndCommit(multiLayerStorage, chainLedger, bogus,
                nonCanonicalParent, std::vector<bcos::bytes>{}, std::vector<bcos::bytes>{}, forks,
                1, std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc),
            StaleOrOutOfOrderBlock);
        BOOST_CHECK_EQUAL(co_await RBReadBalance(backendStorage, chain.recipient), u256(400));
        {
            auto view = multiLayerStorage.forkCommitted();
            auto head = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            BOOST_CHECK_EQUAL(head, 4);
        }
    }());
}

// Direct commit fixtures keep the sequential height guard.
BOOST_FIXTURE_TEST_CASE(disabledWindowJournalsNothing, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBDisabled");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);

        using Verifier = EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>;
        Verifier verifier(scheduler, *executor, *blockFactory);  // Direct commit fixture
        co_await RBDriveChain(*this, scheduler, verifier, chain, 2);

        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "1"));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_ROLLBACK_JOURNAL, "2"));

        // Direct commit fixtures cannot replace a committed block.
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
            co_await verifier.verifyAndCommit(multiLayerStorage, chainLedger, forkHeader,
                chain.headers[0], std::vector<bcos::bytes>{}, std::vector<bcos::bytes>{}, forks, 1,
                std::vector<bcos::bytes>{}, 0, decoder, stateRootCalc),
            StaleOrOutOfOrderBlock);
        // And the explicit entry point refuses immediately.
        BOOST_CHECK_THROW(co_await verifier.rollbackChain(multiLayerStorage, 0), RollbackRefused);
    }());
}

BOOST_FIXTURE_TEST_CASE(virtualUndoAndDiskUndoSharePreimages, RBFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testRBUndo");
        SchedulerSerialImpl scheduler(ioServicePool);
        RBChain chain;
        co_await RBSetupGenesis(*this, chain);
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory, nullptr, /*reversible=*/true);
        co_await RBDriveChain(*this, scheduler, verifier, chain, 4);
        auto committed = multiLayerStorage.forkCommitted();
        auto meta = co_await ledger::loadEngineMeta(committed);
        ledger::BlockAnchor target{2, chain.hashes.at(2)};
        auto overlay = co_await forkRollbackView(multiLayerStorage, target, meta, *blockFactory);
        BOOST_CHECK_EQUAL(co_await ledger::getCurrentBlockNumber(overlay, ledger::fromStorage), 2);
        BOOST_CHECK_EQUAL(
            co_await ledger::getCurrentBlockNumber(committed, ledger::fromStorage), 4);

        auto removed = co_await ledger::getBlockTransactionHashList(committed, 4, *blockFactory);
        BOOST_REQUIRE_EQUAL(removed.size(), 1);
        auto txKey = bcos::concepts::bytebuffer::toView(removed.front());
        for (auto table : {ledger::SYS_HASH_2_TX, ledger::SYS_HASH_2_RECEIPT})
        {
            BOOST_CHECK(rowInBackend(backendStorage, table, txKey));
            BOOST_CHECK(!co_await storage2::readOne(overlay, StateKeyView{table, txKey}));
        }
        auto row = co_await storage2::readOne(committed, journalKey(3));
        BOOST_REQUIRE(row);
        auto raw = row->get();
        auto journal = decodeRollbackJournal(
            bytesConstRef(reinterpret_cast<byte const*>(raw.data()), raw.size()));
        journal.parentHash = chain.hashes.at(1);
        storage::Entry corrupt;
        corrupt.set(encodeRollbackJournal(journal));
        co_await storage2::writeOne(backendStorage, journalKey(3), std::move(corrupt));
        BOOST_CHECK_THROW(co_await verifier.rollbackChain(multiLayerStorage, 2), RollbackRefused);
        BOOST_CHECK_EQUAL(
            co_await ledger::getCurrentBlockNumber(committed, ledger::fromStorage), 4);
        co_await storage2::writeOne(backendStorage, journalKey(3), std::move(*row));

        co_await verifier.rollbackChain(multiLayerStorage, 2);
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_HASH_2_TX, txKey));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_HASH_2_RECEIPT, txKey));
        BOOST_CHECK(!rowInBackend(backendStorage, ledger::SYS_NUMBER_2_WITHDRAWALS, "4"));
        BOOST_CHECK((co_await ledger::loadEngineMeta(committed)).diskTip == target);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
