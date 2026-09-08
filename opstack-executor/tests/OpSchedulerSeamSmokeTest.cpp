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
 * @file OpSchedulerSeamSmokeTest.cpp
 * @brief OpSchedulerSeam smoke tests
 */

// OpSchedulerSeamSmokeTest — minimal compile-and-run verification that the ported
// `bcos::evm::engine::OpSchedulerSeam` header instantiates against the current branch's types and
// that its engine-facing seam surface works. Exercises only:
//   1. construction over a real MultiLayerStorage ViewType;
//   2. the static seam surface the engine reaches as dependent names
//      (computeTxRoot / commitmentsOf / isJovianActive).
//      (The block-pre shape checks live in PreBlockOpStepsTest; the seam itself no longer
//      executes blocks — see the note at the end of this file.)
#include "OpSchedulerSeamTestHelpers.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace testutil = bcos::evm::engine::testutil;
namespace op = bcos::evm::opstack;

namespace
{
using bcos::evm::engine::testutil::TrivialCheckpointStorage;


using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

// Named selectors so BOOST_CHECK_EQUAL_COLLECTIONS does not dangle temporaries.

/// Fully populated L1 info (snapshot + SystemConfig) for the offset pins.
bcos::evm::opstack::L1BlockInfo filledL1Info()
{
    bcos::evm::opstack::L1BlockInfo l1Info;
    l1Info.sequenceNumber = 0x1122334455667788ull;
    l1Info.time = 0x2233445566778899ull;
    l1Info.number = 0x33445566778899aaull;
    l1Info.baseFee = intx::uint256{0x445566778899aabbull};
    l1Info.blobBaseFee = intx::uint256{0x5566778899aabbccull};
    l1Info.baseFeeScalar = 0xa1b2c3d4u;
    l1Info.blobBaseFeeScalar = 0x11223344u;
    l1Info.operatorFeeScalar = 0x55667788u;
    l1Info.operatorFeeConstant = 0x99aabbccddeeff00ull;
    for (size_t i = 0; i < sizeof(l1Info.blockHash.bytes); ++i)
    {
        l1Info.blockHash.bytes[i] = static_cast<uint8_t>(0x60 + i);
        l1Info.batcherHash.bytes[i] = static_cast<uint8_t>(0x90 + i);
    }
    return l1Info;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSeamSmokeSuite)

BOOST_AUTO_TEST_CASE(ConstructAndSeamSurface)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    // L1BlockInfo is required (no silent default). Construction with the unset sentinel is
    // allowed; synthesizeL1AttributesEnvelope is what refuses it.
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, {});

    // Fork predicate: feature-driven (feature_op_jovian), constant across blocks — no timestamps.
    BOOST_CHECK(!scheduler.isJovianActive());
    bcos::evm::engine::OpSchedulerSeam<ViewType> jovianScheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true}, {});
    BOOST_CHECK(jovianScheduler.isJovianActive());

    // computeTxRoot over the empty range: the standard empty-trie root (0x56e81f...), which
    // proves the trie built and hashed end-to-end.
    std::vector<bcos::bytes> emptyTxs;
    const auto txRoot = scheduler.computeTxRoot(emptyTxs);
    const bcos::h256 kEmptyTrieRoot{
        std::string{"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"}};
    BOOST_CHECK_EQUAL(txRoot, kEmptyTrieRoot);

    // commitmentsOf projects the seal + result members into the engine-facing surface.
    bcos::evm::opstack::OpBlockSeal seal;
    seal.receiptsRoot = evmone::hash256{};
    seal.logsBloom = evmone::state::BloomFilter{};
    seal.withdrawalsRoot = evmone::hash256{};
    bcos::evm::engine::OpExecuteBlockResult result{
        .receipts = {},
        .seal = seal,
        .stateRoot = bcos::h256{},
        .gasUsed = 0,
        .txRoot = txRoot,
    };
    const auto commitments = scheduler.commitmentsOf(result);
    BOOST_CHECK_EQUAL(commitments.stateRoot, bcos::h256{});
    BOOST_CHECK_EQUAL(commitments.gasUsed, bcos::u256(0));
    BOOST_CHECK_EQUAL(commitments.txRoot, txRoot);
}

BOOST_AUTO_TEST_CASE(SynthesizeL1AttributesIsDepositEnvelope)
{
    auto const env = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
    BOOST_REQUIRE(!env.empty());
    BOOST_CHECK_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
}

BOOST_AUTO_TEST_CASE(SynthesizeRefusesUnsetL1BlockInfo)
{
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, {});
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(), std::invalid_argument);
}

