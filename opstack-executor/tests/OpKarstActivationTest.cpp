// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Q5: Jovian+ activation blocks are deposits-only, driven by the timestamp schedule
// (not the 176-byte L1-attributes heuristic). Karst fixtures parse after Jovian.

#include "support/KarstNutHelpers.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/OpTime.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <opstack-executor/OpEthBlockExecute.h>
#include <opstack-executor/OpEthBlockSteps.h>
#include <opstack-executor/OpEthDeposit.h>
#include <opstack-executor/OpEthExecutor.h>
#include <opstack-executor/OpEthL1Attributes.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpForkSpec.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using bcos::evm::OpConsensusError;
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace engine = bcos::evm::engine;
namespace opeth = bcos::executor_v1::opstack;
namespace lop = bcos::ledger;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;

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

using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;

struct UnusedView
{
};

constexpr uint64_t kJovianTsSec = 100;
constexpr int64_t kJovianTsMs = static_cast<int64_t>(kJovianTsSec) * 1000;
constexpr uint64_t kParentTsSec = 99;

const bcos::bytes kDepositEnvelope{bcos::byte{0x7e}, bcos::byte{0x01}};
const bcos::bytes kTypedEnvelope{bcos::byte{0x02}, bcos::byte{0x01}};

opeth::DepositTx depositWithJovianAttrs()
{
    bcos::bytes data(opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN, bcos::byte{0});
    std::memcpy(
        data.data(), opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.data(),
        opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.size());
    opeth::DepositTx dep{};
    dep.gasLimit = 1'000'000;
    dep.data = std::move(data);
    return dep;
}

opeth::DepositTx depositWithIsthmusLenAttrs()
{
    bcos::bytes data(opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN, bcos::byte{0});
    opeth::DepositTx dep{};
    dep.gasLimit = 1'000'000;
    dep.data = std::move(data);
    return dep;
}

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader(int64_t timestampMs)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(timestampMs);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setGasLimit(bcos::u256(30'000'000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    return h;
}

bool isActivationUserTxError(OpConsensusError const& e)
{
    auto const w = std::string_view{e.what()};
    return w.find("unexpected non-deposit") != std::string_view::npos;
}

void runPreBlock(bcos::ledger::OpForkSchedule const& schedule, uint64_t /*parentTsSec*/,
    int64_t blockTsMs, std::vector<bcos::bytes> const& rawTxs,
    std::vector<opeth::DepositTx> const& deposits)
{
    MutableStorage storage;
    auto header = makeHeader(blockTsMs);
    evmc::VM vm{evmc_create_evmone()};
    std::shared_ptr<opeth::OpStorageErrorSlot> errorSlot =
        std::make_shared<opeth::OpStorageErrorSlot>();
    std::optional<opeth::OpRecentBlockHashes<MutableStorage>> hashes;
    std::optional<std::string> hashErr;
    std::optional<uint16_t> scalar;
    const auto spec = opeth::opForkSpecAt(schedule, static_cast<uint64_t>(blockTsMs / 1000));
    bcos::task::syncWait(opeth::preBlockOpEthSteps(storage, *header, spec, rawTxs, deposits, vm,
        errorSlot, hashes, hashErr, scalar));
}

bcos::protocol::Transaction::Ptr envelopeToTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const txHash = hashImpl->hash(env);
    bcostars::Transaction tars;
    tars.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tars.extraTransactionHash.assign(txHash.begin(), txHash.end());
    tars.extraTransactionBytes.assign(env.begin(), env.end());
    if (!env.empty())
        tars.web3TypedTxKind = static_cast<tars::Char>(env[0]);
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
}

