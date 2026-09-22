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
 * @file OpHeaderValidatorTest.cpp
 * @brief Unit tests for the OP Stack header validator (Bedrock/Canyon baseFee constants,
 *        Holocene/Jovian extraData gating, fork-gated field presence, OP gas-limit
 *        jump exemption, op-geth timestamp rule).
 * @date 2026/9/21
 */
#include <bcos-devp2p/sync/OpHeaderValidator.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p::sync;

namespace
{
constexpr int64_t kParentTs = 1700000000;
constexpr int64_t kChildTs = kParentTs + 2;  // 2-second OP block cadence
// A fork time far above every test timestamp: with isthmus_time SET to this the full
// Bedrock..Karst ladder is live (no Isthmus zero-start baseline), so schedules can
// express genuine pre-Isthmus forks.
constexpr uint64_t kFullLadderLive = 4102444800;  // 2100-01-01

bcos::bytes holoceneExtra(uint32_t denominator, uint32_t elasticity)
{
    bcos::bytes extra(9);
    extra[0] = 0x00;
    bcos::bytesRef denomRef(extra.data() + 1, 4);
    bcos::toBigEndian(denominator, denomRef);
    bcos::bytesRef elastRef(extra.data() + 5, 4);
    bcos::toBigEndian(elasticity, elastRef);
    return extra;
}

bcos::bytes jovianExtra(uint32_t denominator, uint32_t elasticity, uint64_t minBaseFee)
{
    bcos::bytes extra(17);
    extra[0] = 0x01;
    bcos::bytesRef denomRef(extra.data() + 1, 4);
    bcos::toBigEndian(denominator, denomRef);
    bcos::bytesRef elastRef(extra.data() + 5, 4);
    bcos::toBigEndian(elasticity, elastRef);
    bcos::bytesRef minRef(extra.data() + 9, 8);
    bcos::toBigEndian(minBaseFee, minRef);
    return extra;
}

struct OpPair
{
    bcos::protocol::EthBlockHeaderData parent;
    bcos::protocol::EthBlockHeaderData child;
    OpChainConfig config;
};

// Set every fork field a header's own fork position requires, resolving the fork
// through the same single ladder parser the validator uses (resolveOpFork on
// _header.timestamp), so tests can flip a single fork time without tripping unrelated
// presence checks.
void stampForkFields(bcos::protocol::EthBlockHeaderData& h, OpChainConfig const& config)
{
    OpFork const fork =
        bcos::ledger::resolveOpFork(config.forkSchedule, static_cast<uint64_t>(h.timestamp));
    bool canyon = fork >= OpFork::Canyon;
    bool ecotone = fork >= OpFork::Ecotone;
    bool isthmus = fork >= OpFork::Isthmus;
    bool holocene = fork >= OpFork::Holocene;
    bool jovian = fork >= OpFork::Jovian;
    h.withdrawalsHash.reset();
    h.blobGasUsed.reset();
    h.excessBlobGas.reset();
    h.parentBeaconRoot.reset();
    h.requestsHash.reset();
    if (canyon)
    {
        // Isthmus+ carries the L2ToL1MessagePasser storage root; a non-empty constant
        // stands in (the value is intentionally unchecked post-Isthmus).
        h.withdrawalsHash =
            isthmus ? h256("0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20") :
                      c_opEmptyWithdrawalsHash;
    }
    if (ecotone)
    {
        h.blobGasUsed = u256(0);
        h.excessBlobGas = u256(0);
        h.parentBeaconRoot = h256{};
    }
    if (isthmus)
    {
        h.requestsHash = c_opEmptyRequestsHash;
    }
    h.extraData.clear();
    if (jovian)
    {
        h.extraData = jovianExtra(250, 6, 0);
    }
    else if (holocene)
    {
        h.extraData = holoceneExtra(250, 6);
    }
}

// A minimal valid Bedrock parent/child pair: no fork fields anywhere (pre-Canyon),
// empty extraData, the child's baseFee recomputed with the Bedrock 50/6 constants.
// isthmus_time is set to a far-future time so the FULL ladder is live — an unset
// isthmus_time means "Isthmus is the zero-start baseline" (resolveOpFork), which is
// not the pre-Canyon world this fixture models. Every other rung stays UINT64_MAX
// ("not scheduled", implied/skipped), so both timestamps resolve to Bedrock.
OpPair makeBedrockPair()
{
    OpPair p;
    p.config.chainId = 11155420;
    p.config.blockTimeSeconds = 2;
    p.config.forkSchedule.m_isthmusTime = kFullLadderLive;

    auto& parent = p.parent;
    parent.number = 100;
    parent.timestamp = kParentTs;
    parent.uncleHash = bcos::protocol::c_emptyOmmersHash;
    parent.gasLimit = 30000000;
    parent.gasUsed = 10000000;  // target = 30M/6 = 5M -> baseFee rises
    parent.baseFee = u256(1000000000);

    auto& child = p.child;
    child.number = 101;
    child.timestamp = kChildTs;
    child.uncleHash = bcos::protocol::c_emptyOmmersHash;
    child.gasLimit = 30000000;
    child.gasUsed = 4000000;
    child.coinbase = c_opSequencerFeeVault;
    child.baseFee = u256(1020000000);  // 1e9 + 1e9 * 5M / 5M / 50 (golden, hand-computed)
    return p;
}

void stampForkFields(OpPair& p)
{
    stampForkFields(p.child, p.config);
}

// The child's baseFee recomputed through the same entry point the validator uses —
// the golden-vector cases below pin the arithmetic itself, this helper only removes
// boilerplate from the presence/gating tests.
u256 expectedBaseFee(OpPair const& p)
{
    bool parentHolocene =
        bcos::ledger::resolveOpFork(p.config.forkSchedule, static_cast<uint64_t>(p.parent.timestamp)) >=
        OpFork::Holocene;
    bool parentJovian =
        bcos::ledger::resolveOpFork(p.config.forkSchedule, static_cast<uint64_t>(p.parent.timestamp)) >=
        OpFork::Jovian;
    uint64_t denominator =
        bcos::ledger::resolveOpFork(p.config.forkSchedule, static_cast<uint64_t>(p.child.timestamp)) >=
                OpFork::Canyon ?
            p.config.eip1559DenominatorCanyon :
            p.config.eip1559DenominatorBedrock;
    std::span<const bcos::byte> extra{p.parent.extraData.data(), p.parent.extraData.size()};
    return bcos::engine::calcOpBaseFeeFromFields(p.parent.gasLimit, p.parent.gasUsed,
        *p.parent.baseFee, p.parent.blobGasUsed, extra, parentHolocene, parentJovian, denominator,
        p.config.eip1559Elasticity);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpHeaderValidatorTest)

BOOST_AUTO_TEST_CASE(validBedrockPairPasses)
{
    auto p = makeBedrockPair();
    auto result = validateOpHeader(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);
    BOOST_CHECK(result.error.empty());
}

// Bedrock constants: denominator 50, elasticity 6 (op-geth params ChainConfig.Optimism).
BOOST_AUTO_TEST_CASE(bedrockBaseFeeGoldenVectors)
{
    auto p = makeBedrockPair();
    // gasUsed 10M vs target 5M: +1e9 * 5M/5M/50 = +20'000'000
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1020000000));
    // gasUsed 2.5M: -1e9 * 2.5M/5M/50 = -10'000'000
    p.parent.gasUsed = 2500000;
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(990000000));
    // gasUsed == target: steady
    p.parent.gasUsed = 5000000;
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1000000000));
}

