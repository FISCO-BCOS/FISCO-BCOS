/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

/// R6: the OP build path executes exactly once per block. buildOpPayload runs a single
/// verify=false probe through the scheduler delegate, fills the payload's commitments from
/// the probe's executed header, and adopts the retained probe (adoptProbeAsPending) instead
/// of re-executing. These tests count every delegate call on an OP-mode EngineServiceImpl
/// (c_opMode flipped by the OpSeamStubScheduler seam) and assert the counts.
///
/// OP decode-once is structural on this (carrier-based) branch: every forced/deposit and
/// sealed envelope is decoded exactly ONCE into its EngineTransaction carrier at the top of
/// buildOpPayload (detail::preparedOpTransaction / preparedOpTransactionFromSealed), and the
/// probe, ledger-gas re-probe and adopt buildOpBlock passes reuse those already-decoded
/// `.decoded` carriers (R77 by structure — there is no decode cache and therefore no
/// decode-miss seam to count here, unlike the 5488 line). The tests therefore pin the
/// engine-side contract through call counts (execute-once, adopt-once, commit-once) and by
/// asserting that the returned payload's commitments equal the probe's fabricated executed
/// header (the commitment-copy source).
///
/// The delegate (CountingOpDelegate) is a fabricated SchedulerInterface, not a real
/// OpScheduler: the engine test binary does not link opstack-executor, and a real
/// OpScheduler needs an executor + ledger + MPT state the binary does not bootstrap. What
/// the fabrication verifies is the ENGINE side of the contract (call counts, verify flag,
/// adoption of the probe's commitments); what only an integration test (or opstack-executor's
/// OpSchedulerTest) can verify is the delegate side: commitment mismatch detection in
/// adoptProbeAsPending and probe-view trie-node persistence gated by
/// feature_l2_ethereum_compat.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>

using namespace bcos;
using namespace bcos::engine;

namespace
{
using namespace bcos::protocol;
using namespace bcos::crypto;

// Whole-second milliseconds (1700000000s), same convention as EngineServiceTest.
constexpr std::uint64_t c_opBaseTimestamp = 1700000000ULL * 1000ULL;
constexpr bcos::protocol::BlockNumber c_opParentBlockNumber = 5;
// Matches the tx_gas_limit SYS_CONFIG row the fixture writes, so the eviction branch's
// "CL gasLimit differs from the chain's" re-probe is not triggered (cases pin eviction,
// not the gasLimit re-probe).
constexpr std::uint64_t c_opChainGasLimit = 30'000'000ULL;

// Placeholder commitments the CountingOpDelegate fabricates in the probe's executed header
// (fix-round G pins that buildOpPayload copies exactly these into the returned payload).
const h256 c_probeStateRoot{"4444444444444444444444444444444444444444444444444444444444444444"};
const h256 c_probeReceiptsRoot{"6666666666666666666666666666666666666666666666666666666666666666"};

bcos::protocol::BlockFactory::Ptr opTestBlockFactory()
{
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    return blockFactory;
}

// ---- Storage fixture (same shape as EngineServiceTest's, renamed for the unity build) ----

using OpMutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
using OpBackendStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::CONCURRENT),
    std::hash<bcos::executor_v1::StateKey>>;

template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct OpCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit OpCheckpointStorage(Storage& s) : m_storage(s) {}
    Storage& open() { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};

using OpCheckpointBackend = OpCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, OpBackendStorage>;
using OpGlobalStateStorage =
    bcos::storage2::MultiLayerStorage<OpMutableStorage, void, OpCheckpointBackend>;

struct OpGlobalStateFixture
{
    OpBackendStorage backendStorage;
    OpCheckpointBackend checkpointBackend{backendStorage};
    OpGlobalStateStorage storage{checkpointBackend};