/// Execute an activation-height block with no parent header row in storage.
bcos::Error::Ptr executeActivationWithoutParent(bcos::ledger::OpForkSchedule schedule)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend(backendStorage);
    MLS mls(checkpointBackend);
    auto crypto =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto hashImpl = crypto->hashImpl();
    auto headerFactory = std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(crypto);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(crypto);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(crypto);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        crypto, headerFactory, txFactory, receiptFactory);
    auto io = std::make_shared<bcos::IOServicePool>(1);
    auto scheduler = std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
        hashImpl, /*chainId=*/0x2105, schedule, blockFactory, mls, /*ledger=*/nullptr, io);

    auto depEnv = opeth::encodeOpEthDepositEnvelope(depositWithJovianAttrs());
    auto header = makeHeader(kJovianTsMs);
    auto block = blockFactory->createBlock();
    block->setBlockHeader(header);
    block->appendTransaction(envelopeToTx(depEnv, hashImpl));
    block->appendTransaction(envelopeToTx(kTypedEnvelope, hashImpl));

    bcos::Error::Ptr err;
    bool called = false;
    scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr, bool) {
            called = true;
            err = std::move(e);
        });
    BOOST_REQUIRE(called);
    return err;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstActivationSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(JovianActivationBlockRejectsUserTx, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // 178B Jovian attrs: the 176-byte L1-attributes heuristic must not be what rejects.
    // Header is internal milliseconds; schedule activations are Unix seconds (A13).
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    BOOST_CHECK_EQUAL(
        bcos::engine::unixSecondsFromInternalMillis(static_cast<uint64_t>(kJovianTsMs)),
        kJovianTsSec);

    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(runPreBlock(schedule, kParentTsSec, kJovianTsMs,
                              {kDepositEnvelope, kTypedEnvelope}, {dep, opeth::DepositTx{}}),
        OpConsensusError, isActivationUserTxError);
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianActivationBlockAllowsDepositsOnly, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_NO_THROW(runPreBlock(schedule, kParentTsSec, kJovianTsMs,
        {kDepositEnvelope}, {dep}));
    BOOST_CHECK_NO_THROW(runPreBlock(schedule, kParentTsSec, kJovianTsMs,
        {kDepositEnvelope, kDepositEnvelope}, {dep, dep}));
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianActivationBlockRejectsUserTxBeforeTrailingDeposit, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // Q5 must scan every envelope: a trailing deposit must not hide a user tx.
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(
        runPreBlock(schedule, kParentTsSec, kJovianTsMs,
            {kDepositEnvelope, kTypedEnvelope, kDepositEnvelope}, {dep, opeth::DepositTx{}, dep}),
        OpConsensusError, isActivationUserTxError);
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianActivationIsthmusLenAttrsRejectsMiddleUserTx, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // 176-byte Isthmus attrs on a Jovian activation block: DA-shape last-tx
    // would accept [deposit, user, deposit]; Q5 must not.
    auto schedule = opstack_test::isthmusThenJovian(kJovianTsSec);
    auto dep = depositWithIsthmusLenAttrs();
    BOOST_REQUIRE_EQUAL(dep.data.size(), opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    BOOST_CHECK_EXCEPTION(
        runPreBlock(schedule, kParentTsSec, kJovianTsMs,
            {kDepositEnvelope, kTypedEnvelope, kDepositEnvelope}, {dep, opeth::DepositTx{}, dep}),
        OpConsensusError, isActivationUserTxError);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstActivationBlockRejectsUserTx, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto schedule = opstack_test::karstOnlySchedule(/*karstTs=*/kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(runPreBlock(schedule, kParentTsSec, kJovianTsMs,
                              {kDepositEnvelope, kTypedEnvelope}, {dep, opeth::DepositTx{}}),
        OpConsensusError, isActivationUserTxError);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstActivationBlockRejectsUserTxBeforeTrailingDeposit, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto schedule = opstack_test::karstOnlySchedule(/*karstTs=*/kJovianTsSec);
    auto dep = depositWithJovianAttrs();
    BOOST_CHECK_EXCEPTION(
        runPreBlock(schedule, kParentTsSec, kJovianTsMs,
            {kDepositEnvelope, kTypedEnvelope, kDepositEnvelope}, {dep, opeth::DepositTx{}, dep}),
        OpConsensusError, isActivationUserTxError);
}

// clang-format off

// The engine-API profile selection surface, ported to the post-cutover fork model:
// resolve the fork from the chain's own OpForkSchedule (ledger::resolveOpFork — the ONE
// fork-activation parser), then read the constexpr profile table (OpForkId.h). The old
// seam method (resolveEngineForkAt over a boolean-flag schedule) is retired; the table
// and its static_asserts are the production surface these cases pin.
namespace
{
bcos::engine::EngineForkResolution resolveEngineForkForTest(
    const bcos::ledger::OpForkSchedule& schedule, uint64_t timestampSeconds)
{
    using bcos::engine::EngineForkContext;
    using bcos::engine::OpForkId;
    const auto fork = bcos::ledger::resolveOpFork(schedule, timestampSeconds);
    static constexpr std::array<std::pair<OpForkId, bcos::ledger::OpFork>, 9> kIdByLadder{{
        {OpForkId::Regolith, bcos::ledger::OpFork::Regolith},
        {OpForkId::Canyon, bcos::ledger::OpFork::Canyon},
        {OpForkId::Ecotone, bcos::ledger::OpFork::Ecotone},
        {OpForkId::Fjord, bcos::ledger::OpFork::Fjord},
        {OpForkId::Granite, bcos::ledger::OpFork::Granite},
        {OpForkId::Holocene, bcos::ledger::OpFork::Holocene},
        {OpForkId::Isthmus, bcos::ledger::OpFork::Isthmus},
        {OpForkId::Jovian, bcos::ledger::OpFork::Jovian},
        {OpForkId::Karst, bcos::ledger::OpFork::Karst},
    }};
    auto forkId = OpForkId::Isthmus;
    bool matched = false;
    for (auto const& [id, ledgerFork] : kIdByLadder)
    {
        if (ledgerFork == fork)
        {
            forkId = id;
            matched = true;
        }
    }
    // Delta (and any future rung the table forgets) must not fall through to the
    // Isthmus profile silently — an unmapped fork is a mapping bug, not Isthmus.
    if (!matched)
    {
        return bcos::engine::OpForkResolutionError::InconsistentExecutionConfig;
    }
    return EngineForkContext{
        .forkId = forkId, .api = bcos::engine::engineApiProfileFor(forkId),
        .hasDaFootprint = forkId >= OpForkId::Jovian,
        .extraDataLayout = bcos::engine::extraDataLayoutFor(forkId)};
}

bcos::ledger::OpForkSchedule karstAtSchedule(uint64_t karstTs)
{
    bcos::ledger::OpForkSchedule schedule;
    // jovian implied at karst's second (the fold rule, same as KarstNutHelpers):
    // without this the ts<karstTs arm resolves Isthmus and the case cannot
    // discriminate Jovian from Isthmus.
    schedule.m_jovianTime = karstTs;
    schedule.m_karstTime = karstTs;
    return schedule;
}
}  // namespace

BOOST_AUTO_TEST_CASE(ResolveEngineForkAtKarstSelectsGetPayloadV5, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto schedule = karstAtSchedule(/*karstTs=*/100);
    auto resolved = resolveEngineForkForTest(schedule, 100);
    auto* ctx = std::get_if<bcos::engine::EngineForkContext>(&resolved);
    BOOST_REQUIRE(ctx);
    BOOST_CHECK(ctx->forkId == bcos::engine::OpForkId::Karst);
    BOOST_CHECK(ctx->api.getPayload == bcos::engine::ApiVersion::V5);
    BOOST_CHECK(ctx->api.forkchoiceUpdated == bcos::engine::ApiVersion::V3);
    BOOST_CHECK(ctx->api.newPayload == bcos::engine::ApiVersion::V4);
    auto jov = resolveEngineForkForTest(schedule, 99);
    BOOST_CHECK(std::get<bcos::engine::EngineForkContext>(jov).api.getPayload ==
                bcos::engine::ApiVersion::V4);
}

// clang-format off
// clang-format off
BOOST_AUTO_TEST_CASE(ResolveEngineForkBelowActivationFallsBackToIsthmusBaseline,
    * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
// clang-format on
{
    // Jovian scheduled at 50: timestamps below the activation still resolve under the
    // isthmus-baseline semantics (Isthmus is the zero-start fallback — there is no
    // "unsupported" answer in the new model), and Jovian rules apply exactly from 50.
    const bcos::ledger::OpForkSchedule schedule = [] {
        bcos::ledger::OpForkSchedule s;
        s.m_jovianTime = 50;
        return s;
    }();
    const auto before = resolveEngineForkForTest(schedule, 49);
    BOOST_CHECK(std::get_if<bcos::engine::EngineForkContext>(&before) != nullptr);
    const auto at = resolveEngineForkForTest(schedule, 50);
    BOOST_CHECK(std::get<bcos::engine::EngineForkContext>(at).forkId ==
                bcos::engine::OpForkId::Jovian);
}

BOOST_AUTO_TEST_CASE(ResolveEngineForkAtEcotoneSelectsV3, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // Ecotone resolved via an explicit ladder (isthmus above the sample ts keeps the
    // zero-start Isthmus fallback out of the way, so the ecotone row is what resolves).
    bcos::ledger::OpForkSchedule schedule;
    schedule.m_ecotoneTime = 0;
    schedule.m_isthmusTime = 100;
    auto resolved = resolveEngineForkForTest(schedule, 0);
    auto* ctx = std::get_if<bcos::engine::EngineForkContext>(&resolved);
    BOOST_REQUIRE(ctx);
    BOOST_CHECK(ctx->forkId == bcos::engine::OpForkId::Ecotone);
    BOOST_CHECK(ctx->api.getPayload == bcos::engine::ApiVersion::V3);
    BOOST_CHECK(ctx->api.forkchoiceUpdated == bcos::engine::ApiVersion::V3);
    BOOST_CHECK(ctx->api.newPayload == bcos::engine::ApiVersion::V3);
}

BOOST_AUTO_TEST_CASE(EngineApiProfileTableMatchesOpNode, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // Full ladder with explicit rung times (the old TestBypass shape, now the ledger
    // schedule): sample each rung's activation second and check the profile table row.
    bcos::ledger::OpForkSchedule schedule;
    schedule.m_regolithTime = 0;
    schedule.m_canyonTime = 100;
    schedule.m_ecotoneTime = 200;
    schedule.m_holoceneTime = 300;
    schedule.m_isthmusTime = 400;
    schedule.m_jovianTime = 500;
    schedule.m_karstTime = 600;

    struct Row
    {
        uint64_t ts;
        bcos::engine::OpForkId fork;
        bcos::engine::ApiVersion fcu;
        bcos::engine::ApiVersion get;
        bcos::engine::ApiVersion np;
        bcos::engine::OpExtraDataLayout extra;
    };
    const Row rows[] = {
        {0, bcos::engine::OpForkId::Regolith, bcos::engine::ApiVersion::V1,
            bcos::engine::ApiVersion::V2, bcos::engine::ApiVersion::V2,
            bcos::engine::OpExtraDataLayout::Empty},
        {100, bcos::engine::OpForkId::Canyon, bcos::engine::ApiVersion::V2,
            bcos::engine::ApiVersion::V2, bcos::engine::ApiVersion::V2,
            bcos::engine::OpExtraDataLayout::Empty},
        {200, bcos::engine::OpForkId::Ecotone, bcos::engine::ApiVersion::V3,
            bcos::engine::ApiVersion::V3, bcos::engine::ApiVersion::V3,
            bcos::engine::OpExtraDataLayout::Empty},
        {600, bcos::engine::OpForkId::Karst, bcos::engine::ApiVersion::V3,
            bcos::engine::ApiVersion::V5, bcos::engine::ApiVersion::V4,
            bcos::engine::OpExtraDataLayout::Jovian17},
        {550, bcos::engine::OpForkId::Jovian, bcos::engine::ApiVersion::V3,
            bcos::engine::ApiVersion::V4, bcos::engine::ApiVersion::V4,
            bcos::engine::OpExtraDataLayout::Jovian17},
        {450, bcos::engine::OpForkId::Isthmus, bcos::engine::ApiVersion::V3,
            bcos::engine::ApiVersion::V4, bcos::engine::ApiVersion::V4,
            bcos::engine::OpExtraDataLayout::Holocene9},
        {350, bcos::engine::OpForkId::Holocene, bcos::engine::ApiVersion::V3,
            bcos::engine::ApiVersion::V3, bcos::engine::ApiVersion::V3,
            bcos::engine::OpExtraDataLayout::Holocene9},
    };
    for (auto const& row : rows)
    {
        auto resolved = resolveEngineForkForTest(schedule, row.ts);
        auto* ctx = std::get_if<bcos::engine::EngineForkContext>(&resolved);
        BOOST_REQUIRE_MESSAGE(ctx, "ts=" << row.ts);
        BOOST_CHECK(ctx->forkId == row.fork);
        BOOST_CHECK(ctx->api.forkchoiceUpdated == row.fcu);
        BOOST_CHECK(ctx->api.getPayload == row.get);
        BOOST_CHECK(ctx->api.newPayload == row.np);
        BOOST_CHECK(ctx->extraDataLayout == row.extra);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianActivationWithoutParentHeaderFailsClosed, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto err = executeActivationWithoutParent(opstack_test::isthmusThenJovian(kJovianTsSec));
    BOOST_REQUIRE(err);
    BOOST_CHECK_EQUAL(
        err->errorCode(), static_cast<int>(bcos::scheduler::SchedulerError::OpStorageFault));
    auto const msg = err->errorMessage();
    BOOST_CHECK_MESSAGE(
        msg.find("parent block header is missing from storage") != std::string::npos,
        "missing parent must fail closed as OpStorageFault; got: " + msg);
}

BOOST_AUTO_TEST_SUITE_END()