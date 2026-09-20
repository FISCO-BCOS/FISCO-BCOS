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
 * @file OpEngineSequenceMatrixTest.cpp
 * @brief M4: import/FCU canonicality sequences under the I1-I6 invariant battery.
 */

// M4: each scenario drives the real OpScheduler-backed service and runs the I1-I6
// battery (support/SequenceInvariants.h) after every step. The guarding findings are
// named per sequence so a future reader can trace a cell back to the defect it
// protects: S3/S4 -> N2/N3/NEW-1, S9 -> F3, S10 -> F5, S12 -> N4, S13 -> NEW-3,
// S14 -> NEW-2.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "support/OpEngineKarstTestHarness.h"
#include "support/SequenceInvariants.h"
#include <opstack-executor/RecentBlockHashes.h>

#include <boost/test/unit_test.hpp>

namespace op_matrix
{
using namespace bcos::engine;
using namespace op_engine_parity_test;

namespace
{
constexpr int kValid = static_cast<int>(bcos::engine::PayloadValidationStatus::Valid);
constexpr int kInvalid = static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid);
constexpr int kSyncing = static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing);

template <class Fixture>
int importPayload(Fixture& f, bcos::engine::NewPayloadRequest const& request)
{
    return static_cast<int>(bcos::task::syncWait(f.service.newPayload(request, 4)).status);
}

template <class Fixture>
int fcuTo(Fixture& f, bcos::h256 const& head, bcos::h256 const& safe, bcos::h256 const& finalized,
    std::uint32_t version = 3)
{
    bcos::engine::ForkchoiceState forkchoice{head, safe, finalized};
    return static_cast<int>(
        bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, version))
            .payloadStatus.status);
}

/// Cache-layer byte read (S13): the value at @p key in the process-lifetime cache.
template <class CacheType>
std::optional<std::string> cacheValue(CacheType& cache, bcos::executor_v1::StateKey key)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(cache, std::move(key)));
    if (!entry.has_value())
    {
        return std::nullopt;
    }
    auto const value = entry->get();
    return std::string(value.begin(), value.end());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineSequenceMatrixSuite)

/// S1: forward-canonicalize one block at a time, running the battery after each FCU.
BOOST_AUTO_TEST_CASE(S1_ForwardCanonicalizeBlockByBlock)
{
    ImportServiceFixture f;
    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, request1.executionPayload.blockHash,
                            request1.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 0, .head = 1, .headTxCount = 1});

    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(importPayload(f, request2), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, request2.executionPayload.blockHash,
                            request2.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 1, .head = 2, .headTxCount = 1});

    auto request3 = f.validRequest(request2.executionPayload.blockHash, 3);
    BOOST_REQUIRE_EQUAL(importPayload(f, request3), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, request3.executionPayload.blockHash,
                            request3.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 2, .head = 3, .headTxCount = 1});
}

/// S2: import three blocks, then a single jump FCU straight to the third. The middle
/// blocks must still end up canonical (I1/I2 at heights 1 and 2 prove it).
BOOST_AUTO_TEST_CASE(S2_JumpFcuOverMiddleBlocks)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S3 (guards N2/NEW-1): replace the canonical block at height 2 with a sibling.
/// The tip rewinds 3 -> 2 on purpose, so I6 must be told this is a rewind.
BOOST_AUTO_TEST_CASE(S3_SameHeightSwitchDropsSibling)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const bHash = f.seededChainHash[2];
    auto const cHash = f.seededChainHash[3];
    auto requestBPrime = f.makeSiblingAtHeight2();
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE(bPrimeHash != bHash);

    BOOST_REQUIRE_EQUAL(importPayload(f, requestBPrime), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, bPrimeHash, bPrimeHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 3,
            .head = 2,
            .headTxCount = 1,
            .deadHashes = {bHash, cHash},
            .allowRewind = true});
}

