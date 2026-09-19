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
 * @file OpForkNegativeCoverageTest.cpp
 * @brief Per-fork negative-surface cells that had no coverage (F8, WI-18).
 */

#include "support/OpEngineKarstTestHarness.h"

#include <bcos-ledger/LedgerMethods.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;
using namespace op_engine_parity_test;

// fork × negative-surface coverage (measured; only the two GAPs below are new):
//   方法窗外版本 → UnsupportedFork : OpEngineApiVersionsTest.cpp:83/128/385/429,
//                                    OpEngineServiceParityTest.cpp:102/394/1083,
//                                    EngineServiceTest.cpp:412/436/457/1812
//   错误码路由(−32603/Invalid/Syncing) : OpEngineServiceParityTest.cpp:671/972/1029/1149/1303/1364
//   deposits-only : OpJovianShapeTest.cpp:57(c), PreBlockOpStepsTest.cpp:274/662/731,
//                   OpL1BlockDepositTest.cpp:750   (Jovian+Karst only; pre-Jovian not applicable)
//   latestValidHash : OpEngineServiceParityTest.cpp:431/638/1149,
//   OpEngineImportFcuTest.cpp:52/90/527,
//                     OpEngineServiceExecParityTest.cpp:347/392,
//                     OpNewPayloadRpcE2eTest.cpp:644/656/1323
//   GAP 1 stale head (OP)  — this file
//   GAP 1 note: extends OpEngineImportFcuTest.cpp:450 (OldCanonicalHeadDoesNotRewindLatest)
//     with the latestValidHash-echo pin, tip-hash identity at height 3, and the heartbeat
//     re-win step.
//   GAP 2 多字段首错顺序 (OP) — this file
//
// SCOPE OF THIS TABLE (do not read it as exhaustive):
//   * four surfaces only — code routing, first-error order, deposits-only, latestValidHash.
//     Not swept: blob versioned-hash handling, extraData shape, deposit tolerance, DA gates.
//   * the code-routing and latestValidHash rows are measured on Isthmus/default only
//     (the OP cases listed are Isthmus-timestamped); no per-fork sweep exists for them.
//   * "covered" describes timestamp classification: a listed case runs with the fork's
//     timestamp, it is not a per-fork × per-surface cross product.

BOOST_AUTO_TEST_SUITE(OpForkNegativeCoverageSuite)

/// GAP 1: same semantics as the Eth-side forkchoice_ignores_stale_update_after_newer_head_wins
/// (EngineServiceTest.cpp:677) — a later FCU pointing at an older head must not rewind the tip.
BOOST_AUTO_TEST_CASE(OPStaleHeadForkchoiceDoesNotRewindTheTip)
{
    ImportServiceFixture f;
    // ① seed A(1)-B(2)-C(3) (the fixture's FCU already lands on C) and re-issue the
    //    tip heartbeat: the "newer head wins" step of the Eth-side three-step shape.
    f.seedCanonicalChainABC();
    auto const cHash = f.seededChainHash[3];
    bcos::engine::ForkchoiceState tipFcu{cHash, cHash, fixtureHeadHash()};
    auto tip = bcos::task::syncWait(f.service.updateForkchoice(tipFcu, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(tip.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // ② stale FCU back to A(1): op-geth answers VALID (the old head is a known valid
    //    canonical ancestor); it must be ignored, not applied as a rewind.
    auto const aHash = f.seededChainHash[1];
    bcos::engine::ForkchoiceState staleFcu{aHash, aHash, fixtureHeadHash()};
    auto stale = bcos::task::syncWait(f.service.updateForkchoice(staleFcu, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(stale.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK(!stale.payloadId.has_value());
    // latestValidHash leaves no rewind trace: the Valid path echoes the REQUESTED head
    // (OpEngineService.inl updateForkchoice), never a rewound tip pointer.
    BOOST_REQUIRE(stale.payloadStatus.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*stale.payloadStatus.latestValidHash, aHash);

    // ③ the tip itself did not move: the canonical tip is still C@3 in storage.
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        3);
    auto tipHashAfter =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 3, bcos::ledger::fromStorage));
    BOOST_REQUIRE(tipHashAfter.has_value());
    BOOST_CHECK_EQUAL(tipHashAfter->hex(), cHash.hex());

    // ④ the newer head still wins after the stale poke (the Eth case's third FCU).
    auto again = bcos::task::syncWait(f.service.updateForkchoice(tipFcu, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(again.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

/// GAP 2: the first-error order contract (OpEngineService.cpp:374-404,
/// validateOpNewPayloadRequest) was comment-only on the OP path; pin it by malforming TWO
/// fields and asserting the message names the FIRST one.
BOOST_AUTO_TEST_CASE(OPFirstErrorOrderPrefersTransactionsOverHeaderFields)
{
    ImportServiceFixture f;
    auto request = f.validRequest(fixtureHeadHash(), 1);

    // Two simultaneous violations: the transactions branch (order-table slot 1) sees an
    // emptied raw envelope, and the windowFields branch (a LATER slot) sees the missing
    // parentBeaconBlockRoot. The FIRST branch must own the rejection message.
    request.executionPayload.transactions.front().raw.clear();
    request.parentBeaconBlockRoot = std::nullopt;

    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_MESSAGE(status.validationError->find("executionPayload.transactions[0] is empty") !=
                            std::string::npos,
        "first error must name the transactions branch, got: " << *status.validationError);
    BOOST_CHECK_MESSAGE(status.validationError->find("parentBeaconBlockRoot must be a 32-byte "
                                                     "hash") == std::string::npos,
        "the later windowFields branch must not preempt the transactions branch, got: "
            << *status.validationError);
}

BOOST_AUTO_TEST_SUITE_END()
