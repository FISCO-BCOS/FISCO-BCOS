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
 * @file OpEngineReorgTest.cpp
 * @brief Unfinalized window on the OP Engine lane: D1 §15 rows 1-8 and the §12.3 boundaries.
 *
 * Real OpScheduler<MLS> over in-memory backends, every block produced through the engine's own
 * build path (FCU+attrs → getPayload → newPayload) so no commitment is hand-computed. The chain
 * follows D1 §2.1: bal(0xAA) is 10 at B1, 20 at B2a, 15 at B2b (mint deposits of 10/10/5).
 */

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/StateRoots.h>
#include <bcos-ledger/mpt/ViewNodeStorage.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-table/src/LegacyStorageWrapper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <opstack-executor/OpEthDeposit.h>
#include <opstack-executor/OpEthL1Attributes.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <engine/bcos-engine/OpEngineService.inl>
#include <evmc/evmc.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace opeth = bcos::executor_v1::opstack;

namespace op_engine_reorg
{

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

struct StubMemPool
{
    void removeByHash(std::span<bcos::crypto::HashType const>) {}
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t, View&, OutputIt)
    {}
};

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, EngineOpScheduler>;
using Delegate = bcos::executor_v1::opstack::OpScheduler<MLS>;
using bcos::engine::PayloadValidationStatus;

constexpr uint64_t kChainId = 0x2105;
constexpr int64_t kGenesisTimestampMs = 1'699'000'000'000;
constexpr uint64_t kB1TimestampMs = 1'700'000'000'000ULL;
constexpr uint64_t kBlockTimeMs = 2000;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(cryptoSuite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite));
}

/// The account whose balance the D1 example tracks (0xAA…AA) and its deposit target.
const evmc::address kAccount = [] {
    evmc::address a{};
    std::memset(a.bytes, 0xAA, sizeof(a.bytes));
    return a;
}();
const evmc::address kSink = [] {
    evmc::address a{};
    std::memset(a.bytes, 0xBB, sizeof(a.bytes));
    return a;
}();

/// L1-attributes deposit (to OP_L1_BLOCK from OP_DEPOSITOR, content-only recognition). Every OP
/// block must open with one; @p salt keeps the tx hash unique per block.
bcos::bytes l1AttributesDeposit(uint8_t salt)
{
    opeth::DepositTx dep;
    std::memset(dep.sourceHash.bytes, salt, sizeof(dep.sourceHash.bytes));
    dep.sourceHash.bytes[0] = 0x01;
    dep.from = evmc::literals::operator""_address("0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001");
    dep.to = evmc::literals::operator""_address("0x4200000000000000000000000000000000000015");
    dep.mint = std::nullopt;
    dep.value = bcos::u256{0};
    dep.gasLimit = 1'000'000;
    dep.isSystemTx = false;
    return opeth::encodeOpEthDepositEnvelope(dep);
}

/// A deposit minting @p amount wei to kAccount (value 0, so the balance rises by exactly that).
bcos::bytes mintDeposit(uint8_t salt, uint64_t amount)
{
    opeth::DepositTx dep;
    std::memset(dep.sourceHash.bytes, salt, sizeof(dep.sourceHash.bytes));
    dep.sourceHash.bytes[0] = 0x02;
    dep.from = kAccount;
    dep.to = kSink;
    dep.mint = bcos::u256(amount);
    dep.value = bcos::u256{0};
    dep.gasLimit = 1'000'000;
    dep.isSystemTx = false;
    return opeth::encodeOpEthDepositEnvelope(dep);
}

template <class From, class To>
bcos::task::Task<void> copyFlatRows(From& from, To& to)
{
    auto it = co_await bcos::storage2::range(from);
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        if (auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(v)))
            co_await bcos::storage2::writeOne(to, k, *entry);
    }
}