/// S4 (guards N3): B' switches in while the old chain has a grandchild above the new
/// head (C@3 carries its child D@4). The next legal import at height 3 must be VALID,
/// i.e. the orphaned occupant must not block it.
BOOST_AUTO_TEST_CASE(S4_MultiOrphanAboveHead)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const cHash = f.seededChainHash[3];

    // D@4: a stored child of C, so C is not a leaf when it gets orphaned.
    auto requestD = f.validRequest(cHash, 4);
    BOOST_REQUIRE_EQUAL(importPayload(f, requestD), kValid);

    auto requestBPrime = f.makeSiblingAtHeight2();
    (void)aHash;  // sibling's parent is encoded in the request itself
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(importPayload(f, requestBPrime), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, bPrimeHash, bPrimeHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 3, .head = 2, .headTxCount = 1, .allowRewind = true});

    // The next legal payload extends the new tip B'@2 at height 3.
    auto request3 = f.validRequest(bPrimeHash, 3);
    BOOST_REQUIRE_EQUAL(importPayload(f, request3), kValid);
    BOOST_REQUIRE(f.service.hasImportedBlock(request3.executionPayload.blockHash));
    BOOST_REQUIRE_EQUAL(fcuTo(f, request3.executionPayload.blockHash,
                            request3.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 2, .head = 3, .headTxCount = 1});
}

/// S5: FCU to an OLD canonical head (lower than the tip) without attrs must not rewind
/// the tip (op-geth Optimism old-head semantics), and may refresh safe. I6 with
/// allowRewind = false is exactly the assertion.
BOOST_AUTO_TEST_CASE(S5_OldHeadFcuDoesNotRewindLatest)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const bHash = f.seededChainHash[2];
    BOOST_REQUIRE_EQUAL(fcuTo(f, bHash, bHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S6: replaying the identical payload is idempotent (VALID, no re-execution fault) and
/// must not disturb the canonical plane.
BOOST_AUTO_TEST_CASE(S6_DuplicateImportIsIdempotent)
{
    ImportServiceFixture f;
    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);  // replay
    BOOST_REQUIRE_EQUAL(fcuTo(f, request1.executionPayload.blockHash,
                            request1.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 1, .head = 1, .headTxCount = 1});
}

/// S7: an invalid payload must be rejected without poisoning the next legal import.
BOOST_AUTO_TEST_CASE(S7_InvalidThenValidPayload)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const cHash = f.seededChainHash[3];

    auto bad = f.validRequest(cHash, 4);
    bad.executionPayload.stateRoot = bcos::h256(std::string(64, '9'));
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(bad.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), bad.executionPayload, txRoot,
            *bad.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        bad.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(importPayload(f, bad), kInvalid);
    BOOST_REQUIRE(f.service.hasImportedBlock(bad.executionPayload.blockHash) == false);

    auto good = f.validRequest(cHash, 4);
    BOOST_REQUIRE_EQUAL(importPayload(f, good), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, good.executionPayload.blockHash, good.executionPayload.blockHash,
                            fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 4, .headTxCount = 1});
}