// Canyon raises the denominator 50 -> 250, keyed on the CHILD's timestamp
// (op-geth BaseFeeChangeDenominator(header.Time)); the pre-Holocene parent carries no
// extraData parameters.
BOOST_AUTO_TEST_CASE(canyonDenominatorSwitch)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule.m_canyonTime = kChildTs;  // child is the first Canyon block
    stampForkFields(p);
    // deltaFee = 1e9 * 5M / 5M / 250 = 4'000'000
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1004000000));
    p.child.baseFee = u256(1004000000);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    // One block earlier the Bedrock denominator still applies.
    p.config.forkSchedule.m_canyonTime = kChildTs + 2;
    stampForkFields(p);
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1020000000));
    p.child.baseFee = u256(1020000000);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);
}

// Holocene: the EIP-1559 parameters come from the PARENT's 9-byte extraData
// (op-geth IsOptimismHolocene(parent.Time)), not from the chain config.
BOOST_AUTO_TEST_CASE(holoceneExtraDataParamsDriveBaseFee)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule.m_holoceneTime = kParentTs;  // parent already Holocene
    p.parent.extraData = holoceneExtra(100, 4);
    stampForkFields(p);
    // target = 30M/4 = 7.5M, delta = 2.5M, deltaFee = 1e9 * 2.5M/7.5M/100 = 3'333'333
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1003333333));
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    // A tampered parent extraData (zero elasticity) fails closed.
    p.parent.extraData = holoceneExtra(100, 0);
    auto result = validateOpHeader(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
}

