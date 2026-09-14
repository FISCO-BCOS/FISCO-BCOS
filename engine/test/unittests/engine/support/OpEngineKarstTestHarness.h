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
 * @file OpEngineKarstTestHarness.h
 * @brief Shared OP Engine FCU/getPayload fixtures extracted from OpEngineServiceParityTest.
 */
#pragma once

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/OpEngineService.inl"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>
#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/tests/OpSchedulerSeamTestHelpers.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace op_engine_parity_test
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
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

/// Production-shaped CACHE layer: the OP composition is
/// MultiLayerStorage<GlobalStateMutableStorage, GlobalStateCacheStorage, CheckpointRocksDB…>
/// (libinitializer/GlobalStateStorageInitializer.h), i.e. cache-enabled. The default
/// test MLS above is intentionally cache-LESS; use CacheMLS for cases that must prove
/// behaviour under the production read order (cache first, then backend) — e.g. the
/// canonicalize rollback (review F3).
using CacheMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::CONCURRENT | memory_storage::LRU)>;
using CacheMLS =
    bcos::storage2::MultiLayerStorage<MutableStorage, CacheMemStorage, CheckpointBackend>;

struct StubMemPool
{
    std::vector<bcos::crypto::HashType> removed;
    std::vector<bcos::protocol::Transaction::Ptr> pool;
    void removeByHash(std::span<bcos::crypto::HashType const> hashes)
    {
        removed.insert(removed.end(), hashes.begin(), hashes.end());
    }
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t limit, View&, OutputIt out)
    {
        auto const n = std::min<int64_t>(limit, static_cast<int64_t>(pool.size()));
        for (int64_t i = 0; i < n; ++i)
        {
            *out++ = pool[static_cast<std::size_t>(i)];
        }
    }
};

/// TEST DOUBLE — NOT the real import/commitment path. executeBlock/importExecute return
/// a header with FABRICATED roots (stateRoot/receiptsRoot zero, txsRoot from a knob), so
/// the engine's commitment gate here runs only against invented values. First
/// executeBlock fails with a structured culprit; later calls succeed so the build retry
/// loop can finish (BH capacity / BC evict). RESOLUTION (review F9, contract-only by
/// decision): this stub is kept for API-gate/fault-injection coverage (e.g.
/// OpEngineServiceParityTest/op_newpayload_rejects_executed_withdrawals_root_mismatch,
/// op_build_capacity_reject_does_not_evict, op_commit_error_routing_*); real
/// import/commitment-path coverage lives in OpEngineImportFcuTest against the real
/// OpScheduler delegate — ChainedImportMatchesCanonicalParentState,
/// BadStateRootIsInvalidAndNotStored, CanonicalImportedBlockHasNumberToTxsRow,
/// ThreeImportsThenJumpFcu — plus OpNewPayloadRpcE2eSuite/
/// RegolithPayloadBuildsAndImportsAgainstRealScheduler. Do not cite a suite built on
/// this stub as import-path coverage.
struct FabricatedRootsStub : bcos::scheduler::SchedulerInterface
{
    bcos::h256 culprit;
    bool rejectAsCapacity = false;
    bool failFirst = true;
    bool failCommit = false;
    // Error code for the stub commit failure. -1 is the generic stub; the mapDelegateError
    // routing test sets real SchedulerError codes (UnknownError = the dropped-pending shape
    // a concurrent reset produces in OpScheduler; OpConsensusRejected = the one code the
    // service is allowed to answer INVALID for).
    int commitErrorCode = -1;
    /// Fail this many commitBlock calls, then succeed (the fall-through pin: a retry
    /// would succeed, so a case for "must not retry" can tell the two apart). failCommit
    /// fails every call.
    int failCommitRemaining = 0;
    /// Stamp withdrawalsRoot onto execute/import-produced headers. True models this
    /// node's scheduler, which always stamps it from Canyon on; false models the
    /// node-internal fault the newPayload guard must answer -32603 for.
    bool stampWithdrawalsRoot = true;
    /// Attach OpPendingDropped to a commit failure, modelling OpScheduler's dropped-pending
    /// exit. Independent of commitErrorCode: the tag, not the code, is what the engine's
    /// fall-through must key on, so a case can pair the tag with UnknownError.
    bool tagPendingDropped = false;
    bool failReset = false;
    int executeCalls = 0;
    int commitCalls = 0;
    /// TxsRoot stamped onto importExecute-produced headers (import payloads with no
    /// transactions carry the empty-list root; default zero matches legacy stubs).
    bcos::h256 txsRootToReturn{};
    /// RequestsHash stamped onto execute/import-produced headers when set. nullopt (the
    /// default) leaves it absent, matching the pre-Isthmus shape; a test that drives the
    /// import commitment gate at Isthmus sets it to engine_common::c_emptyRequestsHash so
    /// the fabricated header matches the rebuilt one.
    std::optional<bcos::h256> requestsHashToReturn{};
    bcos::h256 executedWithdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    bcos::protocol::BlockHeaderFactory::Ptr headerFactory;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        if (failFirst && executeCalls == 1)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        if (stampWithdrawalsRoot)
        {
            header->setWithdrawalsRoot(executedWithdrawalsRoot);
        }
        header->setBlobGasUsed(0);
        if (requestsHashToReturn)
        {
            header->setRequestsHash(*requestsHashToReturn);
        }
        callback(nullptr, std::move(header), false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        ++commitCalls;
        if (failCommit || failCommitRemaining > 0)
        {
            if (failCommitRemaining > 0)
            {
                --failCommitRemaining;
            }
            auto error = BCOS_ERROR_PTR(commitErrorCode, "stub commit failure");
            if (tagPendingDropped)
            {
                *error << bcos::engine::OpPendingDropped{true};
            }
            callback(std::move(error), nullptr);
            return;
        }
        callback(nullptr, nullptr);
    }
    /// S5 import arm: FABRICATED header (no execution happens here — see the struct's
    /// doc). The engine's commitment gate runs on whatever this returns, which is why
    /// this stub is NOT import-path coverage. No commit happens on the import path by
    /// design.
    void importExecute(bcos::protocol::Block::Ptr,
        std::vector<bcos::protocol::BlockHeader::Ptr> const&,
        std::shared_ptr<void> const& parentFlat,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr,
            std::shared_ptr<void>, std::shared_ptr<void>)>
            callback) override
    {
        ++executeCalls;
        if (failFirst && executeCalls == 1)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            callback(std::move(error), nullptr, nullptr, nullptr);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setTxsRoot(txsRootToReturn);
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        if (stampWithdrawalsRoot)
        {
            header->setWithdrawalsRoot(executedWithdrawalsRoot);
        }
        header->setBlobGasUsed(0);
        if (requestsHashToReturn)
        {
            header->setRequestsHash(*requestsHashToReturn);
        }
        callback(nullptr, std::move(header), nullptr, nullptr);
    }
    void status(std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)>) override
    {}
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)>) override
    {}
    void reset(std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(failReset ? BCOS_ERROR_PTR(-1, "stub reset failure") : nullptr);
    }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)>) override {}
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)>) override
    {}
};