    OpGlobalStateFixture()
    {
        // executor_version=2 + an explicit EVMC revision: the OP build path reads the
        // ledger config (and derives the header fork era) from these rows.
        writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        writeSysConfig(
            ledger::SYSTEM_KEY_EVMC_REVISION, ledger::encodeEVMCRevisionConfig(EVMC_CANCUN, {}));
        // The chain gas limit the CL attributes must match (eviction, not the gasLimit
        // re-probe, is what these tests pin).
        writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit),
            std::to_string(c_opChainGasLimit));
    }

    void writeSysConfig(std::string_view key, std::string value)
    {
        storage::Entry entry;
        entry.set(bcos::storage::serialize::encode(ledger::SystemConfigEntry{std::move(value), 0}));
        task::syncWait(storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
    }

    void setBlockNumber(const h256& blockHash, bcos::protocol::BlockNumber blockNumber)
    {
        storage::Entry entry;
        entry.set(boost::lexical_cast<std::string>(blockNumber));
        task::syncWait(storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{
                ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
            std::move(entry)));
    }

    /// Persist a header the way the ledger does (SYS_NUMBER_2_BLOCK_HEADER, tars-encoded),
    /// so buildOpPayload's parent reads (getLedgerConfig + calcOpBaseFee) find it.
    void writeStoredHeader(
        bcos::protocol::BlockNumber number, bcos::protocol::BlockHeader::Ptr header)
    {
        bcos::bytes encoded;
        header->encode(encoded);
        storage::Entry entry;
        entry.set(std::string(encoded.begin(), encoded.end()));
        task::syncWait(storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{
                ledger::SYS_NUMBER_2_BLOCK_HEADER, boost::lexical_cast<std::string>(number)},
            std::move(entry)));
    }

    void setNonce(std::string_view sender, std::string nonce)
    {
        evmc_address addr{};
        std::copy_n(sender.begin(), std::min(sender.size(), sizeof(addr.bytes)), addr.bytes);
        ledger::account::EVMAccount account{backendStorage, addr, false};
        task::syncWait(account.setNonce(std::move(nonce)));
    }
};

/// Genesis-style parent header with a valid Holocene extraData (the shape calcOpBaseFee
/// requires: 9 bytes, version 0x00, denom 250 / elasticity 6).
bcos::protocol::BlockHeader::Ptr makeOpParentHeader(
    bcos::protocol::BlockNumber number, std::uint64_t timestampMs)
{
    auto header = opTestBlockFactory()->blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(static_cast<std::int64_t>(timestampMs));
    header->setGasLimit(bcos::u256(c_opChainGasLimit));
    header->setGasUsed(bcos::u256(0));
    header->setBaseFee(bcos::u256(1));
    // 9-byte Holocene extraData: version 0x00 || denom 250 (u32 BE) || elasticity 6 (u32 BE).
    header->setExtraData(bcos::fromHexWithPrefix("0x00000000fa00000006"));
    // getLedgerConfig reads headerPtr->hash() on the stored parent; the tars hash must be
    // present or it throws EmptyBlockHeaderHash.
    header->calculateHash(*opTestBlockFactory()->cryptoSuite()->hashImpl());
    return header;
}

// ---- Executor / scheduler seams (renamed copies of EngineServiceTest's stubs) ----

struct OpStubExecutor
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

OpStubExecutor& opStubExecutor()
{
    static OpStubExecutor executor;
    return executor;
}

/// The OP scheduler seam: flip EngineServiceImpl::c_opMode (computeTxRoot is the seam
/// marker the engine detects) and provide the two extra members the OP build path calls.
/// computeTxRoot is a keccak over the concatenated envelopes — NOT the real DeriveSha tx
/// root. It only needs to be deterministic: the engine recomputes it at finalHeader,
/// adopt and newPayload over the same envelope set, so every comparison agrees.
struct OpSeamStubScheduler
{
    // Concept surface only; OP mode never calls the scheduler's executeBlock.
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&&, const ledger::LedgerConfig&)
    {
        co_return {};
    }

    template <class EnvelopeRange>
    static bcos::h256 computeTxRoot(EnvelopeRange const& envelopes)
    {
        bcos::bytes concatenated;
        for (auto const& envelope : envelopes)
        {
            concatenated.insert(concatenated.end(), envelope.begin(), envelope.end());
        }
        return bcos::crypto::keccak256Hash(bcos::ref(concatenated));
    }

    [[nodiscard]] bool isJovianActive() const noexcept { return false; }

    /// Deposit-tagged stand-in for the L1-attributes deposit (the real one is synthesized
    /// from [op_l1] config); the fabricated delegate never executes it. buildOpPayload
    /// decodes it via preparedOpTransaction like any forced envelope.
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope(std::uint64_t /*l2BlockTime*/) const
    {
        return bcos::bytes{0x7e, 0x01, 0x02};
    }
};