struct ReorgFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite())};
    bcos::ledger::OpForkSchedule forkSchedule{};  // Isthmus baseline for every block
    EngineOpScheduler seam{forkSchedule, {}};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage{
        std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(backendStorage)};
    std::shared_ptr<bcos::ledger::Ledger> ledger{
        std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    int64_t unfinalizedWindow;
    std::shared_ptr<Delegate> opDelegate;
    std::unique_ptr<OpEngine> service;
    bcos::h256 genesisHash{std::string(64, 'a')};
    std::vector<bcos::protocol::BlockNumber> finalizeNotifications;

    explicit ReorgFixture(int64_t window = bcos::engine::c_defaultUnfinalizedWindow)
      : unfinalizedWindow(window)
    {
        seedSysTables();
        seedGenesis();
        start();
    }

    /// (Re)create the scheduler and the service over the SAME backend: the window and the
    /// tracker are process memory, so this is the kill -9 restart of D1 §13.
    void start()
    {
        service.reset();
        opDelegate = std::make_shared<Delegate>(receiptFactory, hashImpl, kChainId, forkSchedule,
            blockFactory, multiLayerStorage, ledger, ioServicePool);
        opDelegate->setBlockNumberNotifier(
            [this](bcos::protocol::BlockNumber n) { finalizeNotifications.push_back(n); });
        service = std::make_unique<OpEngine>(memPool, multiLayerStorage, seam, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, opDelegate, nullptr,
            /*allowSynthesizedL1Attributes=*/false, unfinalizedWindow);
    }
    void restart() { start(); }

    void seedSysTables()
    {
        auto view = multiLayerStorage.fork();
        view.newMutable();
        constexpr std::string_view sysTables[] = {bcos::ledger::SYS_CURRENT_STATE,
            bcos::ledger::SYS_HASH_2_TX, bcos::ledger::SYS_HASH_2_NUMBER,
            bcos::ledger::SYS_NUMBER_2_HASH, bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
            bcos::ledger::SYS_NUMBER_2_TXS, bcos::ledger::SYS_HASH_2_RECEIPT,
            bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES};
        for (auto const& table : sysTables)
        {
            bcos::storage::Entry e;
            e.set(std::string(bcos::ledger::SYS_VALUE));
            bcos::task::syncWait(bcos::storage2::writeOne(
                view, StateKey{bcos::ledger::SYS_TABLES, std::string(table)}, std::move(e)));
        }
        bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
    }

    /// Genesis = block 0 with the (empty) scenario-B state trie persisted and the four ledger
    /// rows every later resolution reads: header by number, hash↔number, current_number.
    void seedGenesis()
    {
        bcos::h256 stateRoot;
        {
            auto readView = multiLayerStorage.fork();
            auto view = multiLayerStorage.fork();
            view.newMutable();
            bcos::task::syncWait(copyFlatRows(readView, view));
            bcos::ledger::LedgerConfig ledgerConfig;
            ledgerConfig.setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
            auto delta = bcos::task::syncWait(bcos::ledger::mpt::computeMptStateDelta(
                view, bcos::ledger::mpt::emptyRootHash(), ledgerConfig));
            bcos::ledger::mpt::ViewNodeStorage<ViewType> nodeStorage(view);
            bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, delta.newNodes));
            bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
            stateRoot = delta.stateRoot;
        }
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(0);
        header->setTimestamp(kGenesisTimestampMs);
        header->setStateRoot(stateRoot);
        header->setGasLimit(30'000'000);
        header->setGasUsed(0);
        header->setBaseFee(bcos::u256(1'000'000'000));
        header->setBlobGasUsed(0);
        header->setExtraData(bcos::fromHex("00000000fa00000006"));  // Holocene 250/6
        bcos::bytes encoded;
        header->encode(encoded);

        auto view = multiLayerStorage.fork();
        view.newMutable();
        auto put = [&](std::string_view table, std::string key, auto value) {
            bcos::storage::Entry entry;
            entry.set(std::move(value));
            bcos::task::syncWait(
                bcos::storage2::writeOne(view, StateKey{table, std::move(key)}, std::move(entry)));
        };
        put(bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, "0", std::move(encoded));
        put(bcos::ledger::SYS_HASH_2_NUMBER,
            std::string(bcos::concepts::bytebuffer::toView(genesisHash)), std::string("0"));
        put(bcos::ledger::SYS_NUMBER_2_HASH, "0", genesisHash.asBytes());
        put(bcos::ledger::SYS_CURRENT_STATE, std::string(bcos::ledger::SYS_KEY_CURRENT_NUMBER),
            std::string("0"));
        bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
    }

    // ---- Engine API drivers ----

    static bcos::engine::ForkchoiceState fc(
        bcos::h256 const& head, bcos::h256 const& safe, bcos::h256 const& finalized)
    {
        return bcos::engine::ForkchoiceState{head, safe, finalized};
    }

    /// Attributes for the child of @p parentNumber: the L1-attributes deposit first, then one
    /// mint deposit of @p mint wei to kAccount (0 = none). Salted per (height, mint) so sibling
    /// blocks carry distinct transactions.
    static bcos::engine::PayloadAttributes attrs(
        bcos::protocol::BlockNumber childNumber, uint64_t mint)
    {
        bcos::engine::PayloadAttributes a;
        a.timestamp = kB1TimestampMs + kBlockTimeMs * static_cast<uint64_t>(childNumber - 1);
        a.prevRandao = bcos::h256(std::string(64, '2'));
        a.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
        a.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
        a.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
        a.gasLimit = 30'000'000;
        a.eip1559Params = bcos::bytes(8, 0);
        a.minBaseFee = std::nullopt;  // Isthmus attributes carry no Jovian floor
        a.noTxPool = true;
        auto const salt = static_cast<uint8_t>(childNumber * 16 + (mint % 16));
        std::vector<std::string> txs{bcos::toHexStringWithPrefix(l1AttributesDeposit(salt))};
        if (mint != 0)
        {
            txs.push_back(bcos::toHexStringWithPrefix(mintDeposit(salt, mint)));
        }
        a.transactions = std::move(txs);
        return a;
    }

    bcos::engine::ForkchoiceUpdatedResult fcu(bcos::engine::ForkchoiceState const& state,
        bcos::engine::PayloadAttributes const* attributes = nullptr)
    {
        return bcos::task::syncWait(service->updateForkchoice(state, attributes, 3));
    }

    bcos::engine::PayloadStatus np(bcos::engine::NewPayloadRequest const& request)
    {
        return bcos::task::syncWait(service->newPayload(request, 4));
    }

    /// FCU(head=parent, attrs) → getPayload: the request op-node would send back.
    bcos::engine::NewPayloadRequest build(bcos::engine::ForkchoiceState const& state,
        bcos::protocol::BlockNumber childNumber, uint64_t mint)
    {
        auto attributes = attrs(childNumber, mint);
        auto built = fcu(state, &attributes);
        BOOST_REQUIRE_MESSAGE(built.payloadStatus.status == PayloadValidationStatus::Valid,
            "build FCU for block " << childNumber << " not VALID"
                                   << (built.payloadStatus.validationError ?
                                              ": " + *built.payloadStatus.validationError :
                                              std::string{}));
        BOOST_REQUIRE_MESSAGE(built.payloadId.has_value(),
            "build FCU for block " << childNumber << " returned no payloadId");
        auto payload = bcos::task::syncWait(service->getPayload(*built.payloadId, 4));
        BOOST_REQUIRE(payload);
        bcos::engine::NewPayloadRequest request;
        request.executionRequests = std::vector<bcos::bytes>{};
        request.executionPayload = payload->executionPayload;
        request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
        request.expectedBlobVersionedHashes = {};
        return request;
    }

    static bcos::h256 hashOf(bcos::engine::NewPayloadRequest const& request)
    {
        return request.executionPayload.blockHash;
    }

    /// keccak of the mint deposit envelope carried by @p request (its second transaction).
    bcos::h256 mintTxHash(bcos::engine::NewPayloadRequest const& request) const
    {
        BOOST_REQUIRE_GE(request.executionPayload.transactions.size(), 2U);
        return hashImpl->hash(request.executionPayload.transactions[1].raw);
    }

    // ---- Read plane ----

    bcos::u256 balanceIn(ViewType& view) const
    {
        bcos::ledger::account::EVMAccount account(view, bcos::ledger::account::FromTableName{},
            bcos::ledger::account::ethLaneAccountTableName(kAccount));
        return bcos::task::syncWait(account.balance());
    }

    /// bal(0xAA) on the chain of @p blockHash (viewAt): what `latest` reads once the RPC plane
    /// follows the tracker head.
    bcos::u256 balanceAt(bcos::h256 const& blockHash)
    {
        auto view = bcos::task::syncWait(opDelegate->viewAt(blockHash));
        BOOST_REQUIRE_MESSAGE(view.has_value(), "no view for " << blockHash.abridged());
        return balanceIn(*view);
    }

    bcos::u256 backendBalance()
    {
        auto view = multiLayerStorage.forkCommitted();
        return balanceIn(view);
    }

    bcos::protocol::BlockNumber backendNumber()
    {
        auto view = multiLayerStorage.forkCommitted();
        return bcos::task::syncWait(
            bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
    }

    bool backendHasTx(bcos::h256 const& txHash)
    {
        auto view = multiLayerStorage.forkCommitted();
        auto entry = bcos::task::syncWait(bcos::storage2::readOne(
            view, StateKey{bcos::ledger::SYS_HASH_2_TX,
                      std::string(bcos::concepts::bytebuffer::toView(txHash))}));
        return entry.has_value();
    }

    std::optional<bcos::h256> onChainAt(bcos::h256 const& tip, bcos::protocol::BlockNumber n)
    {
        return bcos::task::syncWait(opDelegate->hashAtHeightOnChain(tip, n));
    }

    bool inWindow(bcos::h256 const& hash) const
    {
        return opDelegate->unfinalizedBlock(hash).has_value();
    }

    std::optional<bcos::h256> trackedHead() const
    {
        auto head = service->trackedHead();
        return head ? std::optional(head->hash) : std::nullopt;
    }
};

/// The D1 §2.1 setup: B1 finalized on disk (bal 10), B2a admitted and tracked as head (bal 20).
struct BaseChain
{
    bcos::engine::NewPayloadRequest b1;
    bcos::engine::NewPayloadRequest b2a;
    bcos::h256 g, h1, h2a;
};

BaseChain setupB1B2a(ReorgFixture& f)
{
    BaseChain c;
    c.g = f.genesisHash;
    c.b1 = f.build(f.fc(c.g, c.g, c.g), 1, 10);
    c.h1 = f.hashOf(c.b1);
    BOOST_REQUIRE(f.np(c.b1).status == PayloadValidationStatus::Valid);
    // FCU(head=safe=finalized=B1): head switch + finalize → B1 leaves the window for disk.
    auto fin = f.fcu(f.fc(c.h1, c.h1, c.h1));
    BOOST_REQUIRE(fin.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(f.backendNumber(), 1);
    BOOST_REQUIRE_EQUAL(f.backendBalance(), bcos::u256(10));
    BOOST_REQUIRE(!f.inWindow(c.h1));

    c.b2a = f.build(f.fc(c.h1, c.h1, c.h1), 2, 10);
    c.h2a = f.hashOf(c.b2a);
    BOOST_REQUIRE(f.np(c.b2a).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(c.h2a, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(f.inWindow(c.h2a));
    BOOST_REQUIRE_EQUAL(f.balanceAt(c.h2a), bcos::u256(20));
    return c;
}

/// Row 1 (derivation reorg, §11.3): FCU(head=B1, attrs) while B2a is tracked → payloadId,
/// NP(B2b), FCU(head=B2b). Returns B2b.
struct Sibling
{
    bcos::engine::NewPayloadRequest b2b;
    bcos::h256 h2b;
};

Sibling deriveB2b(ReorgFixture& f, BaseChain const& c)
{
    Sibling s;
    // The rewind carries the attributes: one FCU switches the head back to B1 and starts the
    // build there. This is the call that used to be swallowed (VALID, no payloadId).
    s.b2b = f.build(f.fc(c.h1, c.h1, c.h1), 2, 5);
    s.h2b = f.hashOf(s.b2b);
    BOOST_REQUIRE_NE(s.h2b.hex(), c.h2a.hex());
    BOOST_REQUIRE_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), c.h1.hex());
    // B2a stays in the window while B2b is built next to it.
    BOOST_REQUIRE(f.inWindow(c.h2a));
    auto status = f.np(s.b2b);
    BOOST_REQUIRE_MESSAGE(status.status == PayloadValidationStatus::Valid,
        "newPayload(B2b) must be VALID (was -32603 before the window)"
            << (status.validationError ? ": " + *status.validationError : std::string{}));
    BOOST_REQUIRE(
        f.fcu(f.fc(s.h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    return s;
}

}  // namespace op_engine_reorg

BOOST_AUTO_TEST_SUITE(OpEngineReorgTest)

using namespace op_engine_reorg;

// D1 §15 row 1 — sibling at the tip via the derivation reorg.
BOOST_AUTO_TEST_CASE(row1_tip_sibling_via_derivation_reorg)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));  // latest follows the head
    BOOST_CHECK_EQUAL(f.balanceAt(c.h2a), bcos::u256(20));  // the sibling keeps its own state
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(10));  // nothing reached the backend
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK(f.inWindow(s.h2b));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    // Both siblings answer for height 2 on their own chain.
    BOOST_CHECK_EQUAL(f.onChainAt(s.h2b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(c.h2a, 2).value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(s.h2b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    // Header of an unfinalized block is served from its window entry.
    auto header = f.service->executedHeader(s.h2b);
    BOOST_REQUIRE(header);
    BOOST_CHECK_EQUAL(header->number(), 2);
    BOOST_CHECK_EQUAL(header->stateRoot().hex(), s.b2b.executionPayload.stateRoot.hex());

    // Negative control: a finalized block that is NOT on the head's chain (B2b under head
    // B2a) is -38002, and the tracker check runs BEFORE finalizeUpTo — nothing is merged,
    // nothing is pruned, the tracker keeps the previous head.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h2a, c.h1, s.h2b)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 1);
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK(f.inWindow(s.h2b));
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    // Same for safe off the head's chain.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h2a, s.h2b, c.h1)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
}

// Rows 2, 3, 4 (+ the prune assertion): depth > 1, rewind to a non-tip ancestor, finalize
// past the side branch.
BOOST_AUTO_TEST_CASE(rows2_3_4_depth_rewind_and_finalize_past_side_branch)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    // Row 2: B3b, B4b on top of B2b (each mints 1 → 16, 17).
    auto b3b = f.build(f.fc(s.h2b, c.h1, c.h1), 3, 1);
    auto h3b = f.hashOf(b3b);
    BOOST_REQUIRE(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h3b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b4b = f.build(f.fc(h3b, c.h1, c.h1), 4, 1);
    auto h4b = f.hashOf(b4b);
    BOOST_REQUIRE(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h4b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h3b), bcos::u256(16));  // B3b executed on B2b's 15, not 20
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), bcos::u256(17));
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 3).value_or(bcos::h256{}).hex(), h3b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    BOOST_CHECK(!f.onChainAt(h4b, 5).has_value());
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 4U);

    // Row 3: back to B2b, a non-tip ancestor. VALID, tracker only.
    auto back = f.fcu(f.fc(s.h2b, c.h1, c.h1));
    BOOST_CHECK(back.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK(!back.payloadId.has_value());
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.service->getHeadBlockNumber().value_or(-1), 2);
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 4U);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);

    // Row 4: finalized advances to B2b (head jumps back to B4b, safe B3b).
    auto const mintA = f.mintTxHash(c.b2a);
    auto const mintB = f.mintTxHash(s.b2b);
    f.finalizeNotifications.clear();
    auto fin = f.fcu(f.fc(h4b, h3b, s.h2b));
    BOOST_CHECK(fin.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.backendNumber(), 2);
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(15));
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 2);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedHash().hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);  // {B3b, B4b}
    BOOST_CHECK(!f.inWindow(c.h2a));
    BOOST_CHECK(!f.inWindow(s.h2b));
    BOOST_CHECK(f.inWindow(h3b));
    BOOST_CHECK(f.inWindow(h4b));
    // The side branch's transactions never reached the backend; the finalized one's did.
    BOOST_CHECK(!f.backendHasTx(mintA));
    BOOST_CHECK(f.backendHasTx(mintB));
    // The block-number notifier fires on finalize only, once per merged block.
    BOOST_REQUIRE_EQUAL(f.finalizeNotifications.size(), 1U);
    BOOST_CHECK_EQUAL(f.finalizeNotifications.front(), 2);
    // Reads through the head chain still work; B2a's view is gone.
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), bcos::u256(17));
    BOOST_CHECK(!bcos::task::syncWait(f.opDelegate->viewAt(c.h2a)).has_value());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    // Running totals across the window chain: B1(2 txs) + B2b(2) at finalized height 2.
    {
        auto view = f.multiLayerStorage.forkCommitted();
        auto total = bcos::task::syncWait(bcos::storage2::readOne(
            view, StateKey{bcos::ledger::SYS_CURRENT_STATE,
                      std::string(bcos::ledger::SYS_KEY_TOTAL_TRANSACTION_COUNT)}));
        BOOST_REQUIRE(total.has_value());
        BOOST_CHECK_EQUAL(std::string(total->get()), "4");
    }

    // §12.3 row 3 / §15 row 7: a head below finalized (B1) is -38002; a pruned sibling is
    // simply unknown (SYNCING) — the node cannot know the height of a hash it dropped.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h1, c.h1, c.h1)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK(
        f.fcu(f.fc(c.h2a, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), h4b.hex());
    // §12.3 row 2: a block at/below finalized with an unknown hash → SYNCING, not INVALID.
    BOOST_CHECK(f.np(c.b2a).status == PayloadValidationStatus::Syncing);
    // §12.3 row 5: finalized neither in the window nor on disk → -38002.
    BOOST_CHECK_THROW(f.fcu(f.fc(h4b, h3b, bcos::h256(std::string(64, 'f')))),
        bcos::engine::InvalidForkchoiceState);
    // The finalized tip itself is a legal head (heartbeat after a full finalize).
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
}

