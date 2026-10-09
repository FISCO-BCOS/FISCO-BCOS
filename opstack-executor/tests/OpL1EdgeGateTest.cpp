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
 * @file OpL1EdgeGateTest.cpp
 * @brief L1-edge gating of OP payloads: DA footprint, gas limits and the deposit-first shape. */

// opstack-executor/tests/OpL1EdgeGateTest.cpp
// L1 edge gate: B-5b Jovian DA-footprint rejection + D-4 validate-snapshot contract.
//
// B-5b: engine_newPayloadV4 whose blobGasUsed (= the Jovian DA footprint header slot) exceeds
//       gasLimit must be rejected INVALID in Step 2 static validation, BEFORE parentKnown /
//       execution -- so no seedPreState / registerVerifiedBlock is needed. The rejection is
//       fork-gated on Jovian, so the fixture uses jovian fork timestamps.
// D-4:  the validate-time OpFeeParams freeze (OpPolicy ctor) is what the transition
//       consumes — the snapshot is the only fee source after validate (no storage re-read)
//       slots (which may have moved on to a different fee F' by transition time).
//
// B-5b reuses the W6 harness fixture pattern (OpNewPayloadRpcE2eTest.cpp). That fixture lives in
// an anonymous namespace and is TU-local, so it is copied here (only the parts this file needs).
// D-4 follows the OpTransitionTest test::TestState pattern (no harness / MLS needed).
#include "support/GoldenSample.h"
#include "support/OpEngineE2eFixture.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <opstack-executor/OpRollupCost.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpEthBlockExecute.h>
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
#include <json/json.h>
#include <boost/test/unit_test.hpp>

// D-4: TestState pattern (following OpTransitionTest.cpp)
#include <opstack-executor/OpFeeParams.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <evmone/evmone.h>

#include <algorithm>
#include <memory>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