OpSeamStubScheduler& opSeamStubScheduler()
{
    static OpSeamStubScheduler scheduler;
    return scheduler;
}

// ---- The counting delegate ----

/// Minimal SchedulerInterface that fabricates OP responses and counts calls:
/// - executeBlock(verify=false): the probe. On success returns a header whose commitments
///   are deterministic functions of the input block, so the engine's payload fill and the
///   rebuilt finalHeader are self-consistent (stateRoot/receiptsRoot are fixed constants,
///   withdrawalsRoot is the empty-trie root validateOpNewPayloadRequest later requires).
///   When poisonTxHash is set and the block carries an envelope hashing to it, fails with a
///   structured OpCulpritTxHash error-info slot — the same typed-culprit contract the real
///   OpScheduler uses (OpScheduler.h catch attaches OpCulpritTxHash; buildOpPayload's
///   eviction loop reads it back via detail::culpritHashOf).
/// - adoptProbeAsPending: counts and echoes the final block's header (a real OpScheduler
///   would verify the header against the retained probe's view; that check is NOT under
///   test here).
/// - commitBlock: counts and succeeds (the ledger side is not under test).
class CountingOpDelegate : public bcos::scheduler::SchedulerInterface
{
public:
    using ExecuteCallback =
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)>;
    using CommitCallback = std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)>;

    std::atomic<std::uint64_t> executeBlockCount{0};
    std::atomic<std::uint64_t> adoptProbeAsPendingCount{0};
    std::atomic<std::uint64_t> commitBlockCount{0};
    /// The verify flag of every executeBlock call, in call order (single-threaded tests).
    std::vector<bool> verifyFlags;
    std::optional<h256> poisonTxHash;
    bcos::protocol::BlockHeader::Ptr lastAdoptedHeader;
    /// The executed header fabricated by the last successful probe. buildOpPayload copies its
    /// commitments (stateRoot/receiptsRoot/gasUsed/logsBloom/withdrawalsRoot/blobGasUsed)
    /// into the payload; fix-round G asserts the getPayload result equals these values.
    bcos::protocol::BlockHeader::Ptr lastProbeExecutedHeader;

    void executeBlock(
        bcos::protocol::Block::Ptr block, bool verify, ExecuteCallback callback) override
    {
        ++executeBlockCount;
        verifyFlags.push_back(verify);
        if (poisonTxHash.has_value())
        {
            for (const auto& holder : block->transactions())
            {
                const auto& tx = *holder;
                auto const envelopeHash = keccak256Hash(tx.extraTransactionBytes());
                if (envelopeHash == *poisonTxHash)
                {
                    auto error = BCOS_ERROR_PTR(-1, "poisoned transaction");
                    *error << bcos::engine::OpCulpritTxHash(envelopeHash);
                    callback(std::move(error), nullptr, false);
                    return;
                }
            }
        }
        lastProbeExecutedHeader = executedHeaderFor(*block);
        callback(nullptr, lastProbeExecutedHeader, false);
    }

    void adoptProbeAsPending(bcos::protocol::Block::Ptr block, ExecuteCallback callback) override
    {
        ++adoptProbeAsPendingCount;
        lastAdoptedHeader = block->blockHeader();
        callback(nullptr, lastAdoptedHeader, false);
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr, CommitCallback callback) override
    {
        ++commitBlockCount;
        callback(nullptr, nullptr);
    }

    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback(nullptr, nullptr);
    }

    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        callback(nullptr, nullptr);
    }

    void reset(std::function<void(Error::Ptr)> callback) override { callback(nullptr); }

    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }

    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }

    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

