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
 * @file HeaderValidatorTest.cpp
 * @brief Unit tests for the Ethereum PoS header validator.
 * @date 2026/8/18
 */
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::devp2p::sync;

namespace
{
// A minimal PoS-valid parent/child pair. Child's baseFee matches the EIP-1559
// recomputation from the parent.
struct PoSPair
{
    bcos::protocol::EthBlockHeaderData parent;
    bcos::protocol::EthBlockHeaderData child;
    ChainConfig config;
};

PoSPair makeValidPair()
{
    PoSPair p;
    auto& parent = p.parent;
    parent.number = 1;
    parent.timestamp = 1600000000;
    parent.difficulty = 0;
    parent.uncleHash = bcos::protocol::c_emptyOmmersHash;
    parent.gasLimit = 30000000;
    parent.gasUsed = 21000;
    parent.baseFee = u256(1000000000);
    parent.blobGasUsed = u256(0);
    parent.excessBlobGas = u256(0);

    auto& child = p.child;
    child.number = 2;
    child.timestamp = 1600000001;
    child.difficulty = 0;
    child.uncleHash = bcos::protocol::c_emptyOmmersHash;
    child.gasLimit = 30000000;
    child.gasUsed = 21000;
    child.baseFee = computeNextBaseFee(parent);  // 875175000 (golden)
    // Shanghai/Cancun/Prague fields are mandatory once those forks are active (the
    // default config activates every fork from genesis; Osaka/BPO are NOT active by
    // default — ChainConfig defaults them to UINT64_MAX, "not yet active").
    child.withdrawalsHash = h256{};
    child.blobGasUsed = u256(0);
    child.excessBlobGas = computeNextExcessBlobGas(parent, kCancunBlobSchedule, false);  // 0
    child.parentBeaconRoot = h256{};
    child.requestsHash = h256{};

    p.config.chainId = 1;  // London active from genesis (londonTime = 0)
    return p;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(HeaderValidatorTest)

BOOST_AUTO_TEST_CASE(validPoSHeaderPasses)
{
    auto p = makeValidPair();
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);
    BOOST_CHECK(result.error.empty());
}

BOOST_AUTO_TEST_CASE(baseFeeGoldenVector)
{
    // Verified against an independent Python computation.
    auto p = makeValidPair();
    BOOST_CHECK_EQUAL(computeNextBaseFee(p.parent), u256(875175000));
}

BOOST_AUTO_TEST_CASE(excessBlobGasGoldenVectors)
{
    // Verified against an independent Python computation (EIP-4844, Cancun
    // schedule: target 3 blobs per block, Osaka NOT active).
    auto parent = makeValidPair().parent;
    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(2 * kGasPerBlob);  // 262144
    BOOST_CHECK_EQUAL(
        computeNextExcessBlobGas(parent, kCancunBlobSchedule, false), u256(0));

    parent.excessBlobGas = u256(200000);
    parent.blobGasUsed = u256(2 * kGasPerBlob);
    BOOST_CHECK_EQUAL(
        computeNextExcessBlobGas(parent, kCancunBlobSchedule, false), u256(68928));

    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(0);
    BOOST_CHECK_EQUAL(
        computeNextExcessBlobGas(parent, kCancunBlobSchedule, false), u256(0));
}

// EIP-7918 (Osaka): when the blob fee sits below the reserve price
// (8192 * baseFee > 131072 * blobBaseFee), the excess grows by
// used * (max - target) / max instead of the plain target delta.
// Vectors verified against an independent Python port of the EIP-4844
// fake_exponential + geth's consensus/misc/eip4844 calcExcessBlobGas.
BOOST_AUTO_TEST_CASE(eip7918ExcessBlobGasGoldenVectors)
{
    auto parent = makeValidPair().parent;
    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(4 * kGasPerBlob);  // over the Cancun target (3)
    parent.baseFee = u256(1000000000);

    // Osaka active: reserve 8.192e12 > blob price (~1.3e5) -> EIP-7918 branch.
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kCancunBlobSchedule, true), u256(262144));
    // Osaka inactive: plain EIP-4844 delta (4 blobs - 3 target).
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kCancunBlobSchedule, false), u256(131072));

    // Blob fee ABOVE the reserve price: even under Osaka the original EIP-4844
    // rule applies. parent excess 30e6 -> blobBaseFee 7991 -> blob price
    // 7991 * 131072 = 1047396352 > reserve 8192 * 100000 = 819200000.
    auto p2 = makeValidPair().parent;
    p2.excessBlobGas = u256(30000000);
    p2.blobGasUsed = u256(2 * kGasPerBlob);
    p2.baseFee = u256(100000);
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(p2, kCancunBlobSchedule, true), u256(29868928));
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(p2, kCancunBlobSchedule, false), u256(29868928));

    // Branch-discriminating vector: at baseFee 120000 the reserve is
    // 8192 * 120000 = 983040000 and the spec blob price 7991 * 131072 =
    // 1047396352 stays ABOVE it (plain rule). A fake_exponential that starts
    // the accumulator at factor instead of factor * denominator computes
    // blobBaseFee 6816 -> 893386752 < reserve, wrongly taking the EIP-7918
    // branch (30131072). The correct answer is the plain delta 29868928.
    auto p3 = makeValidPair().parent;
    p3.excessBlobGas = u256(30000000);
    p3.blobGasUsed = u256(2 * kGasPerBlob);
    p3.baseFee = u256(120000);
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(p3, kCancunBlobSchedule, true), u256(29868928));
}

