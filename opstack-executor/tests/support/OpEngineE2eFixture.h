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
 * @file OpEngineE2eFixture.h
 * @brief In-process fixture wiring the OP engine service to a real ledger over memory storage. */

#pragma once

// Shared OpEngineService e2e fixture for opstack-executor block-tests, rebuilt on the
// post-cutover API: the seam is constructed from (OpForkSchedule, OpEthL1BlockInfo)
// — timestamps select forks via ledger::resolveOpFork, NOT a boolean flag — and its
// own synthesizeL1AttributesEnvelope(l2Ts, parentTs) already reproduces the
// Isthmus/Jovian layout rule, so the old test-only override is gone.

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-ledger/mpt/Constants.h>     // emptyRootHash
#include <bcos-ledger/mpt/HashBuilder.h>   // flushTrieNodes
#include <bcos-ledger/mpt/StateRoots.h>    // computeMptStateDelta
#include "GenesisTrie.h"  // copyFlatRows + computeAndPersistParentTrie
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-table/src/LegacyStorageWrapper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <json/json.h>
#include <opstack-executor/OpEthL1Attributes.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <engine/bcos-engine/OpEngineService.inl>

#include <memory>
#include <string>
#include <vector>

namespace opstack_e2e
{

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

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
    memory_storage::Attribute(memory_storage::CONCURRENT | memory_storage::ORDERED),
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

constexpr uint64_t c_chainId = 0x2105;

inline bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

inline bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

inline bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// Fork schedule for the e2e fixtures: Isthmus as the zero-start baseline, with
/// optional Jovian at 1s (the pre-cutover `legacy(bool)` shape, now via timestamps —
/// `jovian=true` ⇔ jovian activates at second 1, `false` ⇔ all-Isthmus).
inline bcos::ledger::OpForkSchedule scheduleFor(bool jovian)
{
    bcos::ledger::OpForkSchedule schedule;
    if (jovian)
    {
        schedule.m_jovianTime = 1;
    }
    return schedule;
}

/// The seam's L1Block snapshot. Number/time/blockHash/baseFeeScalar/batcherHash are
/// non-zero on purpose: synthesizeOpEthL1AttributesEnvelope fail-closes on both
/// sentinels (isUnsetOpEthL1BlockInfo / isUnsetOpEthSystemConfig), and the FCU-build
/// suites (S7/S8/S9) drive exactly that path. The fee amounts stay zero — the pricing
/// assertions read the 1559 sources (parent extraData / chain config), never the
/// L1Block content. Named for what it returns (a SEEDED snapshot, not the unset
/// sentinel) — the old name outlived the sentinel fix.
inline bcos::executor_v1::opstack::OpEthL1BlockInfo seededL1BlockInfo()
{
    bcos::executor_v1::opstack::OpEthL1BlockInfo info{};
    info.number = 1;
    info.time = 1;
    info.blockHash.bytes[31] = 0x01;
    info.baseFeeScalar = 1;
    info.batcherHash.bytes[31] = 0x01;
    return info;
}

inline void seedSysTables(MLS& multiLayerStorage)
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
inline void registerVerifiedBlock(
    MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(hashEntry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline void registerGoldenParentHeader(MLS& multiLayerStorage,
    bcos::protocol::BlockFactory::Ptr const& blockFactory, Json::Value const& env, bool jovian,
    bcos::h256 const& stateRoot)
{
    auto quantity = [](std::string const& hex) {
        auto const digits = hex.rfind("0x", 0) == 0 ? hex.substr(2) : hex;
        return std::stoull(digits, nullptr, 16);
    };
    auto const currentNumber = quantity(env["currentNumber"].asString());
    auto const parentNumber = static_cast<int64_t>(currentNumber - 1);
    auto const parentTimestampMs =
        static_cast<int64_t>((quantity(env["currentTimestamp"].asString()) - 1) * 1000ULL);
    auto const gasLimit = static_cast<int64_t>(quantity(env["currentGasLimit"].asString()));
    auto const baseFee = bcos::u256(quantity(env["currentBaseFee"].asString()));
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(parentNumber);
    header->setTimestamp(parentTimestampMs);
    header->setGasLimit(gasLimit);
    header->setGasUsed(gasLimit / 6);
    header->setBaseFee(baseFee);
    header->setBlobGasUsed(0);
    header->setStateRoot(stateRoot);
    header->setExtraData(jovian ? bcos::fromHex("0100000032000000060000000000000000") :
                                  bcos::fromHex("000000003200000006"));
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(parentNumber)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

/// Seed parent header from execution payload fields when the vector has no `env` block
/// (invalid_* fork carriers, inline invalid samples that only carry `_op_payload`).
inline void registerParentHeaderFromPayload(MLS& multiLayerStorage,
    bcos::protocol::BlockFactory::Ptr const& blockFactory, Json::Value const& payload, bool jovian,
    bcos::h256 const& stateRoot)
{
    auto quantity = [](std::string const& hex) {
        auto const digits = hex.rfind("0x", 0) == 0 ? hex.substr(2) : hex;
        return std::stoull(digits, nullptr, 16);
    };
    auto const blockNumber = quantity(payload["blockNumber"].asString());
    if (blockNumber < 1)
    {
        return;
    }
    auto const parentNumber = static_cast<int64_t>(blockNumber - 1);
    auto const parentTimestampMs =
        static_cast<int64_t>((quantity(payload["timestamp"].asString()) - 1) * 1000ULL);
    auto const gasLimit = static_cast<int64_t>(quantity(payload["gasLimit"].asString()));
    auto const baseFee = bcos::u256(quantity(payload["baseFeePerGas"].asString()));
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(parentNumber);
    header->setTimestamp(parentTimestampMs);
    header->setGasLimit(gasLimit);
    header->setGasUsed(gasLimit / 6);
    header->setBaseFee(baseFee);
    header->setBlobGasUsed(0);
    header->setStateRoot(stateRoot);
    header->setExtraData(jovian ? bcos::fromHex("0100000032000000060000000000000000") :
                                  bcos::fromHex("000000003200000006"));
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(parentNumber)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

/// Seed parent hash mapping plus the parent header row OpEngineService::newPayload reads.
inline void registerParentForNewPayload(MLS& multiLayerStorage,
    bcos::protocol::BlockFactory::Ptr const& blockFactory, Json::Value const& vector, bool jovian,
    bcos::h256 const& parentHash, int64_t parentNumber = 0)
{
    registerVerifiedBlock(multiLayerStorage, parentHash, parentNumber);
    // The delegate's incremental MPT build at the payload's block resolves the parent
    // header's stateRoot against persisted "/mpt/" nodes — build + persist the seeded
    // pre-state's trie first (callers always seedPreState before this helper).
    auto const stateRoot = opstack_test::computeAndPersistParentTrie(multiLayerStorage);
    if (vector.isMember("env"))
    {
        registerGoldenParentHeader(
            multiLayerStorage, blockFactory, vector["env"], jovian, stateRoot);
    }
    else if (vector.isMember("_op_payload"))
    {
        registerParentHeaderFromPayload(
            multiLayerStorage, blockFactory, vector["_op_payload"], jovian, stateRoot);
    }
}

inline std::vector<bcos::bytes> rawEnvelopesFromPayload(
    bcos::engine::ExecutionPayload const& payload)
{
    std::vector<bcos::bytes> envelopes;
    envelopes.reserve(payload.transactions.size());
    for (auto const& tx : payload.transactions)
    {
        envelopes.push_back(tx.raw);
    }
    return envelopes;
}

inline bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    auto envelopes = rawEnvelopesFromPayload(payload);
    const auto transactionsRoot = EngineOpScheduler::computeTxRoot(envelopes);
    return bcos::engine::engine_common::op::rebuildOpEthHeader(
        blockFactory->blockHeaderFactory(), payload, transactionsRoot,
        request.parentBeaconBlockRoot.value_or(bcos::h256{}));
}

inline bcos::protocol::Transaction::Ptr buildFiscoTxFromEnvelope(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx =
        bcos::engine::engine_common::op::opEnvelopeToTars(env, txHash, /*allowDeposit=*/true);
    if (!tarsTx)
        return nullptr;
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsBody = std::move(*tarsTx)]() mutable { return &tarsBody; });
}

struct OpE2eFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory;
    EngineOpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> opDelegate;
    OpEngine service;

    explicit OpE2eFixture(bool jovian)
      : OpE2eFixture(scheduleFor(jovian))
    {}

    /// Explicit schedule: lets a case pin a historical fork window (e.g. Regolith) while the
    /// engine, the seam and the real OpScheduler delegate all share it.
    /// `eip1559` stays an optional so a fixture can be honestly undeclared (the engine prices and
    /// warns exactly as a node without [op_eip1559] does) instead of passing the preset in.
    explicit OpE2eFixture(bcos::ledger::OpForkSchedule schedule,
        std::optional<bcos::engine::OpEip1559Params> eip1559 = std::nullopt)
      : hashImpl(makeCryptoSuite()->hashImpl()),
        receiptFactory(makeReceiptFactory()),
        scheduler(schedule, seededL1BlockInfo()),  // copy: opDelegate below moves it
        legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                backendStorage)),
        ledger(std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        opDelegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
            hashImpl, c_chainId, schedule, blockFactory, multiLayerStorage, ledger,
            ioServicePool)),
        service(memPool, multiLayerStorage, scheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, opDelegate, nullptr,
            /*allowSynthesizedL1Attributes=*/true, eip1559)
    {
        seedSysTables(multiLayerStorage);
    }
};

}  // namespace opstack_e2e