private:
    bcos::protocol::BlockHeader::Ptr executedHeaderFor(bcos::protocol::Block const& block)
    {
        auto header = opTestBlockFactory()->blockHeaderFactory()->createBlockHeader();
        header->setNumber(block.blockHeader()->number());
        // Deterministic placeholder commitments; buildOpPayload copies exactly these into
        // the payload before rebuilding the final header from them.
        header->setStateRoot(c_probeStateRoot);
        header->setReceiptsRoot(c_probeReceiptsRoot);
        header->setGasUsed(bcos::u256(0));
        // buildOpPayload copies this into payload.withdrawalsRoot; validateOpNewPayloadRequest
        // (newPayload V4) requires the value to equal withdrawalsRootFor() — the empty-trie root.
        header->setWithdrawalsRoot(bcos::ledger::mpt::emptyRootHash());
        return header;
    }
};

// ---- Engine construction ----

using OpEngineServiceImpl = bcos::engine::EngineServiceImpl<bcos::txpool::MemPoolImpl,
    OpGlobalStateStorage, OpStubExecutor, OpSeamStubScheduler>;

OpEngineServiceImpl makeOpEngineServiceImpl(bcos::txpool::MemPoolImpl& memPool,
    OpGlobalStateStorage& storage, bcos::scheduler::SchedulerInterface::Ptr delegate)
{
    return OpEngineServiceImpl(memPool, storage, opStubExecutor(), opSeamStubScheduler(),
        opTestBlockFactory(), /*ledger=*/nullptr,
        /*blockTxCountLimit=*/bcos::engine::c_defaultBlockTxCountLimit,
        /*maxEngineVersion=*/static_cast<std::uint32_t>(ApiVersion::V3), std::move(delegate));
}

// ---- Request helpers ----

bcos::engine::ForkchoiceState makeOpForkchoiceState(char seed)
{
    const std::string hex(64, seed);
    return {h256(hex), h256(hex), h256(hex)};
}

bcos::engine::PayloadAttributes makeOpBuildPayloadAttributes(std::uint64_t timestampMs)
{
    bcos::engine::PayloadAttributes attributes;
    attributes.timestamp = timestampMs;
    attributes.prevRandao =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    attributes.suggestedFeeRecipient = Address("1234567890abcdef1234567890abcdef12345678");
    attributes.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attributes.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    attributes.gasLimit = c_opChainGasLimit;
    // All-zero Holocene pair -> the Canyon constants (250, 6) are encoded.
    attributes.eip1559Params = bcos::bytes(8, 0);
    return attributes;
}

/// A Web3-typed transaction with a real EIP-1559 signing payload (same shape as
/// EngineServiceTest's makeWeb3Tx): keccak256(envelope) == tx->hash().
class OpTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

protocol::Transaction::Ptr makeOpWeb3Tx(std::string_view senderBytes, std::uint64_t nonce)
{
    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<std::uint64_t>(1));  // chainId
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<std::uint64_t>(1));      // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<std::uint64_t>(1));      // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<std::uint64_t>(21000));  // gasLimit
    bcos::codec::rlp::encode(body, Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd"));
    bcos::codec::rlp::encode(body, static_cast<std::uint64_t>(0));  // value
    bcos::codec::rlp::encode(body, bcos::bytes{});                  // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);               // empty accessList
    bcos::bytes payload;
    payload.push_back(0x02);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());

    auto tx = std::make_shared<OpTestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(payload.begin(), payload.end());
    bcos::bytes signature(65, 0);
    signature[31] = 0x12;  // r != 0
    signature[63] = 0x34;  // s != 0
    signature[64] = 0x01;  // yParity
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    bcos::bytes sender{senderBytes.begin(), senderBytes.end()};
    tx->forceSender(sender);
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<std::int64_t>(nonce));
    return tx;
}