/// Rejects `culprit` whenever that hash is in the block; if only `successor` remains,
/// rejects it as a non-capacity nonce-gap (the R3-F1 production shape).
/// `culprit`/`successor` are pool hashes (OpCulpritTxHash). `*EnvHash` are
/// keccak(reassembled envelope), matching buildOpBlock's transactionHash.
struct NonceChainScheduler : FabricatedRootsStub
{
    bcos::h256 successor;
    bcos::h256 culpritEnvHash;
    bcos::h256 successorEnvHash;

    void executeBlock(bcos::protocol::Block::Ptr block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        bool hasCulprit = false;
        bool hasSuccessor = false;
        if (block)
        {
            for (auto txView : block->transactions())
            {
                auto tx = std::move(txView).toShared();
                if (!tx)
                {
                    continue;
                }
                auto const hash = tx->hash();
                if (hash == culpritEnvHash)
                {
                    hasCulprit = true;
                }
                if (hash == successorEnvHash)
                {
                    hasSuccessor = true;
                }
            }
        }
        if (hasCulprit)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        if (hasSuccessor)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: nonce gap");
            *error << bcos::engine::OpCulpritTxHash(successor);
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(executedWithdrawalsRoot);
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), false);
    }
};

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

/// EIP-1559 envelope that opEnvelopeToTars can decode, plus the signing payload +
/// 65-byte signature that reassembleWeb3RawTransaction expects on the seal path.
struct DecodableWeb3Tx
{
    bcos::protocol::Transaction::Ptr tx;
    std::string rawHex;
};

inline bcos::h256 envelopeHashOf(bcos::protocol::Transaction::Ptr const& tx)
{
    bcos::crypto::Keccak256 hasher;
    auto const raw = bcostars::protocol::reassembleWeb3RawTransaction(
        tx->extraTransactionBytes(), tx->signatureData());
    return hasher.hash(bcos::ref(raw));
}

inline DecodableWeb3Tx makeDecodableWeb3Tx(
    uint64_t nonce, bcos::crypto::KeyPairInterface* keyPair = nullptr, bcos::bytes data = {})
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = nonce;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 1;
    w3.gasLimit = 21000;
    w3.to = bcos::Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd");
    w3.value = 0;
    w3.data = std::move(data);
    bcos::crypto::Secp256k1Crypto secp;
    auto owned = keyPair == nullptr ? secp.generateKeyPair() : nullptr;
    auto const& kp = keyPair != nullptr ? *keyPair : *owned;
    auto const sig = secp.sign(kp, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];

    auto const raw = w3.encode();
    auto const signPayload = w3.encodeForSign();
    {
        bcos::rpc::Web3Transaction decoded;
        bcos::bytes copy = raw;
        bcos::bytesRef ref{copy.data(), copy.size()};
        auto err = bcos::codec::rlp::decode(ref, decoded);
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE(ref.empty());
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(
            raw, bcos::h256{}, /*allowDeposit=*/true));
    }
    bcos::bytes signature(65, 0);
    std::copy(w3.signatureR.begin(), w3.signatureR.end(), signature.begin());
    std::copy(w3.signatureS.begin(), w3.signatureS.end(), signature.begin() + 32);
    signature[64] = static_cast<bcos::byte>(w3.signatureV);
    bcos::bytes reassembled;
    {
        reassembled = bcostars::protocol::reassembleWeb3RawTransaction(
            bcos::bytesConstRef(signPayload.data(), signPayload.size()),
            bcos::bytesConstRef(signature.data(), signature.size()));
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(
            reassembled, bcos::h256{}, /*allowDeposit=*/true));
    }

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(signPayload.begin(), signPayload.end());
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(bcos::fromHex(w3.sender()));
    bcos::crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<int64_t>(nonce));
    // The build loop's culprit matching rests on this identity: the producer tags the
    // culprit with keccak256(signed envelope) (OpBlockExecute) and the consumer matches it
    // against the sealed carrier's hash() (OpEngineService.inl) — they must be the same
    // bytes, or every reject (capacity or not) falls through to -32603.
    BOOST_REQUIRE_EQUAL(
        bcos::crypto::keccak256Hash(bcos::ref(reassembled)).hex(), tx->hash().hex());
    return DecodableWeb3Tx{.tx = std::move(tx), .rawHex = bcos::toHexStringWithPrefix(raw)};
}

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };
    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

using EngineOpSchedulerBase = bcos::evm::engine::OpSchedulerSeam<ViewType>;
/// Production seam synthesizes from L1BlockInfo. Fixtures keep the zero envelope.
struct EngineOpScheduler : EngineOpSchedulerBase
{
    using EngineOpSchedulerBase::EngineOpSchedulerBase;
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(uint64_t timestampSeconds) const
    {
        return bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(
            configAt(timestampSeconds).has_da_footprint);
    }
};
using EthLegacyEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, EngineOpScheduler>;

static_assert(bcos::engine::EngineServiceConcept<EthLegacyEngine>);
static_assert(bcos::engine::EngineServiceConcept<OpEngine>);

constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_finalizedOrderingBlockNumber = 42;

constexpr char const* c_opNewPayloadVersionMismatchMessage =
    "newPayload version does not match the OP Engine API profile at payload timestamp";
constexpr char const* c_safeAboveHeadMessage =
    "Forkchoice safe block number must not exceed head block number";
constexpr char const* c_finalizedAboveHeadMessage =
    "Forkchoice finalized block number must not exceed head block number";
constexpr char const* c_finalizedAboveSafeMessage =
    "Forkchoice finalized block number must not exceed safe block number";

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

template <class StorageType>
inline void registerVerifiedBlock(
    StorageType& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
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

template <class StorageType>
inline void registerHashToNumberOnly(
    StorageType& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

template <class StorageType>
inline void registerCurrentBlockNumber(StorageType& multiLayerStorage, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

/// Materialize the canonical by-number transaction list row for a height with zero
/// transactions (genesis). The merged ledger read is fail-closed: a HEADER|TRANSACTIONS
/// by-number read throws GetStorageError("missing SYS_NUMBER_2_TXS row") when the row is
/// absent, whereas the pre-merge reader returned an empty block. The sequence-matrix
/// invariant I2 reads the committed genesis at height 0, so the fixture must carry the row.
template <class StorageType>
inline void registerEmptyCanonicalTxRow(
    StorageType& multiLayerStorage, bcos::protocol::BlockFactory& blockFactory, int64_t number)
{
    auto block = blockFactory.createBlock();
    bcos::bytes encoded;
    block->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, std::to_string(number)}, std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

template <class StorageType>
inline void registerParentHeader(StorageType& multiLayerStorage,
    bcos::protocol::BlockFactory& blockFactory, int64_t number, int64_t timestampMs)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(timestampMs);
    header->setGasLimit(30'000'000);
    header->setGasUsed(0);
    header->setExtraData(bcos::fromHex("00000000fa00000006"));
    header->setBaseFee(bcos::u256(1'000'000'000));
    header->setBlobGasUsed(0);
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(number)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

inline bcos::engine::NewPayloadRequest makeValidIsthmusNewPayload(
    bcos::protocol::BlockFactory& blockFactory, bcos::h256 const& parentHash,
    bcos::protocol::BlockNumber blockNumber)
{
    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.blockNumber = blockNumber;
    payload.timestamp = 1'700'000'000'000ULL;
    payload.gasLimit = 30'000'000;
    payload.gasUsed = 0;
    payload.baseFeePerGas = 1;
    payload.transactions = {};
    payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    payload.excessBlobGas = bcos::u256(0);
    payload.blobGasUsed = bcos::u256(0);
    payload.extraData = bcos::fromHex("00000000fa00000006");
    request.parentBeaconBlockRoot = bcos::h256{};
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
    auto header =
        bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory.blockHeaderFactory(),
            payload, txRoot, *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    return request;
}

/// Per-fork newPayload request: same skeleton as makeValidIsthmusNewPayload, but the
/// fork-dependent members follow the fork's shape instead of Isthmus's. extraDataLen is
/// 0 pre-Holocene, 9 (00000000fa00000006) for Holocene/Isthmus, 17 for Jovian+ — the
/// layout is pinned by extraDataLayoutFor (OpForkId.h:113-144).
inline bcos::engine::NewPayloadRequest makeNewPayloadAt(bcos::protocol::BlockFactory& blockFactory,
    bcos::h256 const& parentHash, bcos::protocol::BlockNumber blockNumber,
    bcos::engine::OpForkId forkId, bool hasWithdrawals, bool hasBlobFields,
    std::size_t extraDataLen, std::uint64_t timestampMs)
{
    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};  // present-but-empty wire contract
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.blockNumber = blockNumber;
    payload.timestamp = timestampMs;
    payload.gasLimit = 30'000'000;
    payload.gasUsed = 0;
    payload.baseFeePerGas = 1;
    payload.transactions = {};
    if (hasWithdrawals)
    {
        payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
        payload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    }
    if (hasBlobFields)
    {
        payload.excessBlobGas = bcos::u256(0);
        payload.blobGasUsed = bcos::u256(0);
    }
    payload.extraData = bcos::bytes(extraDataLen, 0);
    if (extraDataLen >= 9)
    {
        payload.extraData =
            bcos::fromHex("00000000fa00000006" + std::string((extraDataLen - 9) * 2, '0'));
    }
    request.parentBeaconBlockRoot = bcos::h256{};
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        blockFactory.blockHeaderFactory(), payload, txRoot, *request.parentBeaconBlockRoot, forkId);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    return request;
}

/// Field-wise pin of the strict-compared ExecutionPayload set (the 16 fields
/// compareWithBuiltPayload enforces plus the tx list) between two struct copies —
/// used by the wire round trip and the version-window tests so a dropped field
/// cannot pass silently.
inline void checkSameExecutionPayload(
    bcos::engine::ExecutionPayload const& left, bcos::engine::ExecutionPayload const& right)
{
    BOOST_CHECK_EQUAL(left.parentHash.hex(), right.parentHash.hex());
    BOOST_CHECK_EQUAL(left.feeRecipient.hex(), right.feeRecipient.hex());
    BOOST_CHECK_EQUAL(left.stateRoot.hex(), right.stateRoot.hex());
    BOOST_CHECK_EQUAL(left.receiptsRoot.hex(), right.receiptsRoot.hex());
    BOOST_CHECK_EQUAL(bcos::toHex(bcos::bytes(left.logsBloom.begin(), left.logsBloom.end())),
        bcos::toHex(bcos::bytes(right.logsBloom.begin(), right.logsBloom.end())));
    BOOST_CHECK_EQUAL(left.prevRandao.hex(), right.prevRandao.hex());
    BOOST_CHECK(left.blockNumber == right.blockNumber);
    BOOST_CHECK(left.gasLimit == right.gasLimit);
    BOOST_CHECK(left.gasUsed == right.gasUsed);
    BOOST_CHECK(left.timestamp == right.timestamp);
    BOOST_CHECK_EQUAL(bcos::toHex(left.extraData), bcos::toHex(right.extraData));
    BOOST_CHECK(left.baseFeePerGas == right.baseFeePerGas);
    BOOST_CHECK_EQUAL(left.blockHash.hex(), right.blockHash.hex());
    BOOST_REQUIRE_EQUAL(left.withdrawals.has_value(), right.withdrawals.has_value());
    BOOST_REQUIRE_EQUAL(left.withdrawalsRoot.has_value(), right.withdrawalsRoot.has_value());
    if (left.withdrawalsRoot.has_value())
        BOOST_CHECK_EQUAL(left.withdrawalsRoot->hex(), right.withdrawalsRoot->hex());
    BOOST_REQUIRE_EQUAL(left.blobGasUsed.has_value(), right.blobGasUsed.has_value());
    if (left.blobGasUsed.has_value())
        BOOST_CHECK(*left.blobGasUsed == *right.blobGasUsed);
    BOOST_REQUIRE_EQUAL(left.excessBlobGas.has_value(), right.excessBlobGas.has_value());
    if (left.excessBlobGas.has_value())
        BOOST_CHECK(*left.excessBlobGas == *right.excessBlobGas);
    BOOST_REQUIRE_EQUAL(left.transactions.size(), right.transactions.size());
    for (std::size_t i = 0; i < left.transactions.size(); ++i)
        BOOST_CHECK_EQUAL(
            bcos::toHex(left.transactions[i].raw), bcos::toHex(right.transactions[i].raw));
}

template <typename Exception>
void checkBothExceptionMessages(auto&& leftAction, auto&& rightAction, char const* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(leftAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
    BOOST_CHECK_EXCEPTION(rightAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

inline bcos::engine::PayloadAttributes makeOpPayloadAttributes()
{
    bcos::engine::PayloadAttributes attrs;
    // Whole-second milliseconds for Eth RLP timestamp validation.
    attrs.timestamp = 1'700'000'000'000ULL;
    attrs.prevRandao = bcos::h256(std::string(64, '2'));
    attrs.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes(8, 0);
    attrs.minBaseFee = 0;
    attrs.noTxPool = true;
    return attrs;
}

struct OpServicePair
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler scheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                    bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    bcos::scheduler::SchedulerInterface::Ptr delegate;
    OpEngine service;

    explicit OpServicePair(bool allowSynthesizedL1Attributes = false,
        bcos::scheduler::SchedulerInterface::Ptr delegateIn = nullptr,
        std::shared_ptr<bcos::engine::DACaps> daCapsIn = nullptr,
        std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> scheduleIn = nullptr)
      : scheduler(scheduleIn ? std::move(scheduleIn) :
                               std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                   bcos::evm::opstack::OpForkSchedule::legacy(false)),
            {}),
        delegate(std::move(delegateIn)),
        service(memPool, storage, scheduler, blockFactory, bcos::engine::c_defaultBlockTxCountLimit,
            delegate, std::move(daCapsIn), allowSynthesizedL1Attributes)
    {}
};

/// Production parse: Jovian at 0s, Karst at 1000s.
inline std::shared_ptr<bcos::evm::opstack::OpForkSchedule> makeKarstProfileSchedule()
{
    using bcos::evm::opstack::OpForkSchedule;
    return std::make_shared<OpForkSchedule>(OpForkSchedule::parse("0:jovian,1000:karst"));
}

/// The 9-rung ladder schedule: every modeled EL fork on a 1000s grid. The canonical
/// text follows OpForkScheduleCodec's rules (timestamp-0 baseline + strictly
/// contiguous fork order), so it parses through the same production path the ledger
/// uses. Ladder rungs index activations below.
inline constexpr std::string_view c_forkLadderCanonical =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,5000:holocene,"
    "6000:isthmus,7000:jovian,8000:karst";

inline std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> makeForkLadderSchedule()
{
    return std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
        bcos::evm::opstack::OpForkSchedule::parse(c_forkLadderCanonical));
}

/// PayloadAttributes.timestamp is internal milliseconds (unix seconds × 1000).
inline constexpr std::uint64_t c_jovianPayloadTimestampMs = 999'000;
inline constexpr std::uint64_t c_karstPayloadTimestampMs = 1'000'000;
static_assert(c_jovianPayloadTimestampMs / 1000 == 999);
static_assert(c_karstPayloadTimestampMs / 1000 == 1000);

inline bcos::h256 fixtureHeadHash()
{
    return bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

inline bcos::engine::PayloadAttributes makeOpPayloadAttributesAt(std::uint64_t timestampMs)
{
    auto attrs = makeOpPayloadAttributes();
    attrs.timestamp = timestampMs;
    return attrs;
}

/// Pre-Holocene attrs. makeOpPayloadAttributes() carries Holocene+ fields (beacon
/// root, 8-byte eip1559Params, minBaseFee); leaving them set makes a Regolith V1 or
/// Canyon V2 FCU come back Invalid without throwing, so a NO_THROW assertion would
/// pass for the wrong reason.
inline bcos::engine::PayloadAttributes makeRegolithAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeOpPayloadAttributesAt(timestampMs);
    attrs.withdrawals.reset();
    attrs.parentBeaconBlockRoot.reset();
    attrs.eip1559Params.reset();
    attrs.minBaseFee.reset();
    return attrs;
}

inline bcos::engine::PayloadAttributes makeCanyonAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeRegolithAttrs(timestampMs);
    attrs.withdrawals.emplace();
    return attrs;
}

inline bcos::engine::PayloadAttributes makeEcotoneAttrs(std::uint64_t timestampMs)
{
    auto attrs = makeCanyonAttrs(timestampMs);
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    return attrs;
}

/// FCU V3 build keyed by attrs.timestamp (ms). Parent/head timestamp is seeded separately.
/// Forced txs are deposits-only so a Jovian/Karst activation window (parent pre-fork,
/// attrs on the new fork) stays VALID — user envelopes are FCU-INVALID there.
inline bcos::engine::PayloadID buildPayloadAt(
    OpServicePair& pair, std::uint64_t attrsTimestampMs, std::uint64_t parentTimestampMs)
{
    // Real L1-attributes deposit (not 0x7e00): buildOpBlock rejects undecodable
    // envelopes as FCU INVALID. Jovian+ schedule always has DA footprint.
    auto const deposit = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(true);
    auto attrs = makeOpPayloadAttributesAt(attrsTimestampMs);
    attrs.transactions = std::vector<std::string>{bcos::toHexStringWithPrefix(deposit)};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(
        pair.storage, *pair.blockFactory, 0, static_cast<int64_t>(parentTimestampMs));
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());
    return *built.payloadId;
}

struct KarstProfilePair
{
    std::shared_ptr<FabricatedRootsStub> delegate{std::make_shared<FabricatedRootsStub>()};
    OpServicePair pair;

    KarstProfilePair() : pair(false, delegate, nullptr, makeKarstProfileSchedule())
    {
        delegate->failFirst = false;
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
    }
};

// ---------------------------------------------------------------------------
// Import/FCU fixture, moved here out of OpEngineImportFcuTest.cpp (P1 Task 7a)
// so the sequence matrix can drive the same service composition. It stays in
// this namespace because the moved code was written against these names
// unqualified (MLS, makeCryptoSuite, fixtureHeadHash, ...) and every user does
// `using namespace op_engine_parity_test;`.
// Free functions are `inline` and namespace-scope constants `inline constexpr`
// on purpose: this header is included by many TUs and two of them (this fixture
// plus the sequence matrix) are compiled into the same unity blob (review F-9).
// ---------------------------------------------------------------------------

using namespace evmc::literals;

inline constexpr auto kDepositFrom = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
inline constexpr auto kL1Block = 0x4200000000000000000000000000000000000015_address;
inline const bcos::Address kImportEip1559Sender{"0x1000000000000000000000000000000000000001"};

inline bcos::protocol::TransactionReceiptFactory::Ptr makeImportReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

inline bcos::evm::opstack::DepositTx makeImportDeposit(std::string_view label)
{
    bcos::crypto::Keccak256 hasher;
    bcos::evm::opstack::DepositTx dep;
    auto digest = hasher.hash(
        bcos::bytesConstRef{reinterpret_cast<const bcos::byte*>(label.data()), label.size()});
    std::memcpy(dep.source_hash.bytes, digest.data(), sizeof(dep.source_hash.bytes));
    dep.from = kDepositFrom;
    dep.to = kL1Block;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

/// extraTransactionBytes = full envelope (the only bytes depositFromTransaction
/// reads); sender forced to the deposit's from (OpSchedulerTest precedent).
inline bcos::protocol::Transaction::Ptr buildImportDepositTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const txHash = hashImpl->hash(env);
    bcostars::Transaction tars;
    tars.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tars.extraTransactionHash.assign(txHash.begin(), txHash.end());
    tars.extraTransactionBytes.assign(env.begin(), env.end());
    tars.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef{env.data(), env.size()});
    tx->forceSender(bcos::bytes(dep.from.bytes, dep.from.bytes + sizeof(dep.from.bytes)));
    return tx;
}

inline std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeImportHeader(
    bcos::protocol::BlockNumber number, bcos::h256 parentHash, uint64_t timestampSeconds)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(static_cast<int64_t>(timestampSeconds * 1000));  // internal ms
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = parentHash});
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30'000'000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Seed the committed plane as an existing genesis: current number 0 plus a funded
/// EOA so execution has a world to run against. This is the `latest` the imports
/// must NOT move.
template <class StorageType>
void seedCommittedGenesis(StorageType& mls, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, kImportEip1559Sender, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::storage::Entry entry;
    entry.set("0");
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

template <class StorageType>
int64_t committedTipNumber(StorageType& mls)
{
    auto view = mls.forkCommitted();
    return bcos::task::syncWait(
        bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
}

template <class StorageType>
std::optional<bcos::h256> committedHashAt(StorageType& mls, int64_t number)
{
    auto view = mls.forkCommitted();
    return bcos::task::syncWait(
        bcos::ledger::getBlockHash(view, number, bcos::ledger::fromStorage));
}

/// Build the fixture's storage in place: MLS takes only the checkpoint, CacheMLS also
/// binds the cache layer (production read order). Returning by value is impossible
/// (MultiLayerStorage holds non-movable mutexes), hence the unique_ptr indirection.
template <class StorageType>
std::unique_ptr<StorageType> makeTestStorage(CheckpointBackend& checkpoint, CacheMemStorage& cache)
{
    if constexpr (std::is_same_v<StorageType, MLS>)
    {
        return std::make_unique<StorageType>(checkpoint);
    }
    else
    {
        return std::make_unique<StorageType>(checkpoint, cache);
    }
}

/// Signed EIP-1559 envelope calling @p to with empty data (sender fixed to
/// kImportEip1559Sender by forceSender + mirror fields, OpSchedulerTest precedent).


struct ImportSchedulerFixture
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler;

    ImportSchedulerFixture()
    {
        // Execute-only: ledger = nullptr (importExecute must never reach
        // prewriteBlockToBuffer, which is the only ledger-dependent path).
        scheduler = std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(
            makeImportReceiptFactory(), hashImpl, /*chainId=*/8453,
            std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                bcos::evm::opstack::OpForkSchedule::legacy(false)),
            blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
        seedCommittedGenesis(storage, hashImpl);
    }

    bcos::protocol::Block::Ptr depositBlock(bcos::protocol::BlockNumber number,
        bcos::h256 const& parentHash, uint64_t timestampSeconds, std::string_view label)
    {
        auto env = bcos::evm::opstack::encodeDepositEnvelope(makeImportDeposit(label));
        auto tx = buildImportDepositTx(env, hashImpl);
        auto block = blockFactory->createBlock();
        block->setBlockHeader(makeImportHeader(number, parentHash, timestampSeconds));
        block->appendTransaction(std::move(tx));
        return block;
    }
};

/// Real OpScheduler importExecute with the returned storage delta stripped for one block
/// number. Injects a mid-chain canonicalize failure: earlier blocks merge into the backend,
/// then the target block cannot, so the engine's canonicalize rollback must restore the
/// backend observably (review F3).
template <class StorageType>
struct DeltaStrippingScheduler : bcos::executor_v1::opstack::OpScheduler<StorageType>
{
    using Base = bcos::executor_v1::opstack::OpScheduler<StorageType>;
    using Base::Base;
    bcos::protocol::BlockNumber stripDeltaAt{-1};
    std::map<bcos::protocol::BlockNumber, int> seen;

    void importExecute(bcos::protocol::Block::Ptr block,
        std::vector<bcos::protocol::BlockHeader::Ptr> const& parentHeaders,
        std::shared_ptr<void> const& parentFlat,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr,
            std::shared_ptr<void>, std::shared_ptr<void>)>
            callback) override
    {
        auto const number = block ? block->blockHeader()->number() : -1;
        // The commitment probe imports each height once before the real newPayload; strip
        // only the later (real) import so the probe still gets a usable delta.
        bool const strip = number == stripDeltaAt && seen[number]++ == 1;
        Base::importExecute(block, parentHeaders, parentFlat,
            [strip, cb = std::move(callback)](bcos::Error::Ptr error,
                bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void> delta,
                std::shared_ptr<void> flat) mutable {
                if (strip)
                {
                    delta = nullptr;
                }
                cb(std::move(error), std::move(header), std::move(delta), std::move(flat));
            });
    }
};

template <class StorageType>
inline std::shared_ptr<bcos::scheduler::SchedulerInterface> makeImportDelegate(
    bcos::protocol::BlockNumber stripDeltaAt, bcos::protocol::BlockFactory::Ptr const& blockFactory,
    StorageType& storage, bcos::IOServicePool::Ptr const& ioServicePool)
{
    auto schedule = std::make_shared<bcos::evm::opstack::OpForkSchedule>(
        bcos::evm::opstack::OpForkSchedule::legacy(false));
    if (stripDeltaAt >= 0)
    {
        auto scheduler = std::make_shared<DeltaStrippingScheduler<StorageType>>(
            makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
            std::move(schedule), blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
        scheduler->stripDeltaAt = stripDeltaAt;
        return scheduler;
    }
    return std::make_shared<bcos::executor_v1::opstack::OpScheduler<StorageType>>(
        makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
        std::move(schedule), blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
}

/// Two latches let a test park canonicalizeImportedHead inside an awaited section, so
/// a concurrent import can be attempted while canonicalize is in flight (review F5).
struct BlockingGate
{
    std::latch entered{1};
    std::latch release{1};
};

/// Real OpScheduler whose canonicalize post-condition blocks on @p gate — the parked
/// await a POSIX lock must NOT be held across (review F5).
template <class StorageType>
struct BlockingVerifyScheduler : bcos::executor_v1::opstack::OpScheduler<StorageType>
{
    using Base = bcos::executor_v1::opstack::OpScheduler<StorageType>;
    using Base::Base;
    std::shared_ptr<BlockingGate> gate;

    void verifyCanonicalStateRoot(const bcos::h256& expectedStateRoot) override
    {
        if (gate)
        {
            gate->entered.count_down();
            gate->release.wait();
        }
        Base::verifyCanonicalStateRoot(expectedStateRoot);
    }
};

/// Real OpScheduler whose canonicalize post-condition can be made to fail on demand:
/// the switch-SetCanonical mid-flight failure injection (review NEW-3). By then the
/// batch has already been merged, so the rollback must restore BOTH layers — backend
/// AND cache — to the pre-call plane.
template <class StorageType>
struct FailVerifyScheduler : bcos::executor_v1::opstack::OpScheduler<StorageType>
{
    using Base = bcos::executor_v1::opstack::OpScheduler<StorageType>;
    using Base::Base;
    bool failVerify = false;

    void verifyCanonicalStateRoot(const bcos::h256& expectedStateRoot) override
    {
        if (failVerify)
        {
            throw bcos::evm::OpConsensusError("injected post-condition failure (switch rollback)");
        }
        Base::verifyCanonicalStateRoot(expectedStateRoot);
    }
};

template <class StorageType>
inline std::shared_ptr<bcos::scheduler::SchedulerInterface> makeBlockingVerifyDelegate(
    std::shared_ptr<BlockingGate> const& gate,
    bcos::protocol::BlockFactory::Ptr const& blockFactory, StorageType& storage,
    bcos::IOServicePool::Ptr const& ioServicePool)
{
    auto scheduler = std::make_shared<BlockingVerifyScheduler<StorageType>>(
        makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
        std::make_shared<bcos::evm::opstack::OpForkSchedule>(
            bcos::evm::opstack::OpForkSchedule::legacy(false)),
        blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
    scheduler->gate = gate;
    return scheduler;
}

/// OpEngineService composed with a REAL OpScheduler delegate (imports execute for
/// real); the FCU build path is not exercised by these cases (no attrs). Templated on
/// the MLS so a cache-enabled composition (production read order) can be instantiated
/// for the atomicity cases — see CacheImportServiceFixture.
template <class StorageType>
struct ImportServiceFixtureT
{
    using ServiceT = bcos::engine::OpEngineService<StubMemPool, StorageType, EngineOpScheduler>;

    /// Seed + import + one-jump-FCU the canonical chain A(1)-B(2)-C(3).
    void seedCanonicalChainABC()
    {
        auto requestA = validRequest(fixtureHeadHash(), 1);
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(bcos::task::syncWait(service.newPayload(requestA, 4)).status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        seededChainHash[1] = requestA.executionPayload.blockHash;
        auto requestB = validRequest(requestA.executionPayload.blockHash, 2);
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(bcos::task::syncWait(service.newPayload(requestB, 4)).status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        seededChainHash[2] = requestB.executionPayload.blockHash;
        auto requestC = validRequest(requestB.executionPayload.blockHash, 3);
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(bcos::task::syncWait(service.newPayload(requestC, 4)).status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        seededChainHash[3] = requestC.executionPayload.blockHash;
        bcos::engine::ForkchoiceState fcuC{requestC.executionPayload.blockHash,
            requestC.executionPayload.blockHash, fixtureHeadHash()};
        BOOST_REQUIRE_EQUAL(
            static_cast<int>(bcos::task::syncWait(service.updateForkchoice(fcuC, nullptr, 3))
                                 .payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    }
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    CacheMemStorage cache{};
    std::unique_ptr<StorageType> storagePtr{makeTestStorage<StorageType>(checkpoint, cache)};
    StorageType& storage{*storagePtr};
    StubMemPool memPool;
    op_engine_parity_test::StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> delegate;
    EngineOpScheduler seamScheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                        bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    ServiceT service;

    /// Hashes of the canonicalized seed chain, keyed by height (set by seedCanonicalChainABC).
    std::map<int64_t, bcos::h256> seededChainHash;

    ImportSchedulerFixture imports;  // reuse the deposit-block helpers' receipt factory

    ImportServiceFixtureT() : ImportServiceFixtureT(/*stripImportDeltaAt=*/-1) {}

    explicit ImportServiceFixtureT(bcos::protocol::BlockNumber stripImportDeltaAt)
      : delegate(makeImportDelegate<StorageType>(
            stripImportDeltaAt, blockFactory, storage, ioServicePool)),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        seedGenesisAndForkchoice();
    }

    /// Custom-schedule ctor: the seam can only be swapped in the member-init list
    /// (OpSchedulerSeam deletes copy/move; OpEngineService holds SchedulerType&), so
    /// this is the one place a non-legacy schedule can enter the fixture.
    struct WithSchedule
    {
    };

    explicit ImportServiceFixtureT(
        WithSchedule, std::shared_ptr<const bcos::evm::opstack::OpForkSchedule> schedule)
      : delegate(makeImportDelegate<StorageType>(
            /*stripImportDeltaAt=*/-1, blockFactory, storage, ioServicePool)),
        seamScheduler(std::move(schedule), {}),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        seedGenesisAndForkchoice();
    }

    /// Real scheduler whose canonicalize post-condition parks on @p gate (review F5).
    explicit ImportServiceFixtureT(std::shared_ptr<BlockingGate> gate)
      : delegate(
            makeBlockingVerifyDelegate<StorageType>(gate, blockFactory, storage, ioServicePool)),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        seedGenesisAndForkchoice();
    }

    /// Delegate-factory ctor: the factory needs the fixture's storage reference, so it
    /// runs here (members are already initialized). Lets a case supply its own
    /// scheduler flavour (e.g. FailVerifyScheduler, review NEW-3).
    struct DelegateFromFactory
    {
    };
    template <class DelegateFactory>
    explicit ImportServiceFixtureT(DelegateFromFactory, DelegateFactory makeDelegate)
      : delegate(makeDelegate(blockFactory, storage, ioServicePool)),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        seedGenesisAndForkchoice();
    }

    void seedGenesisAndForkchoice()
    {
        auto const g = fixtureHeadHash();
        registerVerifiedBlock(storage, g, 0);
        registerParentHeader(storage, *blockFactory, 0, 1'699'000'000'000);
        registerEmptyCanonicalTxRow(storage, *blockFactory, 0);
        seedCommittedGenesis(storage, makeCryptoSuite()->hashImpl());
        // FCU to genesis: tracker head = G@0 (no attrs, no delegate involvement).
        bcos::engine::ForkchoiceState forkchoice{g, g, g};
        auto built = bcos::task::syncWait(service.updateForkchoice(forkchoice, nullptr, 3));
        BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    }

    /// Per-number executed headers: the parent header for block N's baseFee is
    /// block N-1's EXECUTED header (chain-accurate pricing across chained imports).
    std::map<int64_t, bcos::protocol::BlockHeader::Ptr> executedByNumber;
    std::map<int64_t, std::shared_ptr<void>> deltaByNumber;
    std::map<int64_t, std::shared_ptr<void>> flatByNumber;

    bcos::protocol::BlockHeader::Ptr parentHeaderFor(int64_t number) const
    {
        if (auto it = executedByNumber.find(number); it != executedByNumber.end())
        {
            return it->second;
        }
        // Seeded genesis parent header (number 0).
        auto parentHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
        parentHeader->setNumber(0);
        parentHeader->setTimestamp(1'699'000'000'000);
        parentHeader->setGasLimit(30'000'000);
        parentHeader->setGasUsed(0);
        parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
        parentHeader->setBaseFee(bcos::u256(1'000'000'000));
        return parentHeader;
    }

    /// A valid Isthmus V4 payload extending @p parent with a strictly increasing
    /// timestamp and the baseFee recomputed from the ACTUAL parent header.
    ///
    /// @p depositTag, when >= 0, perturbs the L1-attributes deposit's final calldata byte
    /// so that each height gets a DISTINCT transaction hash. Production deposits already
    /// differ per L2 block (the L1-info sequence number), so this only restores the
    /// production shape: without it the byte-identical zero envelope is shared by every
    /// height and the first canonicalization writes the single shared
    /// SYS_HASH_2_TX/SYS_HASH_2_RECEIPT row, masking a body row missing at a later height.
    bcos::engine::NewPayloadRequest validRequest(
        bcos::h256 parent, int64_t number, int depositTag = -1)
    {
        auto request = makeValidIsthmusNewPayload(*blockFactory, parent, number);
        // OP blocks always carry the L1 attributes deposit (a zero-tx block is a
        // consensus reject: "missing L1 attributes deposit").
        bcos::engine::EngineTransaction depositTx;
        depositTx.raw = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(
            /*has_da_footprint=*/false);
        if (depositTag >= 0)
        {
            depositTx.raw.back() = static_cast<bcos::byte>(0x40 + (depositTag & 0x3f));
        }
        request.executionPayload.transactions.push_back(std::move(depositTx));
        request.executionPayload.timestamp =
            static_cast<std::uint64_t>(1'700'000'000'000ULL + number * 12'000ULL);
        auto const parentHeader = parentHeaderFor(number - 1);
        request.executionPayload.baseFeePerGas =
            bcos::engine::calcOpBaseFee(*parentHeader, /*has_da_footprint=*/false);
        fillCommitmentsFromProbe(request);
        return request;
    }

    struct ProbeArtifacts
    {
        bcos::protocol::BlockHeader::Ptr header;
        std::shared_ptr<void> delta;
        std::shared_ptr<void> flat;
    };

    /// Import a bare block (attributes deposit only) on @p plane and return the
    /// executed header — the independent oracle the F1 regression compares against.
    ProbeArtifacts probeImport(bcos::h256 const& parentHash, bcos::protocol::BlockNumber number,
        uint64_t tsSec, std::shared_ptr<void> const& plane)
    {
        auto env = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
        auto block = blockFactory->createBlock();
        block->setBlockHeader(makeImportHeader(number, parentHash, tsSec));
        block->appendTransaction(buildImportDepositTx(env, makeCryptoSuite()->hashImpl()));
        ProbeArtifacts out;
        delegate->importExecute(block, {}, plane,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr header,
                std::shared_ptr<void> delta, std::shared_ptr<void> flat) {
                if (error)
                {
                    BOOST_FAIL(std::string("probeImport failed: ") + error->errorMessage());
                }
                out.header = std::move(header);
                out.delta = std::move(delta);
                out.flat = std::move(flat);
            });
        return out;
    }

    /// Learn the true execution commitments for @p request's CURRENT transaction
    /// set by importing the identical block once at scheduler level, then copy
    /// them into the payload and re-hash (the CL learns these from upstream
    /// execution; a hand-built payload cannot know them).
    void fillCommitmentsFromProbe(bcos::engine::NewPayloadRequest& request)
    {
        // The probe MUST run on the same plane as the real import: the DIRECT
        // parent's materialized flat (empty = parent is the canonical tip).
        std::shared_ptr<void> parentFlat;
        if (auto it = flatByNumber.find(request.executionPayload.blockNumber - 1);
            it != flatByNumber.end())
        {
            parentFlat = it->second;
        }
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
            *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);

        // Probe: importExecute the identical block once, then copy the true
        // commitments into the payload (the replay then matches by construction).
        auto probeBlock = blockFactory->createBlock();
        probeBlock->setBlockHeader(header);
        {
            // buildOpBlock equivalent: envelopes -> tars transactions.
            auto& hashImpl = *blockFactory->cryptoSuite()->hashImpl();
            for (auto const& env : bcos::engine::detail::rawEnvelopes(request.executionPayload))
            {
                auto const txHash = hashImpl.hash(env);
                auto tarsTx = bcos::engine::engine_common::op::opEnvelopeToTars(
                    env, txHash, /*allowDeposit=*/true);
                BOOST_REQUIRE(tarsTx.has_value());
                tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
                auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [tars = std::move(*tarsTx)]() mutable { return &tars; });
                probeBlock->appendTransaction(std::move(tx));
            }
        }
        BOOST_CHECK_EQUAL(
            probeBlock->transactionsSize(), request.executionPayload.transactions.size());
        std::shared_ptr<void> probeDelta;
        std::shared_ptr<void> probeFlat;
        bcos::protocol::BlockHeader::Ptr executed;
        std::vector<bcos::protocol::BlockHeader::Ptr> parentHeaders;
        for (int64_t n = 1; n < request.executionPayload.blockNumber; ++n)
        {
            if (auto it = executedByNumber.find(n); it != executedByNumber.end())
            {
                parentHeaders.push_back(it->second);
            }
        }
        delegate->importExecute(probeBlock, parentHeaders, parentFlat,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr done,
                std::shared_ptr<void> delta, std::shared_ptr<void> flat) {
                if (error)
                {
                    BOOST_FAIL(std::string("probe import failed: ") + error->errorMessage());
                }
                executed = std::move(done);
                probeDelta = std::move(delta);
                probeFlat = std::move(flat);
            });
        BOOST_REQUIRE(executed != nullptr);
        BOOST_REQUIRE(probeDelta != nullptr);
        deltaByNumber[request.executionPayload.blockNumber] = std::move(probeDelta);
        flatByNumber[request.executionPayload.blockNumber] = std::move(probeFlat);
        request.executionPayload.stateRoot = executed->stateRoot();
        request.executionPayload.receiptsRoot = executed->receiptsRoot();
        request.executionPayload.gasUsed = executed->gasUsed();
        request.executionPayload.withdrawalsRoot = executed->withdrawalsRoot();
        auto const bloom = executed->logsBloom();
        std::memcpy(request.executionPayload.logsBloom.data(), bloom.data(),
            std::min(bloom.size(), request.executionPayload.logsBloom.size()));

        auto const filledTxRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request.executionPayload));
        auto filledHeader = bcos::engine::engine_common::op::rebuildOpEthHeader(
            blockFactory->blockHeaderFactory(), request.executionPayload, filledTxRoot,
            *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        request.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*filledHeader);
        // Record the FILLED (announced-content) header, not `executed`: the parent-chain
        // seeds must carry the CL-announced hash — computeHash(executed) can drift from
        // the announced hash because finishExecute mirrors only a field subset.
        executedByNumber[request.executionPayload.blockNumber] = filledHeader;
    }

    /// B' sibling of the canonical B at height 2 (parent A): same height, later
    /// timestamp, so its hash differs and a switch to it is a same-height reorg.
    /// Used by the M4 sequence matrix (S3/S4/S13/S14) — the construction is the one
    /// SwitchDropsReplacedSameHeightSiblingLedgerRows used inline.
    bcos::engine::NewPayloadRequest makeSiblingAtHeight2()
    {
        auto request = validRequest(seededChainHash[1], 2);
        request.executionPayload.timestamp += 3'000;
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
            *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
        return request;
    }

    /// One randomized step for the M4 random sequence (S15), returning the head the
    /// invariants must then hold at. Operations stay inside the paths whose semantics
    /// are pinned by S1-S14: extend the chain (then FCU to the new block), heartbeat at
    /// the tip, or a no-attrs FCU back to an older canonical height (which must NOT move
    /// the tip — S5). Pushing safe/finalized to the tip is deliberately NOT generated:
    /// the prune behaviour that follows is the F4 item deferred to Tier-2, and a
    /// BOOST_REQUIRE inside the fixture would abort the suite rather than fail a case.
    int64_t driveRandomStep(std::mt19937_64& rng, int64_t head)
    {
        std::uniform_int_distribution<int> pick(0, 2);
        switch (pick(rng))
        {
        case 0:  // extend: import a child of the tip, then FCU to it
        {
            auto request = validRequest(seededChainHash[head], head + 1);
            auto const status = bcos::task::syncWait(service.newPayload(request, 4));
            if (status.status != bcos::engine::PayloadValidationStatus::Valid)
            {
                break;  // head unchanged; the invariants must still hold
            }
            seededChainHash[head + 1] = request.executionPayload.blockHash;
            bcos::engine::ForkchoiceState fc{request.executionPayload.blockHash,
                request.executionPayload.blockHash, fixtureHeadHash()};
            auto const fcu = bcos::task::syncWait(service.updateForkchoice(fc, nullptr, 3));
            if (fcu.payloadStatus.status == bcos::engine::PayloadValidationStatus::Valid)
            {
                ++head;
            }
            break;
        }
        case 1:  // heartbeat at the tip (also refreshes safe to the tip)
        {
            auto const tipHash = seededChainHash[head];
            bcos::engine::ForkchoiceState fc{tipHash, tipHash, fixtureHeadHash()};
            (void)bcos::task::syncWait(service.updateForkchoice(fc, nullptr, 3));
            break;
        }
        default:  // no-attrs FCU to an older canonical height: must not rewind the tip
        {
            auto const oldHash = seededChainHash.at(1);
            bcos::engine::ForkchoiceState fc{oldHash, oldHash, fixtureHeadHash()};
            (void)bcos::task::syncWait(service.updateForkchoice(fc, nullptr, 3));
            break;
        }
        }
        return head;
    }
};

/// Cache-less (default) and cache-enabled (production read order) service fixtures.
using ImportServiceFixture = ImportServiceFixtureT<MLS>;
using CacheImportServiceFixture = ImportServiceFixtureT<CacheMLS>;

}  // namespace op_engine_parity_test
