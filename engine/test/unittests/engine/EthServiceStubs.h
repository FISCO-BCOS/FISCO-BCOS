/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EthServiceStubs.h
 * @brief Shared test stubs and forkchoice fixtures for the Engine suites
 *. One definition so EngineServiceTest / Eth parity /
 * Eth transactional cannot drift on hashes or storage seeding.
 */

#pragma once

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage/Serialize.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <magic_enum/magic_enum.hpp>
#include <range/v3/range/concepts.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string_view>

namespace bcos::engine::eth_test
{

using RealGlobalStateMutableStorage = bcos::storage2::memory_storage::MemoryStorage<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
using RealGlobalStateBackendStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::Attribute(
            bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::CONCURRENT),
        std::hash<bcos::executor_v1::StateKey>>;

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& s) : m_storage(s) {}
    Storage& open() { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};

using RealGlobalCheckpointBackend = TrivialCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, RealGlobalStateBackendStorage>;
using RealGlobalStateStorage = bcos::storage2::MultiLayerStorage<RealGlobalStateMutableStorage,
    void, RealGlobalCheckpointBackend>;

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };

    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const protocol::BlockHeader&, const protocol::Transaction&, int,
        const ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&, const protocol::BlockHeader&,
        const protocol::Transaction&, int, const ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

struct StubScheduler
{
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&&, const ledger::LedgerConfig&)
    {
        co_return {};
    }
};

/// Whole-second milliseconds: finalizeEthBlockHeader / validateHeader require
/// a whole number of seconds at the Eth RLP boundary.
inline constexpr std::uint64_t c_defaultPayloadTimestamp = 1700000000ULL * 1000ULL;

