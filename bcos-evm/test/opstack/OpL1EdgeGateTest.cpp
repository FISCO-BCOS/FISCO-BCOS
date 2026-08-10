// bcos-evm/test/opstack/OpL1EdgeGateTest.cpp
// W5 Task 4 (L1 edge gate): B-5b Jovian DA-footprint rejection + D-4 validate-snapshot contract.
//
// B-5b: engine_newPayloadV4 whose blobGasUsed (= the Jovian DA footprint header slot) exceeds
//       gasLimit must be rejected INVALID in Step 2 static validation, BEFORE parentKnown /
//       execution -- so no seedPreState / registerVerifiedBlock is needed. The rejection is
//       fork-gated on Jovian, so the fixture uses jovian fork timestamps.
// D-4:  opValidate freezes the OpFeeParams into OpTxProperties.fee (the snapshot). opTransition
//       must price and receipt the tx from that snapshot, NOT by re-reading the L1Block storage
//       slots (which may have moved on to a different fee F' by transition time).
//
// B-5b reuses the W6 harness fixture pattern (OpNewPayloadRpcE2eTest.cpp). That fixture lives in
// an anonymous namespace and is TU-local, so it is copied here (only the parts this file needs).
// D-4 follows the OpTransitionTest test::TestState pattern (no harness / MLS needed).
#include "support/GoldenSample.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
// EngineHelper.h 的 parseNewPayloadRequest 声明引用 bcos::protocol::TransactionFactory&，但
// EngineHelper.h 自身不声明该类型（生产靠 bcos-rpc unity-build 的 include 顺序）。单 TU 直编
// 必须先把 TransactionFactory.h 引入，否则声明处即报错（OpNewPayloadRpcE2eTest.cpp:20 模式）。
#include <bcos-framework/protocol/TransactionFactory.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/EngineServiceImpl.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

// D-4: TestState 模式（照 OpTransitionTest.cpp）
#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <test/utils/test_state.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

// D-4 的 TestState/opstack 命名（照 OpTransitionTest.cpp:16-20）
using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// ── storage fixture（OpNewPayloadRpcE2eTest 匿名 namespace fixture 的副本：anonymous
//    namespace 不可跨 TU，B-5b 需自建）──
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
};
struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
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

constexpr uint64_t kChainId = 0x2105;

bcos::evm::opstack::OpForkTimestamps forkTimestampsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0,
        .jovianTime = jovian ? 0 : std::numeric_limits<uint64_t>::max(),
    };
}

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;

struct OpE2eFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    OpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps),
        service(memPool, multiLayerStorage, executor, scheduler, blockFactory,
            /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4)
    {}
};

}  // namespace

BOOST_AUTO_TEST_SUITE(OpL1EdgeGateSuite)

// B-5b：Jovian 下 blobGasUsed（DA footprint 槽）> gasLimit → Step 2 静态校验 INVALID +
// validationError 含 "DA footprint"。DA 检查带 jovianActive 门控（EngineServiceImpl.cpp:442），
// 故 fixture 必须用 jovian 时间戳；Step 2 早于 parentKnown/执行，故不需 seedPreState /
// registerVerifiedBlock。
BOOST_AUTO_TEST_CASE(DAFootprintExceedsGasLimitRejected)
{
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    auto params = w6test::makeParamsJson(sample);
    // 读 golden header 的 gasLimit，override blobGasUsed = gasLimit+1
    const auto gasLimit = w6test::decodeGoldenHeader(sample)->gasLimit();
    // ⚠️ 勿用 (gasLimit+1).str(16)——十进制位数非 hex（GoldenSample.h:85-90 已警示）；quantityOf
    // 正确。
    params[0u]["blobGasUsed"] = w6test::quantityOf(gasLimit + 1);

    auto fixture = std::make_unique<OpE2eFixture>(forkTimestampsFor(true));
    auto request = bcos::rpc::parseNewPayloadRequest(
        params, *fixture->blockFactory->transactionFactory(), bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // PayloadValidationStatus 是 enum class，无 operator<<；须 static_cast<int> 比较。
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    // ⚠️ 实际串含 (blobGasUsed)：检查 "DA footprint"（勿 "DA footprint exceeds"）。
    BOOST_CHECK(status.validationError->find("DA footprint") != std::string::npos);
}

// D-4：opValidate 注入 fee F → props.fee 冻结快照 → 改 L1Block slot1 为显著不同的 F' →
// opTransition 仍按 props（F）计价/开回执，而非重读存储（F'）。
// 签名照 OpTransitionTest.cpp 的 OperatorFeeConservesWhenCfgDisagreesWithProps（:337-397），
// 差异在：该条改 cfg，本条改 storage slot。
BOOST_AUTO_TEST_CASE(TransitionUsesValidateSnapshot)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[dest] = {};
    // ⚠️ seedOpPredeploys 返回 void，不能 auto ts = seedOpPredeploys(...)
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    // 注入 fee F：l1_base_fee 取 1 gwei（非零），base_fee_scalar 1100 → props.l1_cost 非零。
    OpFeeParams F{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 1100,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(120, 0x11);  // 非空 envelope：flz 非零 → l1_cost 非零

    // opValidate 返回 std::variant<OpTxProperties, std::error_code>，须 std::get<OpTxProperties>。
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), F, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    BOOST_REQUIRE_MESSAGE(props.l1_cost > intx::uint256{0}, "test is vacuous unless l1_cost > 0");

    // OpFeeParams 无 operator==（非 defaulted 聚合 C++20 不生成）→ 逐字段比较快照与注入值。
    BOOST_CHECK(props.fee.l1_base_fee == F.l1_base_fee);
    BOOST_CHECK(props.fee.base_fee_scalar == F.base_fee_scalar);
    BOOST_CHECK(props.fee.blob_base_fee_scalar == F.blob_base_fee_scalar);
    BOOST_CHECK(props.fee.blob_base_fee == F.blob_base_fee);
    BOOST_CHECK(props.fee.operator_fee_scalar == F.operator_fee_scalar);
    BOOST_CHECK(props.fee.operator_fee_constant == F.operator_fee_constant);
    BOOST_CHECK(props.fee.da_footprint_gas_scalar == F.da_footprint_gas_scalar);

    // 改 slot1（l1_base_fee 槽）为显著不同的 F'：7 vs 1e9。若 opTransition 重读存储，
    // l1_cost 会缩到 ~7/1e9，回执 l1_fee 断言即红（slot3 packed 改法繁琐，slot1 单改够）。
    auto key = [](uint8_t s) {
        evmc::bytes32 k{};
        k.bytes[31] = s;
        return k;
    };
    auto low8 = [](uint64_t v) {
        evmc::bytes32 w{};
        for (int i = 0; i < 8; ++i)
            w.bytes[31 - i] = static_cast<uint8_t>(v >> (8 * i));
        return w;
    };
    ts[OP_L1_BLOCK].storage[key(1)] = low8(7);  // F'.l1_base_fee = 7

    // opTransition 消费 props（不重读存储），签名照 OpTransition.h:134-139。
    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);

    // 回执 opStackMeta l1_fee == 按 F 计算值（props.l1_cost），而非按 F'（照
    // OpTransitionTest.cpp:146-149）。
    const auto& meta = txR->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_fee, bcosU256FromIntx(props.l1_cost));
    // 强化：l1_gas_price 同样来自快照 F 而非重读存储（deriveOpReceiptMeta OpTransition.cpp:214）。
    BOOST_REQUIRE(meta->l1_gas_price.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_gas_price, bcosU256FromIntx(F.l1_base_fee));
}

BOOST_AUTO_TEST_SUITE_END()