// Row 5 — op-node's backup restore: FCU(head=B2a) without attributes after B2b won.
BOOST_AUTO_TEST_CASE(row5_restore_replaced_sibling)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    auto restore = f.fcu(f.fc(c.h2a, c.h1, c.h1));
    BOOST_CHECK(restore.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK(!restore.payloadId.has_value());
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(c.h2a), bcos::u256(20));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(10));
    // And back again: switching between siblings is tracker-only, any number of times.
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));
}

// Row 6 — restart after row 4: the window is memory, the backend is the finalized chain.
BOOST_AUTO_TEST_CASE(row6_restart_refills_from_finalized)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);
    auto b3b = f.build(f.fc(s.h2b, c.h1, c.h1), 3, 1);
    auto h3b = f.hashOf(b3b);
    BOOST_REQUIRE(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h3b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b4b = f.build(f.fc(h3b, c.h1, c.h1), 4, 1);
    auto h4b = f.hashOf(b4b);
    BOOST_REQUIRE(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(f.backendNumber(), 2);
    auto const balanceBefore = f.balanceAt(h4b);

    f.restart();
    // Nothing tracked, nothing in the window; finalized hydrates from disk on first use.
    BOOST_CHECK(!f.trackedHead().has_value());
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 0U);
    BOOST_CHECK(!f.inWindow(h3b));
    BOOST_CHECK(!f.inWindow(h4b));
    BOOST_CHECK(bcos::task::syncWait(f.opDelegate->viewAt(s.h2b)).has_value());
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 2);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedHash().hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(15));

    // op-node's first FCU names the old head: unknown here → SYNCING (it then resets and
    // re-derives from the tags, which all answer B2b).
    BOOST_CHECK(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    // Refill: the same payloads execute again on top of the finalized tip.
    BOOST_CHECK(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), balanceBefore);
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    // The pruned sibling is gone for good.
    BOOST_CHECK(f.np(c.b2a).status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!f.inWindow(c.h2a));
}