// EIP-7840 schedule progression past Prague: BPO1 (10/15), BPO2 (14/21).
// The same parent yields different expected excess per active schedule.
// Vectors verified against an independent Python port of geth's calcExcessBlobGas.
BOOST_AUTO_TEST_CASE(postOsakaBlobScheduleGoldenVectors)
{
    // 12 blobs used: above the Prague target (6) and BPO1 target (10), below the
    // BPO2 target (14). baseFee 1e9 keeps the EIP-7918 reserve-price condition true.
    auto parent = makeValidPair().parent;
    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(12 * kGasPerBlob);
    parent.baseFee = u256(1000000000);

    // Prague schedule (also Osaka): 12-6 = 6 blobs under the old rule, but
    // EIP-7918 scales by (9-6)/9 = 1/3 of the used gas.
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kPragueBlobSchedule, true), u256(524288));
    // BPO1: 12 used vs target 10 -> EIP-7918 scaled by (15-10)/15.
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kBpo1BlobSchedule, true), u256(524288));
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kBpo1BlobSchedule, false), u256(262144));
    // BPO2: 12 used is below the target 14 -> excess resets to 0.
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kBpo2BlobSchedule, true), u256(0));

    // 10 blobs: exactly at BPO1's target (10), under BPO2's (14).
    auto p2 = makeValidPair().parent;
    p2.excessBlobGas = u256(0);
    p2.blobGasUsed = u256(10 * kGasPerBlob);
    p2.baseFee = u256(1000000000);
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(p2, kBpo1BlobSchedule, true), u256(436906));
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(p2, kBpo2BlobSchedule, true), u256(0));
}