// Holocene extraData shape gating on the CHILD (op-geth ValidateOptimismExtraData,
// keyed on the child's own timestamp).
BOOST_AUTO_TEST_CASE(holoceneExtraDataShapeIsEnforced)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule.m_holoceneTime = kChildTs;  // parent pre-Holocene, child Holocene
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);  // pre-Holocene parent -> config constants
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    auto bad = p;
    bad.child.extraData = holoceneExtra(250, 6);
    bad.child.extraData.resize(8);  // short
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad.child.extraData = holoceneExtra(250, 6);
    bad.child.extraData[0] = 0x01;  // wrong version byte
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad.child.extraData = holoceneExtra(0, 6);  // zero denominator
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad.child.extraData.clear();  // empty after Holocene
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
}

// Pre-Holocene extraData must be empty (op-geth ValidateOptimismExtraData; the genesis
// exemption never applies here because the genesis header has no parent).
BOOST_AUTO_TEST_CASE(preHoloceneExtraDataMustBeEmpty)
{
    auto p = makeBedrockPair();
    p.child.extraData = bcos::bytes(4, 0x42);
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);
    // The generic 32-byte bound applies on every fork.
    p.child.extraData = bcos::bytes(33, 0x42);
    p.config.forkSchedule.m_holoceneTime = kChildTs;
    stampForkFields(p);  // resets extraData to the Holocene shape
    p.child.extraData = bcos::bytes(33, 0x42);
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);
}

// Jovian: 17-byte extraData, minBaseFee floor, and DA-footprint metering
// (max(gasUsed, blobGasUsed)).
BOOST_AUTO_TEST_CASE(jovianMinBaseFeeFloorAndDaMetering)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule.m_canyonTime = kParentTs;  // Jovian implies the whole ladder below it
    p.config.forkSchedule.m_ecotoneTime = kParentTs;
    p.config.forkSchedule.m_holoceneTime = kParentTs;
    p.config.forkSchedule.m_jovianTime = kParentTs;                       // parent already Jovian
    p.parent.extraData = jovianExtra(100, 4, 2000000000);  // 2 gwei floor
    p.parent.blobGasUsed = u256(20000000);                 // DA footprint > gasUsed
    p.parent.gasUsed = 1000000;
    stampForkFields(p);
    // metered = 20M, target = 30M/4 = 7.5M, deltaFee = 1e9 * 12.5M/7.5M/100 = 16'666'666
    // -> 1'016'666'666, lifted to the 2 gwei floor.
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(2000000000));
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    // Without the floor the metered increase applies.
    p.parent.extraData = jovianExtra(100, 4, 0);
    stampForkFields(p);
    BOOST_CHECK_EQUAL(expectedBaseFee(p), u256(1016666666));

    // A Jovian parent without blobGasUsed fails closed (op-geth would panic on nil).
    p.parent.blobGasUsed.reset();
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    // Jovian child extraData must be exactly 17 bytes with version 0x01.
    p.parent.blobGasUsed = u256(20000000);
    auto bad = p;
    bad.child.baseFee = expectedBaseFee(p);
    bad.child.extraData = holoceneExtra(100, 4);  // 9 bytes under Jovian
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad.child.extraData = jovianExtra(100, 4, 0);
    bad.child.extraData[0] = 0x00;
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    // Jovian blobGasUsed is the DA footprint — an arbitrary value passes.
    bad = p;
    bad.child.baseFee = expectedBaseFee(p);
    bad.child.blobGasUsed = u256(123456789);
    BOOST_CHECK(validateOpHeader(bad.child, bad.parent, bad.config).valid);
}