/// S8 (guards N6): a rejected FCU (unknown head -> SYNCING) must not move the pointer,
/// and a following FCU to the real tip must still be VALID.
BOOST_AUTO_TEST_CASE(S8_RejectedFcuThenFcu)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const cHash = f.seededChainHash[3];
    auto const unknown = bcos::h256(0xdeadbeef);
    BOOST_REQUIRE_EQUAL(fcuTo(f, unknown, unknown, fixtureHeadHash()), kSyncing);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});

    BOOST_REQUIRE_EQUAL(fcuTo(f, cHash, cHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S9 (guards F3): a mid-chain canonicalize failure on the PRODUCTION-shaped
/// composition (cache layer bound) must roll both layers back to the pre-call plane.
/// The post-state is the seeded genesis, so the battery runs at height 0.
BOOST_AUTO_TEST_CASE(S9_MidChainMergeFailureRollsBackBothLayers)
{
    CacheImportServiceFixture f(/*stripImportDeltaAt=*/2);
    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(importPayload(f, request2), kValid);

    bool canonicalizeThrew = false;
    try
    {
        (void)fcuTo(f, request2.executionPayload.blockHash, request2.executionPayload.blockHash,
            fixtureHeadHash());
    }
    catch (std::exception const&)
    {
        canonicalizeThrew = true;
    }
    BOOST_REQUIRE_MESSAGE(canonicalizeThrew, "a half-merged canonicalize must not answer VALID");
    checkAll(f.storage, *f.blockFactory, {.previousTip = 0, .head = 0, .headTxCount = 0});
}

/// S10 (guards F5): canonicalize must not hold a lock across its awaited
/// post-condition — a concurrent import completes while it is parked.
BOOST_AUTO_TEST_CASE(S10_ConcurrentImportDuringCanonicalize)
{
    auto gate = std::make_shared<BlockingGate>();
    ImportServiceFixture f(gate);

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);
    auto const b1Hash = request1.executionPayload.blockHash;

    bcos::engine::ForkchoiceUpdatedResult fcuResult;
    std::thread canonicalizer([&] {
        bcos::engine::ForkchoiceState fcu{b1Hash, b1Hash, fixtureHeadHash()};
        fcuResult = bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    });
    gate->entered.wait();  // canonicalize is parked inside its awaited post-condition

    auto request2 = f.validRequest(b1Hash, 2);
    std::atomic<bool> importFinished{false};
    bcos::engine::PayloadStatus importStatus;
    std::thread importer([&] {
        importStatus = bcos::task::syncWait(f.service.newPayload(request2, 4));
        importFinished.store(true, std::memory_order_release);
    });
    for (int i = 0; i < 2000 && !importFinished.load(std::memory_order_acquire); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bool const finished = importFinished.load(std::memory_order_acquire);
    // Unblock before asserting so a RED run cannot hang the suite.
    gate->release.count_down();
    canonicalizer.join();
    importer.join();

    BOOST_REQUIRE_MESSAGE(
        finished, "concurrent newPayload blocked behind the canonicalize lock across an await");
    BOOST_REQUIRE_EQUAL(static_cast<int>(fcuResult.payloadStatus.status), kValid);
    BOOST_REQUIRE_EQUAL(static_cast<int>(importStatus.status), kSyncing);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 1, .head = 1, .headTxCount = 1});
}

/// S11 (F4 is deferred to Tier-2): advancing safe/finalized to the tip must keep every
/// invariant. The memory/flat upper-bound shrink (prune) itself is asserted in S-GH
/// terrain, not here — this cell only pins that the battery survives the advance.
BOOST_AUTO_TEST_CASE(S11_FinalizedAdvanceKeepsInvariants)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const cHash = f.seededChainHash[3];
    BOOST_REQUIRE_EQUAL(fcuTo(f, cHash, cHash, cHash), kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S12 (guards N4): BLOCKHASH must be served by the real RecentBlockHashes read path
/// walking the payload parent chain, not by seeded rows.
BOOST_AUTO_TEST_CASE(S12_BlockhashAcrossPayloadChain)
{
    ImportServiceFixture f;
    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(importPayload(f, request1), kValid);
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(importPayload(f, request2), kValid);
    auto request3 = f.validRequest(request2.executionPayload.blockHash, 3);
    BOOST_REQUIRE_EQUAL(importPayload(f, request3), kValid);

    {
        auto& probeDeltaStorage = *std::static_pointer_cast<MutableStorage>(f.deltaByNumber[3]);
        evmc::bytes32 parentSeed{};
        std::memcpy(
            parentSeed.bytes, request2.executionPayload.blockHash.data(), sizeof(parentSeed.bytes));
        std::optional<std::string> hashError;
        bcos::evm::engine::detail::RecentBlockHashes<MutableStorage> recentHashes(
            probeDeltaStorage, /*blockNumber=*/3, parentSeed, &hashError);
        auto const b1 = recentHashes.get_block_hash(1);
        BOOST_REQUIRE_MESSAGE(
            !hashError.has_value(), "RecentBlockHashes poisoned: " << hashError.value_or(""));
        BOOST_CHECK_EQUAL(bcos::h256(b1.bytes, sizeof(b1.bytes)).hex(),
            request1.executionPayload.blockHash.hex());
        auto const b2 = recentHashes.get_block_hash(2);
        BOOST_CHECK_EQUAL(bcos::h256(b2.bytes, sizeof(b2.bytes)).hex(),
            request2.executionPayload.blockHash.hex());
    }

    BOOST_REQUIRE_EQUAL(fcuTo(f, request3.executionPayload.blockHash,
                            request3.executionPayload.blockHash, fixtureHeadHash()),
        kValid);
    checkAll(f.storage, *f.blockFactory, {.previousTip = 3, .head = 3, .headTxCount = 1});
}

/// S13 (guards NEW-3): the same-height switch must leave the CACHE layer coherent, not
/// only the backend — production reads go through a process-lifetime cache first.
BOOST_AUTO_TEST_CASE(S13_WarmCacheSameHeightSwitch)
{
    CacheImportServiceFixture f;
    f.seedCanonicalChainABC();  // warms the cache via the forward canonicalize
    auto const bHash = f.seededChainHash[2];
    auto const cHash = f.seededChainHash[3];
    auto requestBPrime = f.makeSiblingAtHeight2();
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;

    BOOST_REQUIRE_EQUAL(importPayload(f, requestBPrime), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, bPrimeHash, bPrimeHash, fixtureHeadHash()), kValid);

    // The cache must serve the NEW plane: the height-2 hash row is B' and the pointer is 2.
    auto const cachedHash2 = cacheValue(
        f.cache, bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(2)});
    BOOST_REQUIRE(cachedHash2.has_value());
    auto const bPrimeBytes = bPrimeHash.asBytes();
    BOOST_CHECK_EQUAL(*cachedHash2, std::string(bPrimeBytes.begin(), bPrimeBytes.end()));
    auto const cachedCurrent =
        cacheValue(f.cache, bcos::executor_v1::StateKey{bcos::ledger::SYS_CURRENT_STATE,
                                bcos::ledger::SYS_KEY_CURRENT_NUMBER});
    BOOST_REQUIRE(cachedCurrent.has_value());
    BOOST_CHECK_EQUAL(*cachedCurrent, std::string("2"));

    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 3,
            .head = 2,
            .headTxCount = 1,
            .deadHashes = {bHash, cHash},
            .allowRewind = true});
}