inline ForkchoiceState makeForkchoiceState()
{
    return {h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
}

inline PayloadAttributes makePayloadAttributesV2(
    std::uint64_t timestamp = c_defaultPayloadTimestamp)
{
    PayloadAttributes payloadAttributes;
    payloadAttributes.timestamp = timestamp;
    payloadAttributes.prevRandao =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    payloadAttributes.suggestedFeeRecipient = Address("1234567890abcdef1234567890abcdef12345678");
    payloadAttributes.withdrawals = std::vector<WithdrawalV1>{};
    return payloadAttributes;
}

inline PayloadAttributes makePayloadAttributesV3(
    std::uint64_t timestamp = c_defaultPayloadTimestamp)
{
    auto payloadAttributes = makePayloadAttributesV2(timestamp);
    payloadAttributes.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    return payloadAttributes;
}

/// op-node Karst attributes: V3 plus an empty withdrawals list (already the V3
/// default; kept so call sites stay explicit).
inline PayloadAttributes makeKarstPayloadAttributes(
    std::uint64_t timestamp = c_defaultPayloadTimestamp)
{
    return makePayloadAttributesV3(timestamp);
}

inline NewPayloadRequest makeNewPayloadRequestV3(ExecutionPayload const& executionPayload)
{
    NewPayloadRequest request;
    request.executionPayload = executionPayload;
    request.parentBeaconBlockRoot =
        h256("5555555555555555555555555555555555555555555555555555555555555555");
    return request;
}

template <class Backend>
void writeEthExecutorConfig(
    Backend& backend, evmc_revision rev = EVMC_CANCUN, bool writeEvmcRevision = true)
{
    auto writeSysConfig = [&](std::string_view key, std::string value) {
        storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(ledger::SystemConfigEntry{std::move(value), 0}));
        task::syncWait(storage2::writeOne(
            backend, bcos::executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
    };
    writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    if (writeEvmcRevision)
    {
        writeSysConfig(ledger::SYSTEM_KEY_EVMC_REVISION, ledger::encodeEVMCRevisionConfig(rev, {}));
    }
}

template <class Backend>
void writeHashToNumber(Backend& backend, h256 const& blockHash, protocol::BlockNumber blockNumber)
{
    storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(blockNumber));
    task::syncWait(storage2::writeOne(backend,
        bcos::executor_v1::StateKey{
            ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
}

template <class Backend>
void writeNumberToHash(Backend& backend, protocol::BlockNumber blockNumber, h256 const& blockHash)
{
    storage::Entry entry;
    entry.set(blockHash.asBytes());
    task::syncWait(storage2::writeOne(backend,
        bcos::executor_v1::StateKey{
            ledger::SYS_NUMBER_2_HASH, boost::lexical_cast<std::string>(blockNumber)},
        std::move(entry)));
}

/// Distinct hashes at one height overwrite NUMBER_2_HASH and fail the
/// fail-closed canonical check. When the three forkchoice hashes differ,
/// collapse to finalized < safe < head.
inline void uniquifyForkchoiceHeights(ForkchoiceState const& forkchoiceState,
    protocol::BlockNumber& headBlockNumber, protocol::BlockNumber& safeBlockNumber,
    protocol::BlockNumber& finalizedBlockNumber)
{
    if (forkchoiceState.headBlockHash != forkchoiceState.safeBlockHash &&
        headBlockNumber == safeBlockNumber)
    {
        BOOST_REQUIRE_GE(headBlockNumber, 1);
        safeBlockNumber = headBlockNumber - 1;
    }
    if (forkchoiceState.headBlockHash != forkchoiceState.finalizedBlockHash &&
        (headBlockNumber == finalizedBlockNumber || safeBlockNumber == finalizedBlockNumber))
    {
        BOOST_REQUIRE_GE(std::min(safeBlockNumber, headBlockNumber), 1);
        finalizedBlockNumber = std::min(safeBlockNumber, headBlockNumber) - 1;
    }
}

template <class Storage>
    requires requires(Storage& s) { s.backendStorage; }
void setForkchoiceBlockNumbers(Storage& storage, ForkchoiceState const& forkchoiceState,
    protocol::BlockNumber headBlockNumber, protocol::BlockNumber safeBlockNumber,
    protocol::BlockNumber finalizedBlockNumber)
{
    uniquifyForkchoiceHeights(
        forkchoiceState, headBlockNumber, safeBlockNumber, finalizedBlockNumber);
    auto& backend = storage.backendStorage;
    writeHashToNumber(backend, forkchoiceState.headBlockHash, headBlockNumber);
    writeNumberToHash(backend, headBlockNumber, forkchoiceState.headBlockHash);
    writeHashToNumber(backend, forkchoiceState.safeBlockHash, safeBlockNumber);
    writeNumberToHash(backend, safeBlockNumber, forkchoiceState.safeBlockHash);
    writeHashToNumber(backend, forkchoiceState.finalizedBlockHash, finalizedBlockNumber);
    writeNumberToHash(backend, finalizedBlockNumber, forkchoiceState.finalizedBlockHash);
}

/// GateMergeStorage (and similar) have no fixture ctor: write executor config
/// plus the default 5/4/3 forkchoice rows in one call.
template <class Storage>
    requires requires(Storage& s) { s.backendStorage; }
void seedForkchoiceStorage(
    Storage& storage, ForkchoiceState const& forkchoice, evmc_revision rev = EVMC_CANCUN)
{
    writeEthExecutorConfig(storage.backendStorage, rev);
    setForkchoiceBlockNumbers(storage, forkchoice, protocol::BlockNumber{5},
        protocol::BlockNumber{5}, protocol::BlockNumber{5});
}

struct RealGlobalStateStorageFixture
{
    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage storage{checkpointBackend};

    explicit RealGlobalStateStorageFixture(
        evmc_revision rev = EVMC_CANCUN, bool writeEvmcRevision = true)
    {
        writeEthExecutorConfig(backendStorage, rev, writeEvmcRevision);
    }

    void setBlockNumber(h256 const& blockHash, protocol::BlockNumber blockNumber)
    {
        writeHashToNumber(backendStorage, blockHash, blockNumber);
    }

    void setCanonicalBlock(h256 const& blockHash, protocol::BlockNumber blockNumber)
    {
        writeHashToNumber(backendStorage, blockHash, blockNumber);
        writeNumberToHash(backendStorage, blockNumber, blockHash);
    }

    void setNonce(std::string_view sender, std::string nonce)
    {
        evmc_address addr{};
        std::copy_n(sender.begin(), std::min(sender.size(), sizeof(addr.bytes)), addr.bytes);
        ledger::account::EVMAccount account{backendStorage, addr, false};
        task::syncWait(account.setNonce(std::move(nonce)));
    }
};

}  // namespace bcos::engine::eth_test