// A full post-Osaka / BPO2 header validates end to end: the validator selects the
// BPO2 schedule by timestamp and applies EIP-7918 from Osaka on.
BOOST_AUTO_TEST_CASE(postBpo2HeaderValidates)
{
    auto p = makeValidPair();
    // Sepolia-style tail: osaka/bpo1/bpo2 activate before the child block.
    p.config.pragueTime = 1600000000;
    p.config.osakaTime = 1600000000;
    p.config.bpo1Time = 1600000001;
    p.config.bpo2Time = 1600000001;
    // Parent carries 12 blobs of usage with a non-zero excess.
    p.parent.excessBlobGas = u256(2000000);
    p.parent.blobGasUsed = u256(12 * kGasPerBlob);
    p.parent.baseFee = u256(1000000000);
    // Child has no blobs; its excess is recomputed under BPO2 + EIP-7918.
    p.child.excessBlobGas =
        computeNextExcessBlobGas(p.parent, kBpo2BlobSchedule, /*osaka=*/true);
    p.child.blobGasUsed = u256(0);
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);
    if (!result.valid)
    {
        BOOST_TEST_MESSAGE("postBpo2HeaderValidates: " << result.error);
    }

    // Wrong excess is rejected (catches a validator stuck on the Prague schedule).
    auto bad = p;
    bad.child.excessBlobGas = u256(0);
    auto rejected = validateHeaderPoS(bad.child, bad.parent, bad.config);
    BOOST_CHECK(!rejected.valid);
    BOOST_CHECK(rejected.error.find("excessBlobGas") != std::string::npos);

    // 16 blobs fit under the BPO2 max (21) but exceed the BPO1 max (15).
    auto pMax = makeValidPair();
    pMax.config.pragueTime = 1600000000;
    pMax.config.osakaTime = 1600000000;
    pMax.config.bpo1Time = 1600000001;
    pMax.config.bpo2Time = 1600000002;  // still BPO1 at the child block
    pMax.parent.excessBlobGas = u256(0);
    pMax.parent.blobGasUsed = u256(0);
    pMax.child.blobGasUsed = u256(16 * kGasPerBlob);  // over BPO1 max (15)
    pMax.child.excessBlobGas = u256(0);
    auto overBpo1 = validateHeaderPoS(pMax.child, pMax.parent, pMax.config);
    BOOST_CHECK(!overBpo1.valid);

    pMax.config.bpo2Time = 1600000001;  // BPO2 active at the child: max 21
    auto underBpo2 = validateHeaderPoS(pMax.child, pMax.parent, pMax.config);
    BOOST_CHECK(underBpo2.valid);
}

// F7: Prague must be fail-closed on requestsHash like Shanghai/Cancun fields.
BOOST_AUTO_TEST_CASE(rejectsMissingRequestsHashWhenPragueActive)
{
    auto p = makeValidPair();
    p.child.requestsHash.reset();
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("requestsHash") != std::string::npos);

    // Pre-Prague the field is genuinely absent: not an error.
    auto p2 = makeValidPair();
    p2.config.shanghaiTime = 1600000002;
    p2.config.cancunTime = 1600000002;
    p2.config.pragueTime = 1600000002;  // activates after the child block
    p2.child.withdrawalsHash.reset();
    p2.child.blobGasUsed.reset();
    p2.child.excessBlobGas.reset();
    p2.child.parentBeaconRoot.reset();
    p2.child.requestsHash.reset();
    auto ok = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(ok.valid);
}

BOOST_AUTO_TEST_CASE(rejectsNonZeroDifficulty)
{
    auto p = makeValidPair();
    p.child.difficulty = 1;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("difficulty") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsNonZeroPoSNonce)
{
    auto p = makeValidPair();
    p.child.nonce = h64{0x01};
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("nonce") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsMissingWithdrawalsHashWhenShanghaiActive)
{
    auto p = makeValidPair();
    p.child.withdrawalsHash.reset();
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("withdrawalsHash") != std::string::npos);

    // Pre-Shanghai the field is genuinely absent: not an error.
    auto p2 = makeValidPair();
    p2.config.shanghaiTime = 1600000002;  // activates after the child block
    p2.config.cancunTime = 1600000002;
    p2.child.withdrawalsHash.reset();
    p2.child.blobGasUsed.reset();
    p2.child.excessBlobGas.reset();
    p2.child.parentBeaconRoot.reset();
    auto ok = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(ok.valid);
}