std::size_t rawTransactionCount(bcos::engine::GetPayloadResult const& payload)
{
    // Carrier-based payload: the wire envelopes live in ExecutionPayload::transactions.
    return payload->executionPayload.transactions.size();
}

/// Fix-round G: the returned payload's commitments must be the probe's fabricated executed
/// header values (buildOpPayload copies stateRoot/receiptsRoot/gasUsed from the probe).
void checkPayloadCopiesProbeCommitments(bcos::engine::GetPayloadResult const& payload,
    CountingOpDelegate const& delegate, bcos::protocol::BlockNumber expectedNumber)
{
    BOOST_REQUIRE(delegate.lastProbeExecutedHeader);
    BOOST_CHECK_EQUAL(
        delegate.lastProbeExecutedHeader->number(), static_cast<int64_t>(expectedNumber));
    BOOST_CHECK(payload->executionPayload.stateRoot == c_probeStateRoot);
    BOOST_CHECK(payload->executionPayload.receiptsRoot == c_probeReceiptsRoot);
    BOOST_CHECK(
        payload->executionPayload.stateRoot == delegate.lastProbeExecutedHeader->stateRoot());
    BOOST_CHECK(
        payload->executionPayload.receiptsRoot == delegate.lastProbeExecutedHeader->receiptsRoot());
    BOOST_CHECK(payload->executionPayload.gasUsed ==
                bcos::u256(delegate.lastProbeExecutedHeader->gasUsed()));
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpBuildPayloadSuite)

// The core R6 assertion: FCU-with-attrs -> getPayload -> newPayload of the node's OWN
// payload runs the delegate's executeBlock exactly ONCE (the verify=false probe; the
// canonical re-execution is gone), adopts the probe exactly once, and commits the pending
// block once. Decode-once is structural on this branch (each envelope is decoded once into
// its EngineTransaction carrier at the top of buildOpPayload; probe and adopt reuse the
// carriers) and carries no miss counter, so no decode-count assertion exists here.
BOOST_AUTO_TEST_CASE(op_build_executes_once_per_block)
{
    bcos::txpool::MemPoolImpl memPool;
    OpGlobalStateFixture fixture;
    auto forkchoiceState = makeOpForkchoiceState('a');
    fixture.setBlockNumber(forkchoiceState.headBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.safeBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.finalizedBlockHash, c_opParentBlockNumber);
    fixture.writeStoredHeader(
        c_opParentBlockNumber, makeOpParentHeader(c_opParentBlockNumber, c_opBaseTimestamp - 3000));

    std::string sender("aaaaaaaaaaaaaaaaaaaa", 20);
    auto tx = makeOpWeb3Tx(sender, 0);
    memPool.add(std::vector<protocol::Transaction::Ptr>{tx});
    fixture.setNonce(sender, "0");

    auto delegate = std::make_shared<CountingOpDelegate>();
    auto engineService = makeOpEngineServiceImpl(memPool, fixture.storage, delegate);

    auto attributes = makeOpBuildPayloadAttributes(c_opBaseTimestamp);
    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    // Probe only: exactly one executeBlock, with verify=false, and one adopt.
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);
    BOOST_REQUIRE_EQUAL(delegate->verifyFlags.size(), 1);
    BOOST_CHECK_EQUAL(static_cast<bool>(delegate->verifyFlags[0]), false);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 5));
    BOOST_REQUIRE(payload);
    // Envelopes: the synthesized L1-attributes deposit plus the sealed pool transaction.
    auto const envelopeCount = rawTransactionCount(payload);
    BOOST_CHECK_EQUAL(envelopeCount, 2);
    // The probe's commitments (stateRoot/receiptsRoot/gasUsed) were copied into the payload.
    checkPayloadCopiesProbeCommitments(payload, *delegate, c_opParentBlockNumber + 1);

    // newPayload of the node's own payload: committed via the adopted pending block
    // without re-execution and without another decode pass.
    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = task::syncWait(engineService.newPayload(request, 4));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));

    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->commitBlockCount.load(), 1);
}

