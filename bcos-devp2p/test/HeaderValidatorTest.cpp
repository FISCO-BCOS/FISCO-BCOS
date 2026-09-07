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
    parent.uncleHash = emptyOmmersHash();
    parent.gasLimit = 30000000;
    parent.gasUsed = 21000;
    parent.baseFee = u256(1000000000);

    auto& child = p.child;
    child.number = 2;
    child.timestamp = 1600000001;
    child.difficulty = 0;
    child.uncleHash = emptyOmmersHash();
    child.gasLimit = 30000000;
    child.gasUsed = 21000;
    child.baseFee = computeNextBaseFee(parent);  // 875175000 (golden)

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
    // schedule: target 3 blobs per block).
    auto parent = makeValidPair().parent;
    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(2 * kGasPerBlob);  // 262144
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kCancunBlobSchedule), u256(0));

    parent.excessBlobGas = u256(200000);
    parent.blobGasUsed = u256(2 * kGasPerBlob);
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kCancunBlobSchedule), u256(68928));

    parent.excessBlobGas = u256(0);
    parent.blobGasUsed = u256(0);
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kCancunBlobSchedule), u256(0));
}

BOOST_AUTO_TEST_CASE(rejectsNonZeroDifficulty)
{
    auto p = makeValidPair();
    p.child.difficulty = 1;
    auto result = validateHeaderPoS(p.child, p.parent, p.config);
    BOOST_CHECK(!result.valid);
    BOOST_CHECK(result.error.find("difficulty") != std::string::npos);
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
    p.child.excessBlobGas = computeNextExcessBlobGas(p.parent, kCancunBlobSchedule);  // 68928
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
        computeNextExcessBlobGas(parent, kCancunBlobSchedule), u256(2 * kGasPerBlob));
    BOOST_CHECK_EQUAL(computeNextExcessBlobGas(parent, kPragueBlobSchedule), u256(0));

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