BOOST_AUTO_TEST_CASE(rejectsMissingBlobFieldsWhenCancunActive)
{
    auto p = makeValidPair();
    p.child.blobGasUsed.reset();
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("blobGasUsed") != std::string::npos);

    auto p2 = makeValidPair();
    p2.child.excessBlobGas.reset();
    auto bad2 = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad2.valid);
    BOOST_CHECK(bad2.error.find("excessBlobGas") != std::string::npos);

    auto p3 = makeValidPair();
    p3.child.parentBeaconRoot.reset();
    auto bad3 = validateHeaderPoS(p3.child, p3.parent, p3.config);
    BOOST_CHECK(!bad3.valid);
    BOOST_CHECK(bad3.error.find("parentBeaconBlockRoot") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsOmmers)
{
    auto p = makeValidPair();
    p.child.uncleHash = h256{0x1234};
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("ommers") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsOversizedExtraData)
{
    auto p = makeValidPair();
    p.child.extraData.assign(kMaxExtraDataSize + 1, 0xab);
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("extraData") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsNonIncreasingTimestamp)
{
    auto p = makeValidPair();
    p.child.timestamp = p.parent.timestamp;  // equal, not strictly greater
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("timestamp") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsNonContiguousNumber)
{
    auto p = makeValidPair();
    p.child.number = 5;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("number") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsGasLimitBelowMinimum)
{
    auto p = makeValidPair();
    p.child.gasLimit = 4999;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsGasLimitDeltaTooLarge)
{
    auto p = makeValidPair();
    // |Δ| = 30000 > 30000000 / 1024 ≈ 29296
    p.child.gasLimit = 30030000;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("gasLimit") != std::string::npos);

    // A bound-legal delta must pass.
    auto p2 = makeValidPair();
    p2.child.gasLimit = 30029000;  // Δ = 29000 <= 29296
    auto ok = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(ok.valid);
}

BOOST_AUTO_TEST_CASE(rejectsGasLimitDeltaAtBound)
{
    // floor(30000000 / 1024) = 29296; the yellow paper and geth VerifyGaslimit
    // reject a delta >= the bound.
    auto p = makeValidPair();
    p.child.gasLimit = 30000000 + 29296;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("gasLimit") != std::string::npos);

    auto p2 = makeValidPair();
    p2.child.gasLimit = 30000000 + 29295;
    auto ok = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(ok.valid);
}

BOOST_AUTO_TEST_CASE(rejectsGasUsedAboveLimit)
{
    auto p = makeValidPair();
    p.child.gasUsed = p.child.gasLimit + 1;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("gasUsed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsMissingBaseFeeWhenLondonActive)
{
    auto p = makeValidPair();
    p.child.baseFee.reset();
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("baseFeePerGas") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsWrongBaseFee)
{
    auto p = makeValidPair();
    p.child.baseFee = *p.child.baseFee + 1;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("baseFeePerGas") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(londonActivationBlockUsesInitialBaseFee)
{
    auto p = makeValidPair();
    p.config.londonTime = 1600000001;  // the child block activates London
    p.parent.baseFee.reset();           // parent is pre-London
    p.child.baseFee = p.config.initialBaseFee;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);

    // A non-initial base fee on the activation block is rejected.
    auto p2 = makeValidPair();
    p2.config.londonTime = 1600000001;
    p2.parent.baseFee.reset();
    p2.child.baseFee = p2.config.initialBaseFee + 1;
    auto bad = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad.valid);
}

BOOST_AUTO_TEST_CASE(excessBlobGasValidation)
{
    // Parent with blob gas; child recomputes correctly. Prague activates after
    // the child, so the Cancun schedule (target 3) applies.
    auto p = makeValidPair();
    p.config.pragueTime = 1600000002;
    p.parent.excessBlobGas = u256(200000);
    p.parent.blobGasUsed = u256(2 * kGasPerBlob);
    p.child.excessBlobGas =
        computeNextExcessBlobGas(p.parent, kCancunBlobSchedule, false);  // 68928
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);

    // Wrong excess blob gas is rejected.
    auto p2 = p;
    p2.child.excessBlobGas = *p2.child.excessBlobGas + 1;
    auto bad = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad.valid);
    BOOST_CHECK(bad.error.find("excessBlobGas") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(rejectsInvalidBlobGasUsed)
{
    auto p = makeValidPair();
    p.child.blobGasUsed = u256(1);  // not a multiple of GAS_PER_BLOB
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("blobGasUsed") != std::string::npos);

    auto p2 = makeValidPair();
    p2.child.blobGasUsed = u256(kCancunBlobSchedule.maxBlobGas + 1);
    auto bad = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad.valid);
}

