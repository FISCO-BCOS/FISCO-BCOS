// bcos-evm/test/opstack/OpL1EdgeGateTest.cpp
// L1 edge gate: B-5b Jovian DA-footprint rejection + D-4 validate-snapshot contract.
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
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpSchedulerSeam.h>
// EngineHelper.h's parseNewPayloadRequest declaration references
// bcos::protocol::TransactionFactory&, but EngineHelper.h does not declare that type
// itself (production relies on bcos-rpc unity-build include order). A single-TU direct
// compile must include TransactionFactory.h first or the declaration fails
// (OpNewPayloadRpcE2eTest.cpp:20 pattern).
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

// D-4: TestState pattern (following OpTransitionTest.cpp)
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

// D-4's TestState/opstack names (following OpTransitionTest.cpp:16-20)
using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// ── storage fixture (copy of OpNewPayloadRpcE2eTest's anonymous-namespace fixture; an
//    anonymous namespace cannot cross TUs, so B-5b builds its own) ──
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

    // Poisoned-tx eviction hook (engine build loop): no-op in tests - the pool is never populated.
    void removeByHash(std::span<bcos::crypto::HashType const>)
    {}
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

bcos::evm::opstack::OpForkFlags forkFlagsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkFlags{.jovianActive = jovian};
}

using EngineOpScheduler = bcos::evm::engine::OpSchedulerSeam<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;

struct OpE2eFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    EngineOpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service;

    explicit OpE2eFixture(bcos::evm::opstack::OpForkFlags forkFlags)
      : scheduler(forkFlags),
        service(memPool, multiLayerStorage, executor, scheduler, blockFactory,
            /*ledger=*/nullptr, bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4,
            /*delegate=*/nullptr)
    {}
};

}  // namespace

BOOST_AUTO_TEST_SUITE(OpL1EdgeGateSuite)

// B-5b: under Jovian, blobGasUsed (the DA-footprint slot) > gasLimit -> Step 2 static
// validation INVALID + validationError contains "DA footprint". The DA check is gated on
// jovianActive (EngineServiceImpl.cpp:442), so the fixture must use jovian timestamps;
// Step 2 runs before parentKnown/execution, so no seedPreState / registerVerifiedBlock needed.
BOOST_AUTO_TEST_CASE(DAFootprintExceedsGasLimitRejected)
{
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    auto params = w6test::makeParamsJson(sample);
    // Read the golden header's gasLimit; override blobGasUsed = gasLimit+1
    const auto gasLimit = w6test::decodeGoldenHeader(sample)->gasLimit();
    // Warning: do not use (gasLimit+1).str(16) — decimal digits are not hex
    // (GoldenSample.h:85-90 warns); quantityOf is correct.
    params[0u]["blobGasUsed"] = w6test::quantityOf(gasLimit + 1);

    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(true));
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // PayloadValidationStatus is an enum class without operator<<; must compare via
    // static_cast<int>.
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    // Warning: the real string includes (blobGasUsed); check "DA footprint" (not "DA footprint
    // exceeds").
    BOOST_CHECK(status.validationError->find("DA footprint") != std::string::npos);
}

// D-4: opValidate injects fee F -> props.fee frozen snapshot -> mutate L1Block slot1 to a
// markedly different F' -> opTransition still prices/receipts from props (F), not by
// re-reading storage (F'). Signature follows OpTransitionTest.cpp's
// OperatorFeeConservesWhenCfgDisagreesWithProps (:337-397); the difference is that that
// case mutates cfg, this one mutates a storage slot.
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
    // Warning: seedOpPredeploys returns void; cannot auto ts = seedOpPredeploys(...)
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

    // Inject fee F: l1_base_fee = 1 gwei (non-zero), base_fee_scalar 1100 -> props.l1_cost
    // non-zero.
    OpFeeParams F{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 1100,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(120, 0x11);  // non-empty envelope: flz non-zero -> l1_cost non-zero

    // opValidate returns std::variant<OpTxProperties, std::error_code>; must
    // std::get<OpTxProperties>.
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), F, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    BOOST_REQUIRE_MESSAGE(props.l1_cost > intx::uint256{0}, "test is vacuous unless l1_cost > 0");

    // OpFeeParams has no operator== (non-defaulted aggregate, not generated in C++20) -> compare
    // the snapshot to the injected values field-by-field.
    BOOST_CHECK(props.fee.l1_base_fee == F.l1_base_fee);
    BOOST_CHECK(props.fee.base_fee_scalar == F.base_fee_scalar);
    BOOST_CHECK(props.fee.blob_base_fee_scalar == F.blob_base_fee_scalar);
    BOOST_CHECK(props.fee.blob_base_fee == F.blob_base_fee);
    BOOST_CHECK(props.fee.operator_fee_scalar == F.operator_fee_scalar);
    BOOST_CHECK(props.fee.operator_fee_constant == F.operator_fee_constant);
    BOOST_CHECK(props.fee.da_footprint_gas_scalar == F.da_footprint_gas_scalar);

    // Mutate slot1 (the l1_base_fee slot) to a markedly different F': 7 vs 1e9. If
    // opTransition re-read storage, l1_cost would shrink to ~7/1e9 and the receipt l1_fee
    // assertion would go red (slot3 packed mutation is fiddly; a single slot1 change suffices).
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

    // opTransition consumes props (does not re-read storage); signature per OpTransition.h:134-139.
    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);

    // Receipt opStackMeta l1_fee == value computed from F (props.l1_cost), not F'
    // (following OpTransitionTest.cpp:146-149).
    const auto& meta = txR->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_fee, bcosU256FromIntx(props.l1_cost));
    // Strengthen: l1_gas_price likewise comes from the F snapshot, not re-read storage
    // (deriveOpReceiptMeta OpTransition.cpp:214).
    BOOST_REQUIRE(meta->l1_gas_price.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_gas_price, bcosU256FromIntx(F.l1_base_fee));
}

BOOST_AUTO_TEST_SUITE_END()