// Canyon (== OP's Shanghai) gates withdrawalsHash both ways; before Isthmus the value is
// pinned to the empty withdrawals hash, from Isthmus it is the (header-unverifiable)
// L2ToL1MessagePasser storage root.
BOOST_AUTO_TEST_CASE(withdrawalsHashGating)
{
    auto p = makeBedrockPair();
    p.child.withdrawalsHash = c_opEmptyWithdrawalsHash;  // pre-Canyon: must be absent
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    p.config.forkSchedule.m_canyonTime = kParentTs;  // Canyon active for the child
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    auto bad = p;
    bad.child.withdrawalsHash.reset();  // missing under Canyon
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad.child.withdrawalsHash = h256{};  // wrong value pre-Isthmus
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);

    // Isthmus: the value is the message-passer storage root — unchecked, any value passes.
    p.config.forkSchedule.m_isthmusTime = kChildTs;
    stampForkFields(p);  // stamps a non-empty stand-in root
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);
}

// Ecotone (== OP's Cancun) gates blobGasUsed/excessBlobGas/parentBeaconRoot both ways;
// excessBlobGas is pinned to 0 and blobGasUsed to 0 until Jovian.
BOOST_AUTO_TEST_CASE(ecotoneBlobFieldsGating)
{
    auto p = makeBedrockPair();
    p.child.blobGasUsed = u256(0);  // pre-Ecotone: must be absent
    p.child.excessBlobGas = u256(0);
    p.child.parentBeaconRoot = h256{};
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    p.config.forkSchedule.m_canyonTime = kParentTs;
    p.config.forkSchedule.m_ecotoneTime = kChildTs;  // child is the first Ecotone block
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    auto bad = p;
    bad.child.parentBeaconRoot.reset();  // missing under Ecotone
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.excessBlobGas = u256(1);  // OP short-circuits the excess to 0
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.blobGasUsed = u256(kGasPerBlob);  // non-zero before Jovian
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
}

// Isthmus gates requestsHash both ways and pins it to sha256("") (no execution
// requests on OP chains).
BOOST_AUTO_TEST_CASE(isthmusRequestsHashGating)
{
    auto p = makeBedrockPair();
    p.child.requestsHash = c_opEmptyRequestsHash;  // pre-Isthmus: must be absent
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    p.config.forkSchedule.m_canyonTime = kParentTs;
    p.config.forkSchedule.m_ecotoneTime = kParentTs;
    p.config.forkSchedule.m_isthmusTime = kChildTs;
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    auto bad = p;
    bad.child.requestsHash.reset();  // missing under Isthmus
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.requestsHash = h256{};  // not sha256("")
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
}