/// S14 (guards NEW-2): re-org BACK onto the abandoned sibling at the same height. The
/// chain roots at A, not at the current tip B', so it must be a switch, not a forward
/// merge onto B''s plane; B' is then the dead hash.
BOOST_AUTO_TEST_CASE(S14_BackReorgOntoAbandonedBranch)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const bHash = f.seededChainHash[2];
    auto requestBPrime = f.makeSiblingAtHeight2();
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(importPayload(f, requestBPrime), kValid);
    BOOST_REQUIRE_EQUAL(fcuTo(f, bPrimeHash, bPrimeHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 3, .head = 2, .headTxCount = 1, .allowRewind = true});

    // Back-reorg onto the abandoned branch B@2.
    BOOST_REQUIRE_EQUAL(fcuTo(f, bHash, bHash, fixtureHeadHash()), kValid);
    checkAll(f.storage, *f.blockFactory,
        {.previousTip = 2,
            .head = 2,
            .headTxCount = 1,
            .deadHashes = {bPrimeHash},
            .allowRewind = true});
}

/// S15: randomized sequences, gated so the PR gate skips it. Nightly sets
/// OPSEQUENCE_MATRIX_STEPS=100000 and OPSEQUENCE_MATRIX_SEED=<uint>.
BOOST_AUTO_TEST_CASE(S15_RandomizedSequences)
{
    auto const steps = std::getenv("OPSEQUENCE_MATRIX_STEPS");
    if (steps == nullptr)
    {
        BOOST_TEST_MESSAGE("OPSEQUENCE_MATRIX_STEPS unset; skipping randomized sequences");
        return;
    }
    auto const seedEnv = std::getenv("OPSEQUENCE_MATRIX_SEED");
    std::uint64_t const seed = seedEnv ? std::stoull(seedEnv) : 20260911ULL;
    std::mt19937_64 rng(seed);

    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    int64_t head = 3;
    int const total = std::stoi(steps);
    for (int i = 0; i < total; ++i)
    {
        head = f.driveRandomStep(rng, head);
        // Only the two invariants that hold under every generated operation: the
        // number<->hash bijection over the canonical range, and nothing above the head.
        checkNumberHashAgreement(f.storage, head);
        checkNoRowsAboveHead(f.storage, head);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace op_matrix