// Row 8 — an externally produced side branch (§11.1, after a restart) arrives with no FCU in
// between: NP(B2a) → NP(B2b) → FCU(head=B2b). B2b is built on a twin node over the same genesis
// so that it is a genuinely external payload here (never staged by this node's build path).
// Also §12.3 row 1: a payload whose parent is unknown → SYNCING.
BOOST_AUTO_TEST_CASE(row8_external_side_branch_and_unknown_parent)
{
    ReorgFixture twin;
    auto tc = setupB1B2a(twin);
    auto external = twin.build(twin.fc(tc.h1, tc.h1, tc.h1), 2, 5);  // B2b, built elsewhere
    auto b3External = [&] {
        auto h2b = twin.hashOf(external);
        BOOST_REQUIRE(twin.np(external).status == PayloadValidationStatus::Valid);
        BOOST_REQUIRE(twin.fcu(twin.fc(h2b, tc.h1, tc.h1)).payloadStatus.status ==
                      PayloadValidationStatus::Valid);
        return twin.build(twin.fc(h2b, tc.h1, tc.h1), 3, 1);  // B3b on the twin
    }();

    ReorgFixture f;
    auto c = setupB1B2a(f);
    BOOST_REQUIRE_EQUAL(c.h1.hex(), tc.h1.hex());  // deterministic twins
    BOOST_REQUIRE_EQUAL(c.h2a.hex(), tc.h2a.hex());

    // §12.3 row 1: B3b's parent (B2b) is unknown here.
    BOOST_CHECK(f.np(b3External).status == PayloadValidationStatus::Syncing);

    // Row 8: the sibling arrives as an external payload — VALID, not SYNCING.
    auto status = f.np(external);
    BOOST_CHECK_MESSAGE(status.status == PayloadValidationStatus::Valid,
        "external sibling must be VALID"
            << (status.validationError ? ": " + *status.validationError : std::string{}));
    auto const h2b = f.hashOf(external);
    BOOST_CHECK(f.inWindow(h2b));
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK(
        f.fcu(f.fc(h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h2b), bcos::u256(15));
    // Now B3b's parent is known: it executes on B2b's chain.
    BOOST_CHECK(f.np(b3External).status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(f.hashOf(b3External)), bcos::u256(16));
    // An honest resend of an admitted payload is VALID without re-execution.
    BOOST_CHECK(f.np(external).status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 3U);
}

// Window depth: newPayload above finalized + unfinalized_window answers SYNCING until the
// finalized tip advances; nothing is lost, the same payload is accepted afterwards.
BOOST_AUTO_TEST_CASE(window_depth_backpressure)
{
    ReorgFixture f(/*window=*/2);
    auto const g = f.genesisHash;
    auto b1 = f.build(f.fc(g, g, g), 1, 10);
    auto h1 = f.hashOf(b1);
    BOOST_REQUIRE(f.np(b1).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(f.fcu(f.fc(h1, g, g)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b2 = f.build(f.fc(h1, g, g), 2, 1);
    auto h2 = f.hashOf(b2);
    BOOST_REQUIRE(f.np(b2).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(f.fcu(f.fc(h2, g, g)).payloadStatus.status == PayloadValidationStatus::Valid);
    // Block 3 would sit 3 above the finalized tip (0) with a window of 2.
    auto b3 = f.build(f.fc(h2, g, g), 3, 1);
    auto h3 = f.hashOf(b3);
    BOOST_CHECK(f.np(b3).status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!f.inWindow(h3));
    // op-node's follow-up FCU naming the unadmitted head is SYNCING too (it retries).
    BOOST_CHECK(f.fcu(f.fc(h3, g, g)).payloadStatus.status == PayloadValidationStatus::Syncing);
    // Finalize B1: the tip moves to 1 and block 3 (depth 2) fits.
    BOOST_REQUIRE(f.fcu(f.fc(h2, h1, h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK(f.np(b3).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(f.fcu(f.fc(h3, h1, h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h3), bcos::u256(12));
}

BOOST_AUTO_TEST_SUITE_END()