// D-4's opstack names on the post-cutover layer (the bcos-evm testutil namespace is gone).
using namespace bcos::executor_v1::opstack;
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
// Overriding gasLimit changes the header preimage; the blockHash recomputation below
// (rebuildOpEthHeader over the mutated request.executionPayload) keeps the payload
// self-consistent and matches the sibling cells' convention. NOTE the gate ORDER: the
// Step-2 shape validator (which answers the DA-footprint error) runs BEFORE the
// blockHash comparison (OpEngineService.inl: validateOpNewPayloadRequest precedes
// canonicalBlockHash), so a stale hash cannot mask the DA gate — the recompute is
// defensive consistency, not an ordering requirement.
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
    auto fixture = std::make_unique<OpE2eFixture>(/*jovian=*/true);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    // Post-cutover the DA-equality gate lives in the six-way verify at EXECUTION (the old
    // engine's F-A2 static step was pre-parent). Without a registered parent the engine
    // answers SYNCING and the gate never runs — seed + register so the import executes.
    opstack_test::seedPreState(fixture->multiLayerStorage, sample.vector["pre"]);
    auto const goldenHeader = w6test::decodeGoldenHeader(sample);
    registerParentForNewPayload(fixture->multiLayerStorage, fixture->blockFactory, sample.vector,
        /*jovian=*/true, goldenHeader->parentInfo().blockHash);
    // rebuildOpEthHeader is fully payload-driven (it stamps every fork field from the
    // payload itself), so the recomputed hash matches the engine's own reconstruction
    // byte for byte — the same call runOpNewPayloadSteps makes (OpEngineService.inl).
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(rawEnvelopesFromPayload(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        fixture->blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        request.parentBeaconBlockRoot.value_or(bcos::h256{}));
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(fixture->service.newPayload(request, 4));
    std::fprintf(stderr, "PROBE status=%d reason=%s\n", static_cast<int>(status.status),
        status.validationError ? status.validationError->c_str() : "<none>");
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
/// Block-level DA footprint Σ over the raw envelopes (op-geth CalcDAFootprint): the
/// scalar comes from the L1-attributes deposit's Jovian tail
/// (opEthJovianDaFootprintGasScalar), every non-deposit envelope contributes
/// estimatedDaSizeFromFlz(flzCompressLen(env)) × scalar — the same OpRollupCost.h
/// primitives the per-tx accumulation (OpEthReceipt.h) uses. The bcos-evm-era free
/// helper (daFootprintOfEnvelopes) retired with the cutover; the recomputation stays
/// deliberately independent here — the guard under test recomputes on its own, so a
/// shared helper would make the equality check tautological.
std::optional<uint64_t> daFootprintOfEnvelopesLocal(
    const std::vector<bcos::bytesConstRef>& envelopes)
{
    if (envelopes.empty() || envelopes.front().empty() ||
        envelopes.front()[0] != OP_DEPOSIT_TX_TYPE)
    {
        return std::nullopt;
    }
    auto const dep = decodeOpDepositEnvelope(envelopes.front());
    auto const attr = std::span<const uint8_t>{dep.data.data(), dep.data.size()};
    auto const scalar = opEthJovianDaFootprintGasScalar(attr);
    if (!scalar.has_value())
    {
        return std::nullopt;
    }
    if (*scalar == 0)
    {
        return uint64_t{0};  // Isthmus-length attributes: no DA footprint yet
    }
    uint64_t sum = 0;
    for (auto const& env : envelopes)
    {
        if (!env.empty() && env[0] == OP_DEPOSIT_TX_TYPE)
        {
            continue;
        }
        auto const term =
            estimatedDaSizeFromFlz(flzCompressLen(evmc::bytes_view{env.data(), env.size()})) *
            static_cast<uint64_t>(*scalar);
        // op-geth accumulates into a Go uint64 (wraps); wrapping would let a crafted
        // block clear the equality gate, so fail closed instead.
        if (sum > std::numeric_limits<uint64_t>::max() - term)
        {
            return std::nullopt;
        }
        sum += term;
    }
    return sum;
}

uint64_t localDaFootprintOfGoldenVector()
{
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
    auto const sum = daFootprintOfEnvelopesLocal(envelopes);
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

    auto fixture = std::make_unique<OpE2eFixture>(/*jovian=*/true);
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
    // jovian_da_mix), so the varying knob is the block gasLimit: below Σ must be INVALID.
    // The VALID cells (== and above Σ) are NOT probed here: jovian_da_mix's deposit
    // declares gas=1M, above the footprint — at gasLimit==Σ the deposit no longer fits and
    // the deposit-fit gate rejects first ("op deposit: block gas limit reached"), so the
    // DA-cap's equality edge is unreachable on this vector. The cap's INVALID wording is
    // pinned by DAFootprintExceedsGasLimitRejected (blobGasUsed mutated above gasLimit,
    // static gate, no execution needed); the equality-mismatch INVALID is pinned by
    // JovianDaFootprintMustEqualLocalRecomputation (the six-way verify at execution).
    auto const local = localDaFootprintOfGoldenVector();
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

// D-4 (post-cutover form): the validate/transition snapshot discipline is enforced
// structurally — OpPolicy's ctor takes the caller-owned OpTxSnapshot, additionalMaxCost
// writes the fee into it, and settleFees/buildReceipt read it (see the read sites at
// OpExecutionPolicy.h:295-296). This case pins the observable half: the snapshot fields
// the transition will consume are exactly the validate-time fee F. The transition-side
// (settleFees) behaviour is covered by the execution suites; this case's old
// storage-mutation half was retired with the pre-cutover driver.
BOOST_AUTO_TEST_CASE(SnapshotFreezeFillsOpTxSnapshot)
{
    using bcos::executor_v1::opstack::OpFeeParams;
    using bcos::executor_v1::opstack::OpPolicy;
    using bcos::executor_v1::opstack::OpTxSnapshot;

    // Jovian window: exercises the has_da_footprint branch of the fee snapshot too.
    const auto spec = OP_JOVIAN_SPEC;
    // l1_base_fee = 1 gwei with base_fee_scalar = 1100 keeps the Fjord calldata term
    // non-zero (l1_cost > 0 is asserted below; the D-4 original pinned both).
    const OpFeeParams F{.l1_base_fee = intx::uint256{1000000000},
        .base_fee_scalar = 1100,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = intx::uint256{0},
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0,
        .da_footprint_gas_scalar = 40};

    eth::EthBlockInfo block{};
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.timestamp = 1'000;  // jovian window under scheduleFor(true)'s jovian@1s

    // A legacy-type envelope (120 bytes): flz non-zero -> l1_cost non-zero, so the case
    // is non-vacuous.
    std::vector<uint8_t> env(120, 0x11);

    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    auto hashImpl = cryptoSuite->hashImpl();
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto tx = txFactory->createTransaction(2, "0x00000000000000000000000000000000000000bb",
        bcos::bytes{0x0a}, "0x1", 100000, "0x2105", "1", 7, /*_abi=*/{}, /*_value=*/{},
        /*_gasPrice=*/"0x3e8", /*_gasLimit=*/10);  // 0x3e8 = 1000: gasPrice is a hex string

    OpTxSnapshot snapshot;
    eth::EthCallParams callParams{};
    OpPolicy policy(spec, F, block, evmc::bytes_view{env.data(), env.size()}, *tx, callParams,
        snapshot);

    const auto maxCost = policy.additionalMaxCost(
        *tx, block, EVMC_SHANGHAI, callParams);
    BOOST_CHECK(maxCost > 0);

    // The frozen-fee invariant: everything the transition consumes came from F.
    BOOST_CHECK(snapshot.fee.l1_base_fee == F.l1_base_fee);
    BOOST_CHECK(snapshot.fee.base_fee_scalar == F.base_fee_scalar);
    BOOST_CHECK(snapshot.fee.blob_base_fee_scalar == F.blob_base_fee_scalar);
    BOOST_CHECK(snapshot.fee.blob_base_fee == F.blob_base_fee);
    BOOST_CHECK(snapshot.fee.da_footprint_gas_scalar == F.da_footprint_gas_scalar);
    BOOST_CHECK(snapshot.l1_cost > intx::uint256{0});
    // Jovian: the DA footprint fields are snapshot-carried as well.
    BOOST_CHECK(snapshot.has_da_footprint);
}

BOOST_AUTO_TEST_SUITE_END()