BOOST_AUTO_TEST_CASE(pragueBlobScheduleRaisesBlobLimits)
{
    // Prague (EIP-7691): target 6 / max 9 blobs per block.
    auto p = makeValidPair();
    p.config.cancunTime = 0;            // Cancun from genesis
    p.config.pragueTime = 1600000001;   // the child activates Prague
    p.parent.excessBlobGas = u256(0);
    p.parent.blobGasUsed = u256(0);
    p.child.excessBlobGas = u256(0);
    p.child.blobGasUsed = u256(7 * kGasPerBlob);  // 7 blobs: over the Cancun max
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);

    auto p2 = p;
    p2.child.blobGasUsed = u256(10 * kGasPerBlob);  // 10 blobs: over the Prague max
    auto bad = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad.valid);
    BOOST_CHECK(bad.error.find("blobGasUsed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(pragueExcessBlobGasRecomputation)
{
    // 5 blobs used: over the Cancun target (3), under the Prague target (6).
    auto parent = makeValidPair().parent;
    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(5 * kGasPerBlob);
    BOOST_CHECK_EQUAL(
        computeNextExcessBlobGas(parent, kCancunBlobSchedule, false), u256(2 * kGasPerBlob));
    BOOST_CHECK_EQUAL(
        computeNextExcessBlobGas(parent, kPragueBlobSchedule, false), u256(0));

    // The validator picks the schedule by the child header timestamp.
    auto p = makeValidPair();
    p.config.pragueTime = 1600000002;  // still Cancun at the child block
    p.parent.excessBlobGas = u256(0);
    p.parent.blobGasUsed = u256(5 * kGasPerBlob);
    p.child.excessBlobGas = u256(2 * kGasPerBlob);
    p.child.blobGasUsed = u256(0);
    auto cancun = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(cancun.valid);

    auto p2 = makeValidPair();
    p2.config.pragueTime = 1600000001;  // the child activates Prague
    p2.parent.excessBlobGas = u256(0);
    p2.parent.blobGasUsed = u256(5 * kGasPerBlob);
    p2.child.excessBlobGas = u256(0);
    p2.child.blobGasUsed = u256(0);
    auto prague = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(prague.valid);
}

BOOST_AUTO_TEST_CASE(cancunActivationBlockResetsExcess)
{
    auto p = makeValidPair();
    p.config.cancunTime = 1600000001;  // child activates Cancun
    p.parent.excessBlobGas.reset();
    p.child.excessBlobGas = u256(0);  // expected zero at activation
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(result.valid);

    auto p2 = makeValidPair();
    p2.config.cancunTime = 1600000001;
    p2.parent.excessBlobGas.reset();
    p2.child.excessBlobGas = u256(1);
    auto bad = validateHeaderPoS(p2.child, p2.parent, p2.config);
    BOOST_CHECK(!bad.valid);
}

BOOST_AUTO_TEST_CASE(forkBlockHelpers)
{
    // forkTime == 0 → active from genesis.
    BOOST_CHECK(isForkActive(0, 1));
    BOOST_CHECK(!isForkBlock(0, 1, 2));
    // Normal timestamp fork.
    BOOST_CHECK(isForkBlock(100, 99, 100));
    BOOST_CHECK(!isForkBlock(100, 100, 100));
    BOOST_CHECK(!isForkBlock(100, 99, 99));
    BOOST_CHECK(isForkActive(100, 100));
    BOOST_CHECK(!isForkActive(100, 99));
}

BOOST_AUTO_TEST_SUITE_END()
