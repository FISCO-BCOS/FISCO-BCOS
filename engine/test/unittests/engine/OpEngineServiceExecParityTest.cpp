// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
//
// Matrix: S6 — OpEngineService × golden (op-geth) execution parity on release-3.18.0.
// Dual parity vs EngineServiceImpl OP mode is unavailable on this branch (no Impl opMode).
// Carrier: transactions[i].raw via parseNewPayloadRequest(V4).
// Golden fields (stateRoot/receiptsRoot/gasUsed/txRoot/blockHash) are asserted against
// OpEngineService::lastExecutedHeader() after newPayload — NOT rebuildOpEthHeader(request),
// which copies those fields from the JSON and stays green if execution is skipped.

#include "support/GoldenSample.h"
#include "support/SeedPreState.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-table/src/LegacyStorageWrapper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/OpEngineService.h>
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace op_engine_exec_parity
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

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

constexpr uint64_t kChainId = 0x2105;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
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

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

bcos::evm::opstack::OpForkFlags forkFlagsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkFlags{.jovianActive = jovian};
}

void seedSysTables(MLS& multiLayerStorage)
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

void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
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

struct OpE2eFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::crypto::Hash::Ptr hashImpl;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory;
    EngineOpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> opDelegate;
    OpEngine service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkFlags forkFlags)
      : hashImpl(makeCryptoSuite()->hashImpl()),
        receiptFactory(makeReceiptFactory()),
        scheduler(forkFlags, {}),
        legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                backendStorage)),
        ledger(std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        opDelegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
            hashImpl, kChainId, forkFlags, blockFactory, multiLayerStorage, ledger, ioServicePool)),
        service(memPool, multiLayerStorage, scheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3), opDelegate)
    {
        seedSysTables(multiLayerStorage);
    }
};

bcos::protocol::BlockHeader::Ptr productionHeaderOf(
    bcos::protocol::BlockFactory::Ptr const& blockFactory,
    bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    std::vector<bcos::bytes> envelopes;
    envelopes.reserve(payload.transactions.size());
    for (auto const& tx : payload.transactions)
    {
        envelopes.push_back(tx.raw);
    }
    const auto transactionsRoot = EngineOpScheduler::computeTxRoot(envelopes);
    return bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory->blockHeaderFactory(),
        payload, transactionsRoot, *request.parentBeaconBlockRoot);
}

void assertExecutionCommitments(std::string const& id,
    bcos::protocol::BlockHeader::Ptr const& produced,
    bcostars::protocol::BlockHeaderImpl::Ptr const& goldenHeader)
{
    // These four fields are copied from OpScheduler execution (finishExecute), not
    // from the request JSON. If newPayload skipped execute, lastExecutedHeader is
    // null and the caller fails before reaching here.
    BOOST_CHECK_MESSAGE(produced->stateRoot() == goldenHeader->stateRoot(), id << ": stateRoot");
    BOOST_CHECK_MESSAGE(
        produced->receiptsRoot() == goldenHeader->receiptsRoot(), id << ": receiptsRoot");
    BOOST_CHECK_MESSAGE(produced->gasUsed() == goldenHeader->gasUsed(), id << ": gasUsed");
    BOOST_CHECK_MESSAGE(produced->txsRoot() == goldenHeader->txsRoot(), id << ": txRoot");
}

void assertRebuiltAnnouncement(std::string const& id,
    bcos::protocol::BlockHeader::Ptr const& rebuilt,
    bcostars::protocol::BlockHeaderImpl::Ptr const& goldenHeader, bcos::h256 const& goldenBlockHash)
{
    BOOST_CHECK_MESSAGE(bcos::protocol::EthBlockHeader::computeHash(*rebuilt) == goldenBlockHash,
        id << ": blockHash");
    assertExecutionCommitments(id, rebuilt, goldenHeader);
}

void runGoldenVector(std::string const& id)
{
    auto sample = w6test::loadVectorSample(id);
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        id << ": expected VALID, got " << static_cast<int>(status.status)
           << (status.validationError ? (" : " + *status.validationError) : std::string{}));

    auto produced = fixture->service.lastExecutedHeader();
    BOOST_REQUIRE_MESSAGE(produced, id << ": newPayload returned VALID without an executed header "
                                          "(execution/commit was skipped)");
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, goldenBlockHash);
    assertExecutionCommitments(id, produced, goldenHeader);
}