// I11-B path: with no sealed pool transactions (forced/deposit-only block) there is
// nothing to evict; the single probe is adopted directly.
BOOST_AUTO_TEST_CASE(op_empty_pool_build_skips_eviction)
{
    bcos::txpool::MemPoolImpl memPool;
    OpGlobalStateFixture fixture;
    auto forkchoiceState = makeOpForkchoiceState('b');
    fixture.setBlockNumber(forkchoiceState.headBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.safeBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.finalizedBlockHash, c_opParentBlockNumber);
    fixture.writeStoredHeader(
        c_opParentBlockNumber, makeOpParentHeader(c_opParentBlockNumber, c_opBaseTimestamp - 3000));

    auto delegate = std::make_shared<CountingOpDelegate>();
    auto engineService = makeOpEngineServiceImpl(memPool, fixture.storage, delegate);

    // One forced deposit, an empty pool: exactly one envelope.
    auto attributes = makeOpBuildPayloadAttributes(c_opBaseTimestamp);
    attributes.transactions = std::vector<std::string>{"0x7e0102030405"};
    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);
    BOOST_REQUIRE_EQUAL(delegate->verifyFlags.size(), 1);
    BOOST_CHECK_EQUAL(static_cast<bool>(delegate->verifyFlags[0]), false);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 5));
    auto const envelopeCount = rawTransactionCount(payload);
    BOOST_CHECK_EQUAL(envelopeCount, 1);
    checkPayloadCopiesProbeCommitments(payload, *delegate, c_opParentBlockNumber + 1);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = task::syncWait(engineService.newPayload(request, 4));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->commitBlockCount.load(), 1);
}

// Eviction path: a poisoned pool transaction fails the first probe (typed OpCulpritTxHash
// error), is evicted, and the retry succeeds; the final build still ends with exactly one
// adopt, one successful probe, and the payload commits. The probe attempts live inside one
// buildOpPayload whose envelopes were decoded once into their carriers up front, so retries
// do not re-decode (structural decode-once, no per-build cache to clear).
BOOST_AUTO_TEST_CASE(op_eviction_path_adopts_after_retry)
{
    bcos::txpool::MemPoolImpl memPool;
    OpGlobalStateFixture fixture;
    auto forkchoiceState = makeOpForkchoiceState('c');
    fixture.setBlockNumber(forkchoiceState.headBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.safeBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.finalizedBlockHash, c_opParentBlockNumber);
    fixture.writeStoredHeader(
        c_opParentBlockNumber, makeOpParentHeader(c_opParentBlockNumber, c_opBaseTimestamp - 3000));

    std::string sender("bbbbbbbbbbbbbbbbbbbb", 20);
    auto poisonedTx = makeOpWeb3Tx(sender, 0);
    memPool.add(std::vector<protocol::Transaction::Ptr>{poisonedTx});
    fixture.setNonce(sender, "0");

    auto delegate = std::make_shared<CountingOpDelegate>();
    delegate->poisonTxHash = poisonedTx->hash();
    auto engineService = makeOpEngineServiceImpl(memPool, fixture.storage, delegate);

    auto attributes = makeOpBuildPayloadAttributes(c_opBaseTimestamp);
    auto result = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result.payloadId.has_value());

    // Two probes (fail + retry), both verify=false; exactly one adopt afterwards.
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 2);
    BOOST_REQUIRE_EQUAL(delegate->verifyFlags.size(), 2);
    BOOST_CHECK_EQUAL(static_cast<bool>(delegate->verifyFlags[0]), false);
    BOOST_CHECK_EQUAL(static_cast<bool>(delegate->verifyFlags[1]), false);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);

    auto payload = task::syncWait(engineService.getPayload(*result.payloadId, 5));
    // The final block carries only the synthesized deposit — the poisoned envelope is gone.
    auto const finalEnvelopeCount = rawTransactionCount(payload);
    BOOST_CHECK_EQUAL(finalEnvelopeCount, 1);
    checkPayloadCopiesProbeCommitments(payload, *delegate, c_opParentBlockNumber + 1);

    // The payload commits without re-execution.
    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto status = task::syncWait(engineService.newPayload(request, 4));
    BOOST_CHECK_EQUAL(
        static_cast<int>(status.status), static_cast<int>(PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 2);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->commitBlockCount.load(), 1);
}