// The L1 1/1024 adjacency bound does not apply on OP chains — the SystemConfig can
// retarget the gas limit between adjacent blocks (op-geth VerifyEIP1559Header skips
// misc.VerifyGaslimit for Optimism).
BOOST_AUTO_TEST_CASE(gasLimitJumpIsAllowed)
{
    auto p = makeBedrockPair();
    p.child.gasLimit = u256(60000000);  // double the parent's: a 50x-1/1024 violation on L1
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);

    p.child.gasLimit = u256(4999);  // the 5000 floor still applies
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    p.child.gasLimit = c_opMaxGasLimit + 1;  // params.MaxGasLimit (2^63-1) still applies
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);
}

// op-geth beacon.verifyHeader only requires header.Time > parent.Time — the 2-second
// cadence is an op-node derivation rule, not an EL header rule.
BOOST_AUTO_TEST_CASE(timestampRule)
{
    auto p = makeBedrockPair();
    p.child.timestamp = kParentTs;  // equal to the parent: invalid
    BOOST_CHECK(!validateOpHeader(p.child, p.parent, p.config).valid);

    p.child.timestamp = kParentTs + 7;  // non-2s interval: valid per op-geth
    BOOST_CHECK(validateOpHeader(p.child, p.parent, p.config).valid);
}

// Post-merge constants and the baseFee presence/exactness checks.
BOOST_AUTO_TEST_CASE(posConstantsAndBaseFee)
{
    auto p = makeBedrockPair();
    auto bad = p;
    bad.child.difficulty = u256(1);
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.nonce = h64(42);
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.uncleHash = h256{};
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.baseFee.reset();
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.baseFee = u256(1020000001);  // off by one
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.number = 103;  // continuity
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
    bad = p;
    bad.child.gasUsed = u256(40000000);  // exceeds gasLimit
    BOOST_CHECK(!validateOpHeader(bad.child, bad.parent, bad.config).valid);
}

// The whole op-sepolia fork ladder on one synthetic chain, crossing every boundary
// with the real superchain-registry timestamps.
BOOST_AUTO_TEST_CASE(opSepoliaForkLadder)
{
    OpChainConfig config;
    config.chainId = 11155420;
    config.blockTimeSeconds = 2;
    config.forkSchedule.m_regolithTime = 0;  // active from genesis
    config.forkSchedule.m_canyonTime = 1699981200;
    config.forkSchedule.m_deltaTime = 1703203200;
    config.forkSchedule.m_ecotoneTime = 1708534800;
    config.forkSchedule.m_fjordTime = 1716998400;
    config.forkSchedule.m_graniteTime = 1723478400;
    config.forkSchedule.m_holoceneTime = 1732633200;
    config.forkSchedule.m_isthmusTime = 1744905600;
    config.forkSchedule.m_jovianTime = 1763568001;
    config.forkSchedule.m_karstTime = 1781712001;

    struct Run
    {
        int64_t timestamp;
        bcos::bytes parentExtra;  // extraData the PARENT carries
    };
    // One boundary block per fork: Bedrock -> Canyon -> Ecotone -> Holocene ->
    // Isthmus -> Jovian.
    std::vector<Run> runs = {
        {1699981200 - 2, {}},                  // last Bedrock block (parent of Canyon 0)
        {1699981200, {}},                      // first Canyon
        {1708534800, {}},                      // first Ecotone
        {1732633200, {}},                      // first Holocene (parent still Granite)
        {1732633202, holoceneExtra(250, 6)},   // first child priced by parent extraData
        {1744905600, holoceneExtra(250, 6)},   // first Isthmus
        {1763568001, holoceneExtra(250, 6)},   // first Jovian (parent Holocene-shaped)
        {1763568003, jovianExtra(250, 6, 0)},  // first child priced by Jovian parent
    };

    bcos::protocol::EthBlockHeaderData parent;
    parent.number = 1;
    parent.timestamp = runs.front().timestamp - 2;
    parent.uncleHash = bcos::protocol::c_emptyOmmersHash;
    parent.gasLimit = 30000000;
    parent.gasUsed = 4000000;
    parent.baseFee = u256(100000000);
    parent.coinbase = c_opSequencerFeeVault;
    stampForkFields(parent, config);

    int64_t number = 2;
    for (auto const& run : runs)
    {
        parent.timestamp = run.timestamp - 2;
        stampForkFields(parent, config);
        parent.extraData = run.parentExtra;  // stampForkFields resets extraData; restore the
                                             // boundary shape this run wants on the parent
        bcos::protocol::EthBlockHeaderData child;
        child.number = number++;
        child.timestamp = run.timestamp;
        child.uncleHash = bcos::protocol::c_emptyOmmersHash;
        child.gasLimit = 30000000;
        child.gasUsed = 4000000;
        child.coinbase = c_opSequencerFeeVault;
        OpPair p{parent, child, config};
        stampForkFields(p);
        child = p.child;
        child.baseFee = expectedBaseFee(p);
        auto result = validateOpHeader(child, parent, config);
        BOOST_CHECK_MESSAGE(result.valid, "ts=" << run.timestamp << " error: " << result.error);
        parent = child;
    }
}