/// isUnsetSystemConfig is a disjunction (baseFeeScalar == 0 || is_zero(batcherHash)); each
/// operand is zeroed on its own so an && regression fails one of the two cases.
BOOST_AUTO_TEST_CASE(SynthesizeRefusesZeroBaseFeeScalar)
{
    auto l1Info = filledL1Info();
    l1Info.baseFeeScalar = 0;
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, l1Info);
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SynthesizeRefusesZeroBatcherHash)
{
    auto l1Info = filledL1Info();
    std::fill(l1Info.batcherHash.bytes, l1Info.batcherHash.bytes + 32, 0);
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, l1Info);
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositMatchesIsthmusLayout)
{
    // All-zero L1 is a test-only fixture: call the encoder, not the production seam.
    const auto env =
        bcos::evm::opstack::synthesizeL1AttributesDeposit(bcos::evm::opstack::L1BlockInfo{}, false);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));

    BOOST_CHECK(dep.from == bcos::evm::opstack::OP_DEPOSITOR);
    BOOST_REQUIRE(dep.to.has_value());
    BOOST_CHECK(*dep.to == bcos::evm::opstack::OP_L1_BLOCK);
    BOOST_CHECK(!dep.mint.has_value());
    BOOST_CHECK(dep.value == intx::uint256{0});
    // Literal, not c_l1InfoDepositGas: synthesizeL1AttributesDeposit sets the field FROM that
    // constant, so comparing against it cannot detect a wrong value (op-geth L1InfoDepositGas).
    BOOST_CHECK_EQUAL(dep.gas_limit, 1'000'000);
    BOOST_CHECK(!dep.is_system_tx);
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::IsthmusL1AttributesLen);
    BOOST_CHECK(std::equal(op::IsthmusL1AttributesSelector.begin(),
        op::IsthmusL1AttributesSelector.end(), dep.data.begin()));

    // sourceHash = keccak(bytes32(1) || keccak(l1Hash || bytes32(seq))).
    std::array<uint8_t, 64> innerInput{};
    const auto inner =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(innerInput.data(), innerInput.size()));
    std::array<uint8_t, 64> domainInput{};
    domainInput[31] = 1;
    std::copy(inner.begin(), inner.end(), domainInput.begin() + 32);
    const auto expectedHash =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(domainInput.data(), domainInput.size()));
    BOOST_CHECK_EQUAL_COLLECTIONS(dep.source_hash.bytes, dep.source_hash.bytes + 32,
        expectedHash.begin(), expectedHash.end());
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsCalldataFieldOffsets)
{
    auto const l1Info = filledL1Info();
    std::array<uint8_t, 32> hashBytes{};
    std::array<uint8_t, 32> batcherBytes{};
    std::copy_n(l1Info.blockHash.bytes, 32, hashBytes.begin());
    std::copy_n(l1Info.batcherHash.bytes, 32, batcherBytes.begin());

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true}, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope();
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::JovianL1AttributesLen);
    auto const& calldata = dep.data;

    // Selector [0:4].
    BOOST_CHECK(std::equal(op::JovianL1AttributesSelector.begin(),
        op::JovianL1AttributesSelector.end(), calldata.begin()));
    testutil::checkU32(calldata, 4, l1Info.baseFeeScalar);
    testutil::checkU32(calldata, 8, l1Info.blobBaseFeeScalar);
    testutil::checkBE(calldata, 12, l1Info.sequenceNumber);  // seq
    testutil::checkBE(calldata, 20, l1Info.time);            // l1 time
    testutil::checkBE(calldata, 28, l1Info.number);          // l1 number
    testutil::checkBE256(calldata, 36, l1Info.baseFee);      // l1 baseFee
    testutil::checkBE256(calldata, 68, l1Info.blobBaseFee);  // l1 blobBaseFee
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(),
        hashBytes.end());  // l1 blockHash
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 132, calldata.begin() + 164, batcherBytes.begin(), batcherBytes.end());
    testutil::checkU32(calldata, 164, l1Info.operatorFeeScalar);
    testutil::checkBE(calldata, 168, l1Info.operatorFeeConstant);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsIsthmusCalldataFieldOffsets)
{
    auto const l1Info = filledL1Info();
    std::array<uint8_t, 32> hashBytes{};
    std::array<uint8_t, 32> batcherBytes{};
    std::copy_n(l1Info.blockHash.bytes, 32, hashBytes.begin());
    std::copy_n(l1Info.batcherHash.bytes, 32, batcherBytes.begin());

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope();
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::IsthmusL1AttributesLen);
    auto const& calldata = dep.data;

    BOOST_CHECK(std::equal(op::IsthmusL1AttributesSelector.begin(),
        op::IsthmusL1AttributesSelector.end(), calldata.begin()));
    testutil::checkU32(calldata, 4, l1Info.baseFeeScalar);
    testutil::checkU32(calldata, 8, l1Info.blobBaseFeeScalar);
    testutil::checkBE(calldata, 12, l1Info.sequenceNumber);
    testutil::checkBE(calldata, 20, l1Info.time);
    testutil::checkBE(calldata, 28, l1Info.number);
    testutil::checkBE256(calldata, 36, l1Info.baseFee);
    testutil::checkBE256(calldata, 68, l1Info.blobBaseFee);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(), hashBytes.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 132, calldata.begin() + 164, batcherBytes.begin(), batcherBytes.end());
    testutil::checkU32(calldata, 164, l1Info.operatorFeeScalar);
    testutil::checkBE(calldata, 168, l1Info.operatorFeeConstant);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositJovianLayout)
{
    // The sourceHash is domain-1(l1Hash, seq) — L2 time is deliberately not bound, so the
    // builder takes no L2-time argument a caller could vary.
    bcos::evm::opstack::L1BlockInfo const unset{};
    const auto env = bcos::evm::opstack::synthesizeL1AttributesDeposit(unset, true);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::JovianL1AttributesLen);
    BOOST_CHECK(std::equal(op::JovianL1AttributesSelector.begin(),
        op::JovianL1AttributesSelector.end(), dep.data.begin()));
    // [176:178] DA-footprint scalar is zero.
    BOOST_CHECK_EQUAL(dep.data[176], 0);
    BOOST_CHECK_EQUAL(dep.data[177], 0);
}

// Note: the empty-block rejection test lives in PreBlockOpStepsTest (RejectsEmptyBlock).
// OpSchedulerSeam is a pure engine seam and no longer executes blocks, so there is no
// matching execution case here. The EIP-7702 authorization yParity width test was removed
// with the RLP decode primitives (decodeAuthYParityScalar retired in OpCommon.h).

BOOST_AUTO_TEST_SUITE_END()
