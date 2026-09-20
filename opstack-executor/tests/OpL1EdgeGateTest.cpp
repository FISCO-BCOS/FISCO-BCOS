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
#include "support/OpEngineE2eFixture.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpCommon.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
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
#include <json/json.h>
#include <boost/test/unit_test.hpp>

// D-4: TestState pattern (following OpTransitionTest.cpp)
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/test/opstack/OpPredeploysSeed.h>
#include <bcos-evm/test/opstack/OpTestReceiptFactory.h>
#include <evmone/evmone.h>
#include <test/utils/test_state.hpp>

#include <algorithm>
#include <memory>
#include <optional>
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
using namespace opstack_e2e;

// Minimal Step-2 static-validation helper extracted from DAFootprintExceedsGasLimitRejected
// (B-5b, below): build a V4 newPayload request from the jovian_da_mix golden vector with
// the header's blobGasUsed (the Jovian DA-footprint slot) overridden to `remote` and, when
// supplied, the payload's gasLimit overridden too; run newPayload, and map the
// PayloadValidationStatus to its string. See the WI-26 cells for why the blockHash is
// recomputed and why "not Invalid" == "the static gate accepted".
//
// GasLimit is a separate override because the F-A2 equality gate ties the header field to the
// LOCAL Σ: the boundary cell must keep remote == local and move gasLimit, not remote.
// Overriding gasLimit changes the header preimage, so the blockHash recomputation below
// (rebuildOpEthHeader over the mutated request.executionPayload) is mandatory — otherwise the
// engine's hash gate would reject before the DA checks run.
std::string statusForDaFootprintRemoteAndGasLimit(uint64_t remote, std::optional<uint64_t> gasLimit)
{
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    auto params = w6test::makeParamsJson(sample);
    // Warning: quantityOf, not u256::str(16) — decimal digits are not hex
    // (GoldenSample.h:124-130).
    params[0u]["blobGasUsed"] = w6test::quantityOf(bcos::u256(remote));
    if (gasLimit.has_value())
    {
        params[0u]["gasLimit"] = w6test::quantityOf(bcos::u256(*gasLimit));
    }
    auto fixture = std::make_unique<OpE2eFixture>(forkFlagsFor(true));
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    // parseNewPayloadRequest converts seconds to internal millis (EngineHelper.cpp:334);
    // the fork table is keyed on Unix seconds (OpSchedulerSeam.h forkIdAt) — the same
    // resolution runOpNewPayloadSteps does (OpEngineService.inl:845), so the recomputed
    // hash matches the engine's own reconstruction byte for byte.
    auto const tsSec = bcos::engine::unixSecondsFromInternalMillis(
        static_cast<uint64_t>(request.executionPayload.timestamp));
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(rawEnvelopesFromPayload(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        fixture->blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        request.parentBeaconBlockRoot, fixture->scheduler.forkIdAt(tsSec));
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    // PayloadValidationStatus is an enum class without operator<<; anything not Invalid
    // passed the Step-2 static gate.
    return status.status == bcos::engine::PayloadValidationStatus::Invalid ? "INVALID" : "VALID";
}

std::string statusForDaFootprintRemote(uint64_t remote)
{
    return statusForDaFootprintRemoteAndGasLimit(remote, std::nullopt);
}

/// Local Σ of the Jovian DA footprint for the jovian_da_mix fixture, via the production
/// accumulator bcos::evm::opstack::daFootprintOfEnvelopes (OpBlockExecute.cpp) — the same
/// function the engine's F-A2 equality gate calls. It is NOT read back out of the golden
/// header; the caller's `Σ == golden header blobGasUsed` assertion keeps this non-tautological
/// (opstack-executor/tests/t8n/golden/engine/jovian_da_mix.golden.json is sealed by op-geth's
/// t8n), so it checks the accumulator against an external oracle rather than against itself.
uint64_t localDaFootprintOfGoldenVector()
{
    namespace op = bcos::evm::opstack;
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    std::vector<bcos::bytes> owned;
    for (auto const& raw : sample.golden["rawTransactions"])
    {
        owned.emplace_back(bcos::fromHex(raw.asString()));
    }
    std::vector<bcos::bytesConstRef> envelopes;
    envelopes.reserve(owned.size());
    for (auto const& env : owned)
    {
        envelopes.emplace_back(env.data(), env.size());
    }
    auto const sum = op::daFootprintOfEnvelopes(envelopes);
    BOOST_REQUIRE(sum.has_value());
    return *sum;
}

/// Equality-gap shape (F-A2) for JovianDaFootprintMustEqualLocalRecomputation: `local` is the Σ
/// the txs really sum to, `remote` what the header field claims. `local` comes from
/// localDaFootprintOfGoldenVector(), which the guard recomputes from the tx set, and the guard
/// separately requires the golden header's blobGasUsed to match it. Both halves are
/// non-tautological: dropping either leaves one side assumed.
std::string statusForDaFootprintWithRemote(uint64_t local, uint64_t remote)
{
    BOOST_REQUIRE_EQUAL(localDaFootprintOfGoldenVector(), local);
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    BOOST_REQUIRE_EQUAL(
        static_cast<uint64_t>(*w6test::decodeGoldenHeader(sample)->blobGasUsed()), local);
    return statusForDaFootprintRemote(remote);
}

/// Boundary shape: remote == local Σ (so the F-A2 equality gate passes) and the block gasLimit
/// is the varying knob, with the blockHash recomputed over the mutated header. `gasLimit >= Σ`
/// must be VALID (op-geth checks `daFootprint > gasLimit`, block_validator.go:131), `gasLimit < Σ`
/// INVALID.
std::string statusForDaFootprintWithGasLimit(uint64_t local, uint64_t gasLimit)
{
    BOOST_REQUIRE_EQUAL(localDaFootprintOfGoldenVector(), local);
    auto sample = w6test::loadVectorSample("jovian_da_mix");
    BOOST_REQUIRE_EQUAL(
        static_cast<uint64_t>(*w6test::decodeGoldenHeader(sample)->blobGasUsed()), local);
    return statusForDaFootprintRemoteAndGasLimit(local, gasLimit);
}

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

// WI-26 boundary + equality cells. Both reuse the B-5b construction above (jovian_da_mix
// golden vector + OpE2eFixture at jovian timestamps + V4 newPayload), extracted into one
// minimal helper below — NOT a copy of the W6 fixture. The payload's TRANSACTIONS stay the
// golden set (so the local Σ is fixed — op-geth's t8n sealed it into the golden header's
// blobGasUsed, and localDaFootprintOfGoldenVector recomputes that Σ via the production
// accumulator rather than reading it back, with the F-A2 guard cross-checking the golden
// header). The blockHash is recomputed over any overridden header field with the engine's own
// recipe (computeTxRoot + rebuildOpEthHeader + EthBlockHeader::computeHash,
// OpEngineService.inl:920-924) at the same fork the engine resolves for the payload timestamp,
// so the hash gate never masks the gate under test. For an accepted payload Step 2 passes and
// the parent lookup answers SYNCING (golden parent unknown) — anything not Invalid means the
// Step-2 static gate accepted the field.
//
// The equality gate ties the header's blobGasUsed to the local Σ, so the boundary cell keeps
// remote == local and varies the block gasLimit instead. op-geth uses '>' on the local
// footprint (block_validator.go:131), so == gasLimit is VALID; spec's "below, like gasUsed"
// (jovian/exec-engine.md:125) is <= semantics. Pin all three sides so neither a '>=' nor a
// '<' regression can hide.
// clang-format off
BOOST_AUTO_TEST_CASE(JovianDaFootprintGasLimitBoundary, * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // The F-A2 equality gate fixes the header blobGasUsed at the local Σ (593,600 for
    // jovian_da_mix), so the varying knob is the block gasLimit: < Σ INVALID, == Σ VALID
    // (op-geth's '>' makes equality legal), > Σ VALID.
    auto const local = localDaFootprintOfGoldenVector();
    BOOST_CHECK(
        statusForDaFootprintWithGasLimit(/*local=*/local, /*gasLimit=*/local + 1) == "VALID");
    BOOST_CHECK(statusForDaFootprintWithGasLimit(/*local=*/local, /*gasLimit=*/local) ==
                "VALID");  // == is legal
    BOOST_CHECK(
        statusForDaFootprintWithGasLimit(/*local=*/local, /*gasLimit=*/local - 1) == "INVALID");
}

// op-geth rejects a header whose blobGasUsed disagrees with the locally recomputed
// footprint (block_validator.go:127 "invalid DA footprint in blobGasUsed field
// (remote: %d local: %d)"). The F-A2 equality gate now enforces that here too, so this cell is
// a plain green assertion; the expected_failures(1) decorator was removed in the same commit
// that landed the fix (a fixed implementation plus the decorator would be red the other way,
// "no errors but expected to fail").
// clang-format off
BOOST_AUTO_TEST_CASE(JovianDaFootprintMustEqualLocalRecomputation,
    * boost::unit_test::label("fork-jovian"))
// clang-format on
{
    // One payload whose local footprint is F, recomputed in-test via the production accumulator
    // (localDaFootprintOfGoldenVector); statusForDaFootprintWithRemote also requires op-geth's
    // t8n-sealed golden header blobGasUsed to equal F, so F is not read back out of the header.
    // The remote header field says F-1, and the engine must answer INVALID
    // (op-geth block_validator.go:127).
    auto const local = localDaFootprintOfGoldenVector();
    auto const status = statusForDaFootprintWithRemote(/*local=*/local, /*remote=*/local - 1);
    BOOST_CHECK_EQUAL(status, "INVALID");
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
        .overhead = 0_u256,
        .bedrock_scalar = 0_u256,
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