// F1 defect scenario (single fork-activation parser): a schedule that sets ONLY
// jovian_time/karst_time — isthmus_time unset, the only shape existing chains
// configure (NodeConfigOpStackELTest's opGenesis fixture is exactly this) — must
// resolve to Isthmus+ under the SAME ladder semantics the executor uses. Before the
// fix the validator kept its own per-field reading (unset field == fork inactive),
// so this config booted fine, executed as Isthmus+, and then rejected block 1 with
// "withdrawalsHash present before Canyon" — a permanent slow-probe stall with no
// config error.
BOOST_AUTO_TEST_CASE(jovianKarstOnlyScheduleAcceptsIsthmusShapeHeaders)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule = bcos::ledger::OpForkSchedule{};  // every rung unset...
    p.config.forkSchedule.m_jovianTime = 0;                  // ...except jovian/karst,
    p.config.forkSchedule.m_karstTime = 0;                   // active from genesis
    // resolveOpFork -> Karst for both headers: withdrawalsHash, Cancun blob fields,
    // requestsHash and the 17-byte Jovian extraData are all required.
    stampForkFields(p.parent, p.config);
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);
    auto result = validateOpHeader(p.child, p.parent, p.config);
    BOOST_CHECK_MESSAGE(result.valid, "error: " << result.error);

    // The Baseline half of the same shape: isthmus/jovian/karst unset -> every
    // timestamp resolves to the Isthmus zero-start baseline.
    p.config.forkSchedule = bcos::ledger::OpForkSchedule{};
    stampForkFields(p.parent, p.config);
    stampForkFields(p);
    BOOST_CHECK_EQUAL(
        static_cast<int>(bcos::ledger::resolveOpFork(p.config.forkSchedule, kParentTs)),
        static_cast<int>(OpFork::Isthmus));
    p.child.baseFee = expectedBaseFee(p);
    result = validateOpHeader(p.child, p.parent, p.config);
    BOOST_CHECK_MESSAGE(result.valid, "error: " << result.error);
}

// isthmus_time set with canyon/ecotone/holocene UNSET: the ladder implies the lower
// rungs (an unscheduled intermediate fork is skipped, not "inactive"), so a ts >= T
// Isthmus-shaped header passes. The pre-fix per-field reading rejected it.
BOOST_AUTO_TEST_CASE(isthmusTimeAloneImpliesLowerRungs)
{
    auto p = makeBedrockPair();
    p.config.forkSchedule = bcos::ledger::OpForkSchedule{};
    p.config.forkSchedule.m_isthmusTime = kParentTs;  // parent already Isthmus
    stampForkFields(p.parent, p.config);              // Isthmus: 9B Holocene extraData shape
    stampForkFields(p);
    p.child.baseFee = expectedBaseFee(p);
    auto result = validateOpHeader(p.child, p.parent, p.config);
    BOOST_CHECK_MESSAGE(result.valid, "error: " << result.error);
}

BOOST_AUTO_TEST_SUITE_END()