void runInvalidFieldParity(std::string const& vectorId, std::string const& corruptField)
{
    auto sample = w6test::loadVectorSample(vectorId);
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    registerVerifiedBlock(fixture->multiLayerStorage, goldenHeader->parentInfo().blockHash, 0);

    auto params = w6test::makeParamsJson(sample);
    if (corruptField == "stateRoot")
    {
        params[0u]["stateRoot"] =
            "0x1111111111111111111111111111111111111111111111111111111111111111";
    }
    else if (corruptField == "receiptsRoot")
    {
        params[0u]["receiptsRoot"] =
            "0x2222222222222222222222222222222222222222222222222222222222222222";
    }
    else if (corruptField == "gasUsed")
    {
        params[0u]["gasUsed"] = "0x1";
    }
    else
    {
        BOOST_FAIL("unknown corruptField");
    }

    // Recompute blockHash from the corrupted payload so Invalid is not InvalidBlockHash.
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto header = productionHeaderOf(fixture->blockFactory, request);
    params[0u]["blockHash"] =
        w6test::hexPrefixedH256(bcos::protocol::EthBlockHeader::computeHash(*header));
    request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, request.executionPayload.parentHash);
}

}  // namespace op_engine_exec_parity

#define SKIP_IF_NO_T8N_CORPUS()                                    \
    do                                                             \
    {                                                              \
        if (!w6test::t8nCorpusAvailable())                         \
        {                                                          \
            BOOST_TEST_MESSAGE("skipping S6: t8n corpus missing"); \
            return;                                                \
        }                                                          \
    } while (0)

BOOST_AUTO_TEST_SUITE(OpEngineServiceExecParityTest)

using namespace op_engine_exec_parity;

BOOST_AUTO_TEST_CASE(s6_skips_when_t8n_corpus_missing)
{
    // Matrix: T06 — skip must return from the test case, not from a helper.
    if (w6test::t8nCorpusAvailable())
    {
        BOOST_TEST_MESSAGE("t8n corpus present; skip-path not exercised");
        return;
    }
    BOOST_CHECK(!w6test::t8nCorpusAvailable());
}

BOOST_AUTO_TEST_CASE(load_json_file_reports_cannot_open)
{
    BOOST_CHECK_EXCEPTION(w6test::loadJsonFile("/no/such/t8n/file.json"), std::runtime_error,
        [](std::runtime_error const& e) {
            return std::string_view(e.what()).find("cannot open") != std::string_view::npos;
        });
}

BOOST_AUTO_TEST_CASE(op_e2e_isthmus_deposit_only_matches_golden)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runGoldenVector("isthmus_deposit_only");
}

BOOST_AUTO_TEST_CASE(op_e2e_jovian_deposit_only_matches_golden)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runGoldenVector("jovian_deposit_only");
}

BOOST_AUTO_TEST_CASE(op_e2e_jovian_transfer_multi_matches_golden)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runGoldenVector("jovian_transfer_multi");
}

BOOST_AUTO_TEST_CASE(op_invalid_state_root_returns_invalid)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runInvalidFieldParity("jovian_deposit_only", "stateRoot");
}

BOOST_AUTO_TEST_CASE(op_invalid_receipts_root_returns_invalid)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runInvalidFieldParity("jovian_deposit_only", "receiptsRoot");
}

BOOST_AUTO_TEST_CASE(op_invalid_gas_used_returns_invalid)
{
    // Matrix: S6
    SKIP_IF_NO_T8N_CORPUS();
    runInvalidFieldParity("jovian_deposit_only", "gasUsed");
}

BOOST_AUTO_TEST_CASE(s6_request_rebuild_matches_golden_without_calling_newpayload)
{
    // Finding O discriminator: rebuildOpEthHeader copies stateRoot/receiptsRoot/gasUsed
    // from the request JSON. That comparison is green even when newPayload never runs.
    // runGoldenVector must therefore read lastExecutedHeader(), not this rebuild.
    SKIP_IF_NO_T8N_CORPUS();
    auto sample = w6test::loadVectorSample("isthmus_deposit_only");
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(sample.jovian));
    const auto goldenHeader = w6test::decodeGoldenHeader(sample);
    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto rebuilt = productionHeaderOf(fixture->blockFactory, request);
    const auto goldenBlockHash = bcos::h256(std::string(sample.golden["blockHash"].asString()));
    assertRebuiltAnnouncement(
        "isthmus_deposit_only/request-rebuild", rebuilt, goldenHeader, goldenBlockHash);
    BOOST_CHECK(!fixture->service.lastExecutedHeader());
}

BOOST_AUTO_TEST_SUITE_END()