// Two consecutive FCU builds in one engine instance each execute exactly once and adopt
// exactly once (the R6 contract is per build, not per instance). This replaces the 5488
// line's decode-cache-lifetime case (op_decode_cache_is_cleared_per_build): its premise — a
// per-build decode cache to clear — does not exist on this carrier-based branch, where
// decode-once is structural. The two builds use different attribute timestamps so the second
// build walks a distinct parent (block 1's adopted header).
BOOST_AUTO_TEST_CASE(op_two_consecutive_builds_execute_once)
{
    bcos::txpool::MemPoolImpl memPool;
    OpGlobalStateFixture fixture;
    auto forkchoiceState = makeOpForkchoiceState('d');
    fixture.setBlockNumber(forkchoiceState.headBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.safeBlockHash, c_opParentBlockNumber);
    fixture.setBlockNumber(forkchoiceState.finalizedBlockHash, c_opParentBlockNumber);
    fixture.writeStoredHeader(
        c_opParentBlockNumber, makeOpParentHeader(c_opParentBlockNumber, c_opBaseTimestamp - 3000));

    auto delegate = std::make_shared<CountingOpDelegate>();
    auto engineService = makeOpEngineServiceImpl(memPool, fixture.storage, delegate);

    // Block 1: deposit-only, one envelope, at height 6.
    auto attributes = makeOpBuildPayloadAttributes(c_opBaseTimestamp);
    auto result1 = task::syncWait(engineService.updateForkchoice(forkchoiceState, &attributes, 3));
    BOOST_REQUIRE(result1.payloadId.has_value());
    auto payload1 = task::syncWait(engineService.getPayload(*result1.payloadId, 5));
    BOOST_CHECK_EQUAL(rawTransactionCount(payload1), 1);
    checkPayloadCopiesProbeCommitments(payload1, *delegate, c_opParentBlockNumber + 1);
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 1);
    // A re-query of the same build does not execute again.
    auto payload1Again = task::syncWait(engineService.getPayload(*result1.payloadId, 5));
    BOOST_CHECK_EQUAL(rawTransactionCount(payload1Again), 1);
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 1);

    // Block 2: head = block 1's hash at height 6; the stored parent header is block 1's
    // adopted (final) header, re-hashed with the tars hasher so getLedgerConfig can read
    // hash() (the eth RLP hash is not carried in the tars dataHash field).
    auto const block1Hash = payload1->executionPayload.blockHash;
    fixture.setBlockNumber(block1Hash, c_opParentBlockNumber + 1);
    auto block1Header = delegate->lastAdoptedHeader;
    BOOST_REQUIRE(block1Header);
    block1Header->calculateHash(*opTestBlockFactory()->cryptoSuite()->hashImpl());
    fixture.writeStoredHeader(c_opParentBlockNumber + 1, block1Header);

    auto forkchoiceState2 = bcos::engine::ForkchoiceState{block1Hash, block1Hash, block1Hash};
    auto attributes2 = makeOpBuildPayloadAttributes(c_opBaseTimestamp + 5000);
    auto result2 =
        task::syncWait(engineService.updateForkchoice(forkchoiceState2, &attributes2, 3));
    BOOST_REQUIRE(result2.payloadId.has_value());

    // The second build ran its own single probe + adopt.
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 2);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 2);
    auto payload2 = task::syncWait(engineService.getPayload(*result2.payloadId, 5));
    BOOST_CHECK_EQUAL(rawTransactionCount(payload2), 1);
    checkPayloadCopiesProbeCommitments(payload2, *delegate, c_opParentBlockNumber + 2);
    BOOST_CHECK_EQUAL(delegate->executeBlockCount.load(), 2);
    BOOST_CHECK_EQUAL(delegate->adoptProbeAsPendingCount.load(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
