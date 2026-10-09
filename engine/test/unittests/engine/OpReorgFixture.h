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
 * @file OpReorgFixture.h
 * @brief Shared fixture for the OP unfinalized-window tests: a real OpScheduler<MLS> +
 *        OpEngineService over in-memory backends, every block produced through the engine's
 *        own build path (FCU+attrs → getPayload → newPayload). The chain follows D1 §2.1:
 *        bal(0xAA) is 10 at B1, 20 at B2a, 15 at B2b (mint deposits of 10/10/5).
 *
 * Used by OpEngineReorgTest.cpp (import side) and OpReadPlaneTest.cpp (RPC read side). Free
 * functions are inline: the test binary is a unity build and the two users may land in
 * different batches.
 */
#pragma once

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
    /// Observes the current block number of the view a payload build seals against (the
    /// parent chain's SYS_CURRENT_STATE): OpReadPlaneTest pins that it is the head chain's,
    /// not the finalized plane's. Unset = plain stub.
    std::function<void(bcos::protocol::BlockNumber)> onSeal;

    void removeByHash(std::span<bcos::crypto::HashType const>) {}
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t, View& view, OutputIt)
    {
        if (onSeal)
        {
            onSeal(bcos::task::syncWait(
                bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)));
        }
    }
};

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, EngineOpScheduler>;
using Delegate = bcos::executor_v1::opstack::OpScheduler<MLS>;
using bcos::engine::PayloadValidationStatus;

constexpr uint64_t kChainId = 0x2105;
constexpr int64_t kGenesisTimestampMs = 1'699'000'000'000;
constexpr uint64_t kB1TimestampMs = 1'700'000'000'000ULL;
constexpr uint64_t kBlockTimeMs = 2000;

inline bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

inline bcos::protocol::BlockFactory::Ptr makeBlockFactory()
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
inline bcos::bytes l1AttributesDeposit(uint8_t salt)
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
inline bcos::bytes mintDeposit(uint8_t salt, uint64_t amount)
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

inline BaseChain setupB1B2a(ReorgFixture& f)
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

inline Sibling deriveB2b(ReorgFixture& f, BaseChain const& c)
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
