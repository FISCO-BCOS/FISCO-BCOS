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
 * @file OpEngineImportFcuTest.cpp
 * @brief S5+S6 handshake matrix: newPayload = import-without-setHead, FCU = pin
 * three-branch + atomic SetCanonical. Drives the REAL OpScheduler through the
 * SchedulerInterface seam against an in-memory MLS (no ledger: execute-only —
 * imports never touch prewriteBlockToBuffer).
 */

#include "support/OpEngineKarstTestHarness.h"

#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <support/SeedPreState.h>

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <limits>
#include <ranges>

using namespace bcos;
using namespace bcos::engine;
using namespace op_engine_parity_test;
using bcos::evm::opstack::encodeDepositEnvelope;
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using namespace evmc::literals;
using bcos::executor_v1::opstack::decodeDepositEnvelope;

BOOST_AUTO_TEST_SUITE(OpEngineImportFcuTest)

// ---- S5 Task 4: service-level newPayload import semantics ----

// newPayload 扩展 canonical 父：VALID；latest / SYS_CURRENT_STATE / tracker 不动；
// 块落在 ImportedStore；同哈希重放幂等 VALID。
BOOST_AUTO_TEST_CASE(NewPayloadExtendsParentLeavesLatestUnchanged)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    auto const blockHash = request.executionPayload.blockHash;
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), blockHash.hex());

    // latest / committed plane unchanged.
    auto view = f.storage.forkCommitted();
    auto const tip =
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
    BOOST_CHECK_EQUAL(tip, 0);
    // tracker head unchanged (safe == 0 from the genesis FCU).
    BOOST_REQUIRE(f.service.getSafeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 0);
    // the block landed in the ImportedStore
    BOOST_CHECK(f.service.hasImportedBlock(blockHash));

    // idempotent replay: VALID, no re-execution fault.
    auto replay = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(replay.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

// 缺父（规范表与 ImportedStore 都没有）→ 只 SYNCING，无 ACCEPTED。
BOOST_AUTO_TEST_CASE(MissingParentIsSyncing)
{
    ImportServiceFixture f;
    auto request = f.validRequest(bcos::h256(0xdeadbeef), 1);
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// 执行根不一致 → INVALID + parent；不落 ImportedStore；随后 FCU(该哈希) → SYNCING。
BOOST_AUTO_TEST_CASE(BadStateRootIsInvalidAndNotStored)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    request.executionPayload.stateRoot = bcos::h256(std::string(64, '9'));
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        f.blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), fixtureHeadHash().hex());
    BOOST_CHECK(!f.service.hasImportedBlock(request.executionPayload.blockHash));

    // FCU to the never-stored hash: SYNCING (unknown head).
    bcos::engine::ForkchoiceState bad{
        request.executionPayload.blockHash, request.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(bad, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// S5 Task 3: importExecute executes on the PARENT's post-state (delta chain stacked
// over the committed flat) WITHOUT any canonical-table write, and consecutive imports
// never trip the linear pending-slot gates (no RefuseOtherHeight — the pending slot
// is not importExecute's business).
BOOST_AUTO_TEST_CASE(ImportExecuteStacksParentDeltasWithoutCanonicalWrites)
{
    ImportSchedulerFixture f;

    // B1: parent = genesis (empty delta chain).
    auto b1 = f.depositBlock(1, bcos::h256{}, 1'000'000, "import-b1");
    std::shared_ptr<void> delta1;
    bcos::protocol::BlockHeader::Ptr header1;
    f.scheduler->importExecute(b1, {}, {},
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void> delta,
            std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header1 = std::move(header);
            delta1 = std::move(delta);
        });
    BOOST_REQUIRE(header1 != nullptr);
    BOOST_REQUIRE(delta1 != nullptr);
    auto const b1Hash = bcos::protocol::EthBlockHeader::computeHash(*header1);
    // Receipts ride on the block for the later canonical prewrite (Task 5).
    BOOST_CHECK_EQUAL(b1->receiptsSize(), b1->transactionsSize());

    // The canonical plane is untouched after B1.
    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 0);
    BOOST_CHECK(!committedHashAt(f.storage, 1).has_value());

    // B2: parent = B1, delta chain {delta1}. Must NOT be RefuseOtherHeight.
    auto b2 = f.depositBlock(2, b1Hash, 1'000'012, "import-b2");
    bcos::protocol::BlockHeader::Ptr header2;
    f.scheduler->importExecute(b2, {}, delta1,
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>,
            std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header2 = std::move(header);
        });
    BOOST_REQUIRE(header2 != nullptr);
    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 0);
    BOOST_CHECK(!committedHashAt(f.storage, 2).has_value());

    // Control: the SAME B2 executed on the genesis plane (no parent delta) must
    // produce a DIFFERENT stateRoot — proving the stacked run really executed on
    // B1's post-state instead of the committed flat.
    auto b2GenesisPlane = f.depositBlock(2, b1Hash, 1'000'012, "import-b2");
    bcos::protocol::BlockHeader::Ptr header2Genesis;
    f.scheduler->importExecute(b2GenesisPlane, {}, nullptr,
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>,
            std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header2Genesis = std::move(header);
        });
    BOOST_REQUIRE(header2Genesis != nullptr);
    BOOST_CHECK(header2->stateRoot() != header2Genesis->stateRoot());
}

// ---- S5 Task 5: FCU 认 imported 哈希 + 原子 SetCanonical ----

// newPayload(B1) 后 latest 仍 G；FCU(head=B1) 一次 VALID：SYS_CURRENT_STATE 推到 1、
// NUMBER_2_HASH[1]==B1、tracker head==B1、world stateRoot == B1.stateRoot。
BOOST_AUTO_TEST_CASE(FcuToImportedTipCanonicalizes)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    auto const blockHash = request.executionPayload.blockHash;
    auto const importedStateRoot = request.executionPayload.stateRoot;
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // latest 仍 G：committed tip 0，tracker safe 0。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 0);

    bcos::engine::ForkchoiceState forkchoice{blockHash, blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // SYS_CURRENT_STATE == 1；NUMBER_2_HASH[1] == B1。
    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        1);
    auto canonicalHash = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 1, bcos::ledger::fromStorage));
    BOOST_REQUIRE(canonicalHash.has_value());
    BOOST_CHECK_EQUAL(canonicalHash->hex(), blockHash.hex());

    // tracker head == B1（safe 推到 1）。
    BOOST_REQUIRE(f.service.getSafeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 1);

    // world stateRoot == B1.stateRoot。
    bcos::evm::evmstate::Storage2State<ViewType> state(canonicalView);
    auto const root = bcos::evm::stateRootOf(state);
    BOOST_CHECK_EQUAL(bcos::h256(root.bytes, 32).hex(), importedStateRoot.hex());
}

// FCU 未知头（规范表与 ImportedStore 都没有）→ SYNCING，无 payloadId。
BOOST_AUTO_TEST_CASE(FcuUnknownHeadIsSyncing)
{
    ImportServiceFixture f;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256(0xfeedbeef), bcos::h256(0xfeedbeef), fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!fcu.payloadId.has_value());
}

// ---- S5 Task 6: 连续 import ×3 + 一次跳号 FCU（设计 §5）----

// B1/B2/B3 连续 newPayload（latest 始终 G），B3 的用户 deposit 执行 BLOCKHASH(1)；
// FCU(B3) 一次 VALID（不先 FCU(B1)/(B2)）。BLOCKHASH 必须沿 payload parent 链回走
// （§4.4.3），否则 B1 的哈希读成 0。
BOOST_AUTO_TEST_CASE(ThreeImportsThenJumpFcu)
{
    ImportServiceFixture f;

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    auto status1 = bcos::task::syncWait(f.service.newPayload(request1, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status1.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    auto status2 = bcos::task::syncWait(f.service.newPayload(request2, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status2.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B2 status " << static_cast<int>(status2.status)
                     << " err=" << status2.validationError.value_or("<none>"));

    // B3 的承诺必须来自「在 B2 后状态上执行」——探针在同一个平面链上算出期望根，
    // 服务端 VALID 要求真实导入逐位一致：在错误平面上执行会因承诺不符被 INVALID。
    // 平面本身由 ChainedImportMatchesCanonicalParentState 用独立口径（canonicalize
    // 之后的后端状态）钉住。设计 §5 的 BLOCKHASH 行由下面 RecentBlockHashes 的真实
    // 读路径断言（不是「播种行存在」的替代口径，review N4）。
    auto request3 = f.validRequest(request2.executionPayload.blockHash, 3);
    f.fillCommitmentsFromProbe(request3);

    auto status3 = bcos::task::syncWait(f.service.newPayload(request3, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status3.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B3 status " << static_cast<int>(status3.status)
                     << " err=" << status3.validationError.value_or("<none>"));

    // 设计 §5：第三块交易读 BLOCKHASH(第 1 块高度) ≠ 0。走真实读路径
    // RecentBlockHashes（op-geth GetHashFn）：构造函数播种 {N-1: parentHash}，更早
    // 祖先按需从导入视图的 SYS_NUMBER_2_HASH 读。仅断言「播种的行存在且 32 字节」是
    // 自洽替代，不能证明读回的就是 B1（review N4）。
    {
        auto& probeDeltaStorage = *std::static_pointer_cast<MutableStorage>(f.deltaByNumber[3]);
        evmc::bytes32 parentSeed{};
        std::memcpy(
            parentSeed.bytes, request2.executionPayload.blockHash.data(), sizeof(parentSeed.bytes));
        std::optional<std::string> hashError;
        bcos::evm::engine::detail::RecentBlockHashes<MutableStorage> recentHashes(
            probeDeltaStorage, /*blockNumber=*/3, parentSeed, &hashError);
        auto const b1 = recentHashes.get_block_hash(1);
        BOOST_CHECK_MESSAGE(
            !hashError.has_value(), "RecentBlockHashes poisoned: " << hashError.value_or(""));
        auto const b1Hash = bcos::h256(b1.bytes, sizeof(b1.bytes));
        BOOST_CHECK(b1Hash != bcos::h256{});
        BOOST_CHECK_EQUAL(b1Hash.hex(), request1.executionPayload.blockHash.hex());
        // The direct parent is the constructor's zero-read seed.
        auto const b2 = recentHashes.get_block_hash(2);
        BOOST_CHECK_EQUAL(bcos::h256(b2.bytes, sizeof(b2.bytes)).hex(),
            request2.executionPayload.blockHash.hex());
    }

    // latest 仍 G（committed tip 0）。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);

    // 一次跳号 FCU（不先 FCU(B1)/(B2)）。
    bcos::engine::ForkchoiceState forkchoice{request3.executionPayload.blockHash,
        request3.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // latest = B3。
    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        3);
    auto tipHash = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 3, bcos::ledger::fromStorage));
    BOOST_REQUIRE(tipHash.has_value());
    BOOST_CHECK_EQUAL(tipHash->hex(), request3.executionPayload.blockHash.hex());
}
// ---- S5 Task 7: 规范祖先 sibling（op-node L1 再重组，设计 §4.3）----

// 链 A-B-C 已 FCU（tip=C）。newPayload(B', parent=A)：
// VALID；latest 仍 C；高度 2 仍读 B；B' 已落 store。
BOOST_AUTO_TEST_CASE(AncestorSiblingWhileTipStillAhead)
{
    ImportServiceFixture f;

    auto requestA = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestA, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestB = f.validRequest(requestA.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestB, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestC = f.validRequest(requestB.executionPayload.blockHash, 3);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestC, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // 一次跳号 FCU 到 C。
    bcos::engine::ForkchoiceState fcuC{requestC.executionPayload.blockHash,
        requestC.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuC, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 3);

    // B'：parent=A，高度 2（B 的同高 sibling），时间戳不同 → 哈希不同。
    auto requestBPrime = f.validRequest(requestA.executionPayload.blockHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto statusBPrime = bcos::task::syncWait(f.service.newPayload(requestBPrime, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusBPrime.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B' status " << static_cast<int>(statusBPrime.status)
                     << " err=" << statusBPrime.validationError.value_or("<none>")
                     << " probeRoot=" << requestBPrime.executionPayload.receiptsRoot.hex()
                     << " cRoot=" << requestC.executionPayload.receiptsRoot.hex()
                     << " executedRoot="
                     << (f.service.lastExecutedHeader() ?
                                f.service.lastExecutedHeader()->receiptsRoot().hex() :
                                "<null>"));

    // latest 仍是 C；高度 2 仍读 B；B' 在 store。
    {
        auto view = f.storage.forkCommitted();
        BOOST_CHECK_EQUAL(bcos::task::syncWait(
                              bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
            3);
        auto height2 =
            bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
        BOOST_REQUIRE(height2.has_value());
        BOOST_CHECK_EQUAL(height2->hex(), requestB.executionPayload.blockHash.hex());
    }
    BOOST_CHECK(f.service.hasImportedBlock(requestBPrime.executionPayload.blockHash));

    // FCU(head=B')：SetCanonical 换头；latest=B'；高度 2 改写；高度 3 不再规范。
    bcos::engine::ForkchoiceState fcuBPrime{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        2);
    auto height2New = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2New.has_value());
    BOOST_CHECK_EQUAL(height2New->hex(), requestBPrime.executionPayload.blockHash.hex());
    auto height3 = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 3, bcos::ledger::fromStorage));
    BOOST_CHECK(!height3.has_value());
    // 旧哈希仍是旧块（按哈希读不随规范标签变）：B 与 C 都还在 ImportedStore。
    BOOST_CHECK(f.service.hasImportedBlock(requestB.executionPayload.blockHash));
    BOOST_CHECK(f.service.hasImportedBlock(requestC.executionPayload.blockHash));
    BOOST_CHECK(f.service.hasImportedBlock(requestBPrime.executionPayload.blockHash));
}

// 已 import 的同高覆盖（该槽已有子孙）→ SYNCING（不冲掉 parent）。
BOOST_AUTO_TEST_CASE(ImportedOverwriteWithDescendantsIsSyncing)
{
    ImportServiceFixture f;

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // B1'：parent=G，高度 1（B1 已有 imported 子孙 B2）。
    auto request1Prime = f.validRequest(fixtureHeadHash(), 1);
    request1Prime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request1Prime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), request1Prime.executionPayload, txRoot,
            *request1Prime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        request1Prime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto status = bcos::task::syncWait(f.service.newPayload(request1Prime, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// ---- S5 Task 8: old-head 与 RebuildOnParent 边界 ----

// 链 A-B-C 已 FCU。FCU(head=A) 无 attrs：VALID；latest 不变矮（仍 C=3）；
// safe 可更新（op-geth Optimism old-head：不 ignore，也不 rewind Current）。
BOOST_AUTO_TEST_CASE(OldCanonicalHeadDoesNotRewindLatest)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();

    auto const aHash = bcos::protocol::EthBlockHeader::computeHash(*f.executedByNumber[1]);
    bcos::engine::ForkchoiceState oldHead{aHash, aHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(oldHead, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK(!fcu.payloadId.has_value());

    // latest 仍为 C=3；safe 推到 1。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        3);
    BOOST_REQUIRE(f.service.getSafeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 1);
}

// B' 已 import（非规范）。FCU(head=B', attrs)：先 SetCanonical(B')（latest=B'），
// 再在 B' 上造块；build 的 parent 必须按 B' 哈希读（NUMBER_2_BLOCK_HEADER[2]=B'）。
BOOST_AUTO_TEST_CASE(FcuToNonCanonicalWithAttrsSetsCanonicalFirst)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();

    // 导入 B'（parent=A，时间戳偏移 → 哈希不同）。
    auto requestBPrime =
        f.validRequest(bcos::protocol::EthBlockHeader::computeHash(*f.executedByNumber[1]), 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // SetCanonical 禁止 RebuildOnParent 冒充：attrs 存在也必须先换头。
    bcos::engine::ForkchoiceState fcu{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    auto attrs = makeOpPayloadAttributesAt(1'700'000'000'000ULL + 5 * 12'000ULL);
    attrs.minBaseFee = std::nullopt;  // pre-Jovian: minBaseFee must be unset
    auto const deposit = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
    attrs.transactions = std::vector<std::string>{bcos::toHexStringWithPrefix(deposit)};
    auto fcuResult = bcos::task::syncWait(f.service.updateForkchoice(fcu, &attrs, 3));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(fcuResult.payloadStatus.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "FCU(B',attrs) status " << static_cast<int>(fcuResult.payloadStatus.status) << " err="
                                << fcuResult.payloadStatus.validationError.value_or("<none>"));
    BOOST_REQUIRE(fcuResult.payloadId.has_value());

    // latest 已是 B'（先 SetCanonical 再造块）。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        2);
    auto tipHash =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(tipHash.has_value());
    BOOST_CHECK_EQUAL(tipHash->hex(), requestBPrime.executionPayload.blockHash.hex());

    // build 的 parent 头按 B' 哈希取：产出的 payload parentHash 必须是 B'。
    auto payload = bcos::task::syncWait(f.service.getPayload(*fcuResult.payloadId, 4));
    BOOST_REQUIRE(payload);
    BOOST_CHECK_EQUAL(
        payload->executionPayload.parentHash.hex(), requestBPrime.executionPayload.blockHash.hex());
}

// ---- S5 Task 9: §5 矩阵剩余行 ----

// 形状错误 → INVALID 且 latestValidHash=null（对齐 pin 组块失败）。
BOOST_AUTO_TEST_CASE(ShapeErrorCarriesNullLatestValidHash)
{
    ImportServiceFixture f;
    auto request = f.validRequest(fixtureHeadHash(), 1);
    request.executionRequests = std::nullopt;  // Isthmus wire contract violated
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!status.latestValidHash.has_value());
}

// newPayload 后立刻读 "safe"|"finalized"：与 FCU 前相同（导入不碰标签）。
BOOST_AUTO_TEST_CASE(NewPayloadLeavesSafeAndFinalizedUnchanged)
{
    ImportServiceFixture f;
    auto safeBefore = f.service.getSafeBlockNumber();
    auto finalizedBefore = f.service.getFinalizedBlockNumber();

    auto request = f.validRequest(fixtureHeadHash(), 1);
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto safeAfter = f.service.getSafeBlockNumber();
    auto finalizedAfter = f.service.getFinalizedBlockNumber();
    BOOST_CHECK_EQUAL(safeAfter.has_value(), safeBefore.has_value());
    BOOST_CHECK_EQUAL(finalizedAfter.has_value(), finalizedBefore.has_value());
    if (safeBefore.has_value())
        BOOST_CHECK_EQUAL(*safeAfter, *safeBefore);
    if (finalizedBefore.has_value())
        BOOST_CHECK_EQUAL(*finalizedAfter, *finalizedBefore);
}

// 心跳：同 tip、无 attrs → VALID 且无 payloadId。
BOOST_AUTO_TEST_CASE(HeartbeatSameTipNoAttrsIsValidWithoutPayloadId)
{
    ImportServiceFixture f;
    bcos::engine::ForkchoiceState fcu{fixtureHeadHash(), fixtureHeadHash(), fixtureHeadHash()};
    auto first = bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(first.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK(!first.payloadId.has_value());

    // 第二次同 tip 心跳：仍 VALID、仍无 payloadId。
    auto second = bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(second.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK(!second.payloadId.has_value());
}

// ---- review fixes (R1) ----

// F1 regression, service level with an INDEPENDENT oracle: the root the engine
// accepts for a chained import must equal the root produced by executing the same
// block on the canonicalized parent state (materialized by canonicalize itself, not
// by the import's own plane machinery). Before the fix the import executed on a
// stale committed plane, so the two roots differed.
BOOST_AUTO_TEST_CASE(ChainedImportMatchesCanonicalParentState)
{
    ImportServiceFixture f;

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto const acceptedRoot = request2.executionPayload.stateRoot;

    // Canonicalize B1: the committed plane becomes B1's post-state.
    bcos::engine::ForkchoiceState fcuB1{request1.executionPayload.blockHash,
        request1.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuB1, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Independent oracle: execute B2's transactions on that plane (the committed
    // plane IS B1's post-state now) and compare the world roots.
    auto onParentPlane = f.probeImport(request2.executionPayload.blockHash, 2,
        request2.executionPayload.timestamp / 1000, /*plane=*/nullptr);
    BOOST_REQUIRE(onParentPlane.header != nullptr);
    BOOST_CHECK_EQUAL(onParentPlane.header->stateRoot().hex(), acceptedRoot.hex());
}

// N1 regression: an imported block that FCU makes canonical must carry the by-number
// transaction list. ledger::getBlockData reads SYS_NUMBER_2_TXS[number] and resolves
// SYS_HASH_2_TX through it; the canonicalize branches wrote only the hash-keyed bodies,
// so eth_getBlockByNumber returned the block with no transactions after SetCanonical.
BOOST_AUTO_TEST_CASE(CanonicalImportedBlockHasNumberToTxsRow)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcu{
        request.executionPayload.blockHash, request.executionPayload.blockHash, fixtureHeadHash()};
    auto canonical = bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(canonical.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto view = f.storage.forkCommitted();
    auto block = bcos::task::syncWait(bcos::ledger::getBlockData(
        view, 1, bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS, *f.blockFactory));
    BOOST_REQUIRE(block != nullptr);
    // The canonicalized block's deposit transaction must be retrievable by number.
    BOOST_CHECK_EQUAL(block->transactionsSize(), 1U);
}

// N2 regression: a switch must not scrub the still-canonical ancestors' metadata rows.
// The switch's "delete every backend row absent from the head flat" treated the state
// plane as the whole backend, deleting A's SYS_HASH_2_NUMBER / hash-keyed rows.
BOOST_AUTO_TEST_CASE(SwitchKeepsCanonicalAncestorLedgerRows)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = bcos::protocol::EthBlockHeader::computeHash(*f.executedByNumber[1]);

    {
        auto pre = f.storage.forkCommitted();
        auto preA = bcos::task::syncWait(
            bcos::ledger::getBlockNumber(pre, aHash, bcos::ledger::fromStorage));
        BOOST_REQUIRE(preA.has_value());
        BOOST_CHECK_EQUAL(*preA, 1);
    }

    // B' sibling of B, parent A.
    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcuBPrime{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto view = f.storage.forkCommitted();
    // A stays canonical at height 1: its hash->number row must survive the switch.
    auto aNumber =
        bcos::task::syncWait(bcos::ledger::getBlockNumber(view, aHash, bcos::ledger::fromStorage));
    BOOST_REQUIRE(aNumber.has_value());
    BOOST_CHECK_EQUAL(*aNumber, 1);
    auto height1 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 1, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height1.has_value());
    BOOST_CHECK_EQUAL(height1->hex(), aHash.hex());
    // Above the new head the number mapping is gone.
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 3, bcos::ledger::fromStorage))
             .has_value());
}

// NEW-1 regression: the switch height trim starts at head.number + 1, so a same-height
// replacement left the replaced canonical block B's SYS_HASH_2_NUMBER alive. Because
// EthEndpoint's by-hash lookup is hash→number→by-number, eth_getBlockByHash(B) then
// silently returned the NEW head B'; handleOpNewPayload likewise mis-classified the
// orphan as the canonical tip. The ledger must drop B when A-B-C switches to B'@2.
BOOST_AUTO_TEST_CASE(SwitchDropsReplacedSameHeightSiblingLedgerRows)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const bHash = f.seededChainHash[2];

    // Precondition: B is canonical at height 2 before the switch.
    {
        auto pre = f.storage.forkCommitted();
        auto preB = bcos::task::syncWait(
            bcos::ledger::getBlockNumber(pre, bHash, bcos::ledger::fromStorage));
        BOOST_REQUIRE(preB.has_value());
        BOOST_CHECK_EQUAL(*preB, 2);
    }

    // B' sibling of B, parent A.
    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE(bPrimeHash != bHash);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto view = f.storage.forkCommitted();
    // The new head resolves at the same height.
    auto newHeadNumber = bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage));
    BOOST_REQUIRE(newHeadNumber.has_value());
    BOOST_CHECK_EQUAL(*newHeadNumber, 2);
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), bPrimeHash.hex());
    // The REPLACED sibling must no longer resolve: its hash->number row is gone (this
    // is exactly the ledger equivalent of eth_getBlockByHash no longer finding it).
    BOOST_CHECK_MESSAGE(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, bHash, bcos::ledger::fromStorage))
             .has_value(),
        "replaced same-height sibling still resolves by hash to the new head");
}

// NEW-3 regression (delta round 3, Part C): on the production composition the MLS
// carries a process-lifetime cache layer that the forward canonicalize warms via
// mergeToBackends, while the switch branch wrote only m_latestBackend. fork()/
// forkCommitted() read the cache first, so after a same-height switch the committed
// view still served the OLD plane (NUMBER_2_HASH[2]==B, SYS_CURRENT_STATE==3) and the
// state-root post-condition — which reads through forkCommitted() — could never see
// the new head's world state. The switch must reach the cache layer too.
BOOST_AUTO_TEST_CASE(SwitchOnWarmCacheServesNewPlane)
{
    CacheImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const bHash = f.seededChainHash[2];

    // B' sibling of B, parent A (same construction as the ledger-row regression above).
    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // The COMMITTED view reads the cache first; it must serve the NEW plane.
    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 2);
    auto height2 = committedHashAt(f.storage, 2);
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_MESSAGE(height2->hex() == bPrimeHash.hex(),
        "committed NUMBER_2_HASH[2] still resolves to the replaced sibling");
    BOOST_CHECK(!committedHashAt(f.storage, 3).has_value());
    auto view = f.storage.forkCommitted();
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, bHash, bcos::ledger::fromStorage))
             .has_value());

    // And the CACHE LAYER ITSELF holds the switch's rows — written from the same commit
    // as the backend, not merely failing to shadow them — while the de-canonicalized
    // rows are gone from it.
    auto readCacheBytes = [&](executor_v1::StateKey key) -> std::optional<bcos::bytes> {
        auto entry = bcos::task::syncWait(bcos::storage2::readOne(f.cache, std::move(key)));
        if (!entry.has_value())
        {
            return std::nullopt;
        }
        auto const value = entry->get();
        return bcos::bytes(value.begin(), value.end());
    };
    auto const number2Hash =
        readCacheBytes(StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(2)});
    BOOST_REQUIRE(number2Hash.has_value());
    BOOST_CHECK(*number2Hash == bcos::bytes(bPrimeHash.begin(), bPrimeHash.end()));
    auto const currentCache = readCacheBytes(
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER});
    BOOST_REQUIRE(currentCache.has_value());
    BOOST_CHECK(bcos::toHexStringWithPrefix(*currentCache) == "0x32");  // "2"
    auto const primeHashNumber = readCacheBytes(
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(bPrimeHash)});
    BOOST_REQUIRE(primeHashNumber.has_value());
    BOOST_CHECK(bcos::toHexStringWithPrefix(*primeHashNumber) == "0x32");  // "2"
    BOOST_CHECK_MESSAGE(!readCacheBytes(StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                            bcos::concepts::bytebuffer::toView(bHash)})
                             .has_value(),
        "cache still holds the replaced sibling B's hash->number row");
    BOOST_CHECK_MESSAGE(
        !readCacheBytes(StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(3)}).has_value(),
        "cache still holds the trimmed height-3 number->hash row");
}

// NEW-2 regression (delta round 3): the canonical-chain walk classified a chain by
// height alone, so a re-org BACK onto an abandoned branch whose head is at/above the
// current tip took the forward branch and merged the block's delta onto the WRONG
// plane (the post-condition then rejected it with an opaque error). A chain whose
// canonical root is not the current tip is a switch, whatever the head's height.
BOOST_AUTO_TEST_CASE(SwitchBackToAbandonedSiblingBranch)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const bHash = f.seededChainHash[2];

    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Back-reorg onto the abandoned sibling at the SAME height (its flat survives the
    // prune; C@3's would not). The walk roots at A, not at the tip B', so this must be
    // a switch, not a forward merge onto B''s plane.
    bcos::engine::ForkchoiceState fcuBack{bHash, bHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBack, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 2);
    auto height2 = committedHashAt(f.storage, 2);
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_MESSAGE(height2->hex() == bHash.hex(),
        "back-reorg did not restore the abandoned branch as canonical");
    auto view = f.storage.forkCommitted();
    BOOST_CHECK(!bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage))
                     .has_value());
}

// N3 regression: after a switch, the orphaned old-chain occupant must stop occupying
// its height, or the next legal import there answers SYNCING forever (the old occupant
// is no longer canonical, so the caller's occupantCanonical gate rejects it).
BOOST_AUTO_TEST_CASE(SwitchClearsOrphanedOccupantForNextImport)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = bcos::protocol::EthBlockHeader::computeHash(*f.executedByNumber[1]);

    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuBPrime{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Old-chain C@3 is now orphaned; the next legal payload extends the new tip B'@2.
    auto request3 = f.validRequest(requestBPrime.executionPayload.blockHash, 3);
    auto status = bcos::task::syncWait(f.service.newPayload(request3, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "orphan occupant blocked import: status=" << static_cast<int>(status.status) << " err="
                                                  << status.validationError.value_or("<none>"));
    BOOST_CHECK(f.service.hasImportedBlock(request3.executionPayload.blockHash));
}

// N3 regression (multi-orphan): the single-orphan case above only passes because the
// de-canonicalized occupant has no stored child. With A-B-C-D and a switch to B'@2, the
// orphan C@3 KEEPS its child D@4 in m_blocks; put()'s descendant guard then still saw a
// live conflict at height 3 and answered SYNCING forever, because adoptCanonicalHead
// erased only m_byNumber. The guard must consult liveness (detached blocks skipped).
BOOST_AUTO_TEST_CASE(SwitchClearsMultiOrphanAboveHeadForNextImport)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();  // tip C@3
    auto const aHash = f.seededChainHash[1];
    auto const cHash = f.seededChainHash[3];

    // D@4: a second old-chain block above the future head, so C has a stored child.
    auto requestD = f.validRequest(cHash, 4);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestD, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuBPrime{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // C@3 (child D@4) is orphaned; the next legal payload extends the new tip B'@2.
    auto request3 = f.validRequest(requestBPrime.executionPayload.blockHash, 3);
    auto status = bcos::task::syncWait(f.service.newPayload(request3, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "multi-orphan descendant blocked import: status="
            << static_cast<int>(status.status)
            << " err=" << status.validationError.value_or("<none>"));
    BOOST_CHECK(f.service.hasImportedBlock(request3.executionPayload.blockHash));
}

// F3 regression: canonicalize is one atomic batch. A mid-chain failure must leave the
// backend observably as it was before the call (design §4.2: 失败则全部回到调用前), not
// half-written: B1's rows merge first, then B2's delta is gone.
BOOST_AUTO_TEST_CASE(CanonicalizeRollsBackOnMidChainMergeFailure)
{
    ImportServiceFixture f(/*stripImportDeltaAt=*/2);

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcu{request2.executionPayload.blockHash,
        request2.executionPayload.blockHash, fixtureHeadHash()};
    bool canonicalizeThrew = false;
    try
    {
        (void)bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    }
    catch (std::exception const&)
    {
        canonicalizeThrew = true;
    }
    BOOST_CHECK_MESSAGE(canonicalizeThrew, "a half-merged canonicalize must not answer VALID");

    // The failure must leave the committed plane exactly as it was (genesis only).
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 1, bcos::ledger::fromStorage))
             .has_value());
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage))
             .has_value());
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view,
                                  request1.executionPayload.blockHash, bcos::ledger::fromStorage))
             .has_value());
}

// F3 regression, CACHE-ENABLED composition: the production OP GlobalStateStorage is
// MultiLayerStorage<Mutable, Cache, RocksDBCheckpoint> (GlobalStateStorageInitializer.h),
// and mergeToBackends writes BOTH backend and cache while fork()/forkCommitted() read the
// cache first. The cache-less MLS above cannot see a backend-only rollback, so this case
// binds the production-shaped CacheMLS and asserts the failed mid-chain batch left BOTH
// layers at their pre-call values (review F3 / NEW-2).
BOOST_AUTO_TEST_CASE(CanonicalizeRollsBackCacheLayerOnMidChainMergeFailure)
{
    CacheImportServiceFixture f(/*stripImportDeltaAt=*/2);

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Snapshot the cache layer BEFORE the doomed canonicalize (the seed merges populate
    // it, e.g. SYS_CURRENT_STATE="0"), then require it byte-identical after the rollback.
    auto readCacheBytes = [&](executor_v1::StateKey key) -> std::optional<bcos::bytes> {
        auto entry = bcos::task::syncWait(bcos::storage2::readOne(f.cache, std::move(key)));
        if (!entry.has_value())
        {
            return std::nullopt;
        }
        auto const value = entry->get();
        return bcos::bytes(value.begin(), value.end());
    };
    auto const numberHashKey = StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(1)};
    auto const b1NumberKey = StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
        bcos::concepts::bytebuffer::toView(request1.executionPayload.blockHash)};
    auto const currentKey =
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER};
    auto const preNumberHash = readCacheBytes(numberHashKey);
    auto const preB1Number = readCacheBytes(b1NumberKey);
    auto const preCurrent = readCacheBytes(currentKey);
    // Pre-call the failed batch's new rows are absent (only the seeded tip pointer exists).
    BOOST_REQUIRE(!preNumberHash.has_value());
    BOOST_REQUIRE(!preB1Number.has_value());
    BOOST_REQUIRE(preCurrent.has_value());

    bcos::engine::ForkchoiceState fcu{request2.executionPayload.blockHash,
        request2.executionPayload.blockHash, fixtureHeadHash()};
    bool canonicalizeThrew = false;
    try
    {
        (void)bcos::task::syncWait(f.service.updateForkchoice(fcu, nullptr, 3));
    }
    catch (std::exception const&)
    {
        canonicalizeThrew = true;
    }
    BOOST_CHECK_MESSAGE(canonicalizeThrew, "a half-merged canonicalize must not answer VALID");

    // Cache layer directly: the failed batch's B1 canonical rows were merged into the
    // cache before the B2 failure, so they must have been rolled back there too.
    BOOST_CHECK_MESSAGE(!readCacheBytes(numberHashKey).has_value(),
        "cache still holds SYS_NUMBER_2_HASH[1] after the rollback");
    BOOST_CHECK_MESSAGE(!readCacheBytes(b1NumberKey).has_value(),
        "cache still holds SYS_HASH_2_NUMBER[B1] after the rollback");
    BOOST_CHECK_MESSAGE(readCacheBytes(currentKey) == preCurrent,
        "cache SYS_CURRENT_STATE changed across a failed canonicalize");

    // Backend plane (via the cache-first committed view) is genesis-only too.
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 1, bcos::ledger::fromStorage))
             .has_value());
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage))
             .has_value());
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view,
                                  request1.executionPayload.blockHash, bcos::ledger::fromStorage))
             .has_value());
}

// NEW-3 regression (switch SUCCESS path on a warm cache): the production composition's
// cache layer is process-lifetime and read FIRST by fork()/forkCommitted()
// (MultiLayerStorage.h), and the forward canonicalize warms it per block. A
// switch-SetCanonical that wrote only m_latestBackend left the cache holding the OLD
// chain's rows — the verifyCanonicalStateRoot post-condition then read a shadowed plane
// and the design §5 mandatory same-height reorg could never succeed (or, after an LRU
// eviction of the state rows, served stale canonical metadata). The switch must leave
// backend AND cache coherent: cache-first reads serve the NEW plane.
BOOST_AUTO_TEST_CASE(SwitchSetCanonicalServesNewPlaneThroughWarmCache)
{
    CacheImportServiceFixture f;
    f.seedCanonicalChainABC();  // forward-canonicalize A-B-C: the cache is warm with C-era rows
    auto const bHash = f.seededChainHash[2];
    auto const cHash = f.seededChainHash[3];

    // B' sibling of B, parent A — then the §5 mandatory same-height switch to B'@2.
    auto requestBPrime = f.validRequest(f.seededChainHash[1], 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE(bPrimeHash != bHash);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bool switched = false;
    std::string switchError;
    try
    {
        bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
        auto result = bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3));
        switched = static_cast<int>(result.payloadStatus.status) ==
                   static_cast<int>(bcos::engine::PayloadValidationStatus::Valid);
        if (!switched)
        {
            switchError = result.payloadStatus.validationError.value_or("<no error>");
        }
    }
    catch (std::exception const& e)
    {
        switchError = std::string("exception: ") + e.what();
    }
    // A plain-throw OpConsensusError rethrown through task::syncWait does not match
    // catch(std::exception) on this toolchain — keep a catch(...) fallback for it.
    catch (...)
    {
        switchError = "exception (non-std)";
    }
    BOOST_CHECK_MESSAGE(
        switched, "switch-SetCanonical must succeed through a warm cache (NEW-3): " << switchError);
    if (!switched)
    {
        return;  // keep the RED run's signal clean — the reads below would all fail too
    }

    // The cache-first committed view serves the NEW plane.
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        2);
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), bPrimeHash.hex());
    auto primeNumber = bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage));
    BOOST_REQUIRE(primeNumber.has_value());
    BOOST_CHECK_EQUAL(*primeNumber, 2);
    // A stale cache would still resolve the OLD chain here.
    BOOST_CHECK_MESSAGE(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, bHash, bcos::ledger::fromStorage))
             .has_value(),
        "cache-first read still resolves the replaced sibling B");
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, cHash, bcos::ledger::fromStorage))
             .has_value());
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockHash(view, 3, bcos::ledger::fromStorage))
             .has_value());

    // And the CACHE LAYER ITSELF holds the switch's rows (written from the same commit
    // as the backend — not merely failing to shadow them).
    auto readCacheBytes = [&](executor_v1::StateKey key) -> std::optional<bcos::bytes> {
        auto entry = bcos::task::syncWait(bcos::storage2::readOne(f.cache, std::move(key)));
        if (!entry.has_value())
        {
            return std::nullopt;
        }
        auto const value = entry->get();
        return bcos::bytes(value.begin(), value.end());
    };
    auto const number2Hash =
        readCacheBytes(StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(2)});
    BOOST_REQUIRE(number2Hash.has_value());
    // Raw byte compare: h256::hex() is UNPREFIXED, so it must not be compared against
    // toHexStringWithPrefix output.
    BOOST_CHECK(*number2Hash == bcos::bytes(bPrimeHash.begin(), bPrimeHash.end()));
    auto const currentCache = readCacheBytes(
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER});
    BOOST_REQUIRE(currentCache.has_value());
    BOOST_CHECK(bcos::toHexStringWithPrefix(*currentCache) == "0x32");  // "2"
    auto const primeHashNumber = readCacheBytes(
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(bPrimeHash)});
    BOOST_REQUIRE(primeHashNumber.has_value());
    BOOST_CHECK(bcos::toHexStringWithPrefix(*primeHashNumber) == "0x32");  // "2"
    BOOST_CHECK_MESSAGE(!readCacheBytes(StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                            bcos::concepts::bytebuffer::toView(cHash)})
                             .has_value(),
        "cache still holds the de-canonicalized C's hash->number row");
}

// NEW-3 failure-atomicity on the SWITCH branch: a same-height switch that fails after
// the batch was committed (injected post-condition failure) must restore BOTH layers —
// backend AND cache — to the pre-call plane (design §4.2: 失败则全部回到调用前), and the
// retried switch must then succeed.
BOOST_AUTO_TEST_CASE(SwitchFailureRestoresWarmCacheAndBackend)
{
    std::shared_ptr<FailVerifyScheduler<CacheMLS>> failDelegate;
    CacheImportServiceFixture f(CacheImportServiceFixture::DelegateFromFactory{},
        [&failDelegate](auto& blockFactory, auto& storage, auto& ioServicePool) {
            failDelegate = std::make_shared<FailVerifyScheduler<CacheMLS>>(
                makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
                std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                    bcos::evm::opstack::OpForkSchedule::legacy(false)),
                blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
            return failDelegate;
        });
    f.seedCanonicalChainABC();
    auto const bHash = f.seededChainHash[2];

    auto requestBPrime = f.validRequest(f.seededChainHash[1], 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Full raw-cache snapshot before the doomed switch.
    auto snapshotCache = [&f]() {
        std::map<std::string, bcos::bytes> out;
        auto iterator = bcos::task::syncWait(bcos::storage2::range(f.cache));
        while (true)
        {
            auto item = bcos::task::syncWait(iterator.next());
            if (!item.has_value())
            {
                break;
            }
            auto const& [key, valueVariant] = *item;
            if (auto* entry = std::get_if<bcos::storage::Entry>(&valueVariant))
            {
                auto const& value = entry->get();
                out.emplace(key.m_tableAndKey, bcos::bytes(value.begin(), value.end()));
            }
            else
            {
                out.emplace(key.m_tableAndKey, bcos::bytes{'<', 'D', '>'});
            }
        }
        return out;
    };
    auto const preCache = snapshotCache();
    BOOST_REQUIRE(!preCache.empty());

    failDelegate->failVerify = true;
    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    bool switchThrew = false;
    try
    {
        (void)bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3));
    }
    // catch(...) fallback, not just std::exception: a plain-throw bcos::evm::OpConsensusError
    // rethrown through task::syncWait does not match catch(std::exception) on this toolchain
    // (the RTTI base-walk fails), even though boost's catch-all monitor translates it.
    catch (std::exception const&)
    {
        switchThrew = true;
    }
    catch (...)
    {
        switchThrew = true;
    }
    BOOST_CHECK_MESSAGE(switchThrew, "a failed switch must not answer VALID");

    // BOTH layers observably unchanged: the raw cache is byte-identical...
    BOOST_CHECK_MESSAGE(snapshotCache() == preCache,
        "the failed switch left the cache layer different from its pre-call state");
    // ...and the cache-first committed view still serves the OLD chain.
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        3);
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), bHash.hex());
    BOOST_CHECK(!bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage))
                     .has_value());

    // The rollback was complete: the retried switch (post-condition live again)
    // succeeds and serves the new plane.
    failDelegate->failVerify = false;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto viewAfter = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          viewAfter, bcos::ledger::fromStorage)),
        2);
    auto height2After =
        bcos::task::syncWait(bcos::ledger::getBlockHash(viewAfter, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2After.has_value());
    BOOST_CHECK_EQUAL(height2After->hex(), bPrimeHash.hex());
}

// NEW-2 regression (reorg BACK onto the abandoned branch): after A-B-C → B'@2, a
// payload extending the de-canonicalized sibling B produces a chain whose canonical
// ROOT is below the current tip. That is a switch (design §4.4.5 整表替换), not a
// forward canonicalize — the forward branch merges E's delta onto B''s plane and fails
// its post-condition with an opaque mismatch. The walk must classify by
// root-attaches-at-tip, so the reorg-back lands on the abandoned branch's plane.
BOOST_AUTO_TEST_CASE(SwitchClassifiesReorgBackOntoAbandonedBranch)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const bHash = f.seededChainHash[2];
    auto const cHash = f.seededChainHash[3];

    // Switch to B'@2 (the §5 mandatory reorg).
    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Reorg BACK onto the abandoned branch: E@3 extends the de-canonicalized B. The
    // timestamp bump keeps E distinct from C (identical parent/number/txs would
    // reproduce C's exact hash).
    auto requestE = f.validRequest(bHash, 3);
    requestE.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestE.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestE.executionPayload, txRoot,
            *requestE.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestE.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestE, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto const eHash = requestE.executionPayload.blockHash;

    bool switched = false;
    std::string switchError;
    try
    {
        bcos::engine::ForkchoiceState fcuE{eHash, eHash, fixtureHeadHash()};
        auto result = bcos::task::syncWait(f.service.updateForkchoice(fcuE, nullptr, 3));
        switched = static_cast<int>(result.payloadStatus.status) ==
                   static_cast<int>(bcos::engine::PayloadValidationStatus::Valid);
        if (!switched)
        {
            switchError = result.payloadStatus.validationError.value_or("<no error>");
        }
    }
    catch (std::exception const& e)
    {
        switchError = std::string("exception: ") + e.what();
    }
    // A plain-throw OpConsensusError rethrown through task::syncWait does not match
    // catch(std::exception) on this toolchain — keep a catch(...) fallback for it.
    catch (...)
    {
        switchError = "exception (non-std)";
    }
    BOOST_CHECK_MESSAGE(switched,
        "reorg-back onto the abandoned branch must be classified and served as a switch "
        "(NEW-2): "
            << switchError);
    if (!switched)
    {
        return;
    }

    // The canonical chain is now A - B - E: B is re-canonicalized at height 2, the
    // B' branch is de-canonicalized, C never returns.
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        3);
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), bHash.hex());
    auto bNumber =
        bcos::task::syncWait(bcos::ledger::getBlockNumber(view, bHash, bcos::ledger::fromStorage));
    BOOST_REQUIRE(bNumber.has_value());
    BOOST_CHECK_EQUAL(*bNumber, 2);
    auto height3 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 3, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height3.has_value());
    BOOST_CHECK_EQUAL(height3->hex(), eHash.hex());
    BOOST_CHECK(!bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage))
                     .has_value());
    // C (the abandoned branch's old head) stays de-canonicalized — E is a distinct
    // block, so C's hash->number row must not have come back with the branch.
    BOOST_CHECK(
        !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, cHash, bcos::ledger::fromStorage))
             .has_value());
}

// NEW-2 regression (reorg BACK onto a PRUNED branch): after A-B-C → B'@2 the B' switch
// released C@3's materialized flat (pruneFlatsAbove), so a later FCU back to C@3 can no
// longer restore the head flat directly. The walk yields [B, C] rooted at A (canonical
// at 1) while the tip is B'@2 — a switch whose head flat is gone. The switch must
// re-materialize C's world from the deepest surviving ancestor flat (B@2) plus the
// chain deltas (design §4.4.5's replay stand-in), re-canonicalize EVERY height above
// the fork point (B's rows were removed/overwritten by the B' switch), and
// de-canonicalize B'.
BOOST_AUTO_TEST_CASE(SwitchBackToPrunedBranchRecanonicalizesChain)
{
    ImportServiceFixture f;
    f.seedCanonicalChainABC();
    auto const aHash = f.seededChainHash[1];
    auto const bHash = f.seededChainHash[2];
    auto const cHash = f.seededChainHash[3];
    // Independent oracle for the reconstruction: the state root the CL announced for C
    // when it was first imported (captured before B' overwrites height 2's probes).
    auto const cStateRoot = f.executedByNumber[3]->stateRoot();

    auto requestBPrime = f.validRequest(aHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto const bPrimeHash = requestBPrime.executionPayload.blockHash;
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestBPrime, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuBPrime{bPrimeHash, bPrimeHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // FCU back to C@3 — the head's flat was pruned, only B@2's survives.
    bool switched = false;
    std::string switchError;
    try
    {
        bcos::engine::ForkchoiceState fcuC{cHash, cHash, fixtureHeadHash()};
        auto result = bcos::task::syncWait(f.service.updateForkchoice(fcuC, nullptr, 3));
        switched = static_cast<int>(result.payloadStatus.status) ==
                   static_cast<int>(bcos::engine::PayloadValidationStatus::Valid);
        if (!switched)
        {
            switchError = result.payloadStatus.validationError.value_or("<no error>");
        }
    }
    catch (std::exception const& e)
    {
        switchError = std::string("exception: ") + e.what();
    }
    catch (...)
    {
        switchError = "exception (non-std)";
    }
    BOOST_CHECK_MESSAGE(switched,
        "reorg back to the pruned branch must reconstruct the head plane and switch "
        "(NEW-2): "
            << switchError);
    if (!switched)
    {
        return;
    }

    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        3);
    auto height3 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 3, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height3.has_value());
    BOOST_CHECK_EQUAL(height3->hex(), cHash.hex());
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), bHash.hex());
    auto bNumber =
        bcos::task::syncWait(bcos::ledger::getBlockNumber(view, bHash, bcos::ledger::fromStorage));
    BOOST_REQUIRE(bNumber.has_value());
    BOOST_CHECK_EQUAL(*bNumber, 2);
    BOOST_CHECK(!bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, bPrimeHash, bcos::ledger::fromStorage))
                     .has_value());

    // The reconstructed plane is C's ORIGINAL world: the committed world root equals the
    // root C's payload announced at first import.
    bcos::evm::evmstate::Storage2State<ViewType> state(view);
    auto const root = bcos::evm::stateRootOf(state);
    BOOST_CHECK_MESSAGE(bcos::h256(root.bytes, 32).hex() == cStateRoot.hex(),
        "reconstructed plane is not C's original world: " << bcos::h256(root.bytes, 32).hex()
                                                          << " != " << cStateRoot.hex());
}

// F2 regression: an imported parent whose materialized plane is gone must be answered
// SYNCING (design §4.5 "hasState failed") — never executed on the committed plane.
BOOST_AUTO_TEST_CASE(PrunedParentPlaneIsSyncingNotEmptyReExecute)
{
    ImportServiceFixture f;

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request3 = f.validRequest(request2.executionPayload.blockHash, 3);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request3, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Forward-canonicalize only 1..2: canonicalizeImportedHead releases the flats of
    // every height above the new tip, so B3's plane is gone while B3 stays imported.
    bcos::engine::ForkchoiceState fcuB2{request2.executionPayload.blockHash,
        request2.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuB2, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(f.service.hasImportedBlock(request3.executionPayload.blockHash));

    auto request4 = f.validRequest(request3.executionPayload.blockHash, 4);
    auto status4 = bcos::task::syncWait(f.service.newPayload(request4, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status4.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!status4.latestValidHash.has_value());
    BOOST_CHECK(!f.service.hasImportedBlock(request4.executionPayload.blockHash));
}

// F5 regression: canonicalize must NOT hold the imported-tree POSIX mutex across a
// suspension point. task::syncWait completes the coroutine via status.notify_one() on
// whatever thread resumed it (libtask/bcos-task/Wait.h), so a mutex locked before an
// await can be unlocked on another thread (UB); it also serializes an unrelated import
// behind the whole canonicalize batch. The delegate parks canonicalize inside its
// awaited post-condition, then a concurrent newPayload must still answer promptly
// (SYNCING, fail-closed) instead of blocking on the lock.
BOOST_AUTO_TEST_CASE(CanonicalizeHoldsNoLockAcrossAwait)
{
    auto gate = std::make_shared<BlockingGate>();
    ImportServiceFixture f(gate);

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    bcos::engine::ForkchoiceState fcuB1{request1.executionPayload.blockHash,
        request1.executionPayload.blockHash, fixtureHeadHash()};
    bcos::engine::ForkchoiceUpdatedResult fcuResult;
    std::thread canonicalizer(
        [&] { fcuResult = bcos::task::syncWait(f.service.updateForkchoice(fcuB1, nullptr, 3)); });
    // canonicalizeImportedHead is now parked inside its awaited post-condition.
    gate->entered.wait();

    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
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
    // Unblock canonicalize before asserting so a RED run cannot hang the suite.
    gate->release.count_down();
    canonicalizer.join();
    importer.join();

    BOOST_CHECK_MESSAGE(
        finished, "concurrent newPayload blocked behind the canonicalize lock across an await");
    BOOST_CHECK_EQUAL(static_cast<int>(fcuResult.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(static_cast<int>(importStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// U6-F1 regression (MEDIUM, confirmed): a switch-SetCanonical that walks a chain with
// an intermediate height that was NEVER canonical must stage that height's hash-keyed
// bodies, not only its by-number index. Before the fix the (2.5) loop staged just
// SYS_NUMBER_2_TXS[height], so SYS_NUMBER_2_TXS[h] listed tx hashes whose
// SYS_HASH_2_TX / SYS_HASH_2_RECEIPT rows were absent, and ledger::getBlockData's batch
// tx resolution failed for the freshly canonical block. The fixture's zero
// L1-attributes envelope is byte-identical across heights (the first canonicalization
// writes the single shared hash), so a per-height deposit tag is required to unmask it;
// production deposits already differ per block via the L1-info sequence number.
BOOST_AUTO_TEST_CASE(SwitchSetCanonicalWritesIntermediateHeightHashKeyedBodies)
{
    ImportServiceFixture f;

    // Canonical chain A@1 -> B@2 (one-jump FCU), so the tip is 2.
    auto requestA = f.validRequest(fixtureHeadHash(), 1, 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestA, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestB = f.validRequest(requestA.executionPayload.blockHash, 2, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestB, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    bcos::engine::ForkchoiceState fcuB{requestB.executionPayload.blockHash,
        requestB.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuB, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // Sibling C@2 on A was imported but never canonicalized; D@3 extends C.
    auto requestC = f.validRequest(requestA.executionPayload.blockHash, 2, 3);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestC, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestD = f.validRequest(requestC.executionPayload.blockHash, 3, 4);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestD, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // FCU to D: the chain [C@2, D@3] roots at A@1 while the tip is still 2, so this takes
    // the switch branch and re-canonicalizes [C, D]; C is the intermediate height.
    bcos::engine::ForkchoiceState fcuD{requestD.executionPayload.blockHash,
        requestD.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuD, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto const crypto = makeCryptoSuite()->hashImpl();
    auto const cTxHash = crypto->hash(requestC.executionPayload.transactions[0].raw);
    auto const dTxHash = crypto->hash(requestD.executionPayload.transactions[0].raw);
    auto const bTxHash = crypto->hash(requestB.executionPayload.transactions[0].raw);
    auto view = f.storage.forkCommitted();

    // C now occupies canonical height 2 ...
    auto height2 =
        bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2.has_value());
    BOOST_CHECK_EQUAL(height2->hex(), requestC.executionPayload.blockHash.hex());

    // ... the by-number index [2] was re-staged with C's tx hash by step (2.5) ...
    auto numberToTxs = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, std::to_string(2)}));
    BOOST_REQUIRE(numberToTxs.has_value());
    auto const numberToTxsBytes = numberToTxs->get();
    auto metaBlock = f.blockFactory->createBlock(bcos::bytesConstRef(
        reinterpret_cast<const bcos::byte*>(numberToTxsBytes.data()), numberToTxsBytes.size()));
    auto hashes = metaBlock->transactionHashes() | ::ranges::to<std::vector>();
    BOOST_REQUIRE_EQUAL(hashes.size(), 1U);
    BOOST_CHECK_EQUAL(hashes[0].hex(), cTxHash.hex());

    // ... so C's hash-keyed bodies MUST exist. This is the U6-F1 symptom: before the fix
    // step (2.5) staged no SYS_HASH_2_TX / SYS_HASH_2_RECEIPT for intermediate heights.
    auto cBody = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(cTxHash)}));
    BOOST_CHECK_MESSAGE(
        cBody.has_value(), "SYS_HASH_2_TX missing for intermediate height tx " << cTxHash.hex());
    auto cReceipt = bcos::task::syncWait(bcos::storage2::readOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(cTxHash)}));
    BOOST_CHECK_MESSAGE(cReceipt.has_value(),
        "SYS_HASH_2_RECEIPT missing for intermediate height tx " << cTxHash.hex());

    // Impact: a canonical block must resolve every hash listed by SYS_NUMBER_2_TXS[2]
    // through the bodies; ledger::getBlockData's batch get otherwise fails the block.
    std::size_t resolved = 0;
    for (auto const& hash : hashes)
    {
        auto body = bcos::task::syncWait(bcos::storage2::readOne(
            view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(hash)}));
        if (body.has_value())
        {
            ++resolved;
        }
    }
    BOOST_CHECK_MESSAGE(
        resolved == hashes.size(), "Ledger batch-get-transactions would fail: resolved "
                                       << resolved << " of " << hashes.size() << " hashes");

    // Controls: head D's bodies come from step (3), and previously-canonical sibling B's
    // bodies exist from its own canonicalization. Only never-canonical C is affected.
    auto dBody = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(dTxHash)}));
    BOOST_CHECK_MESSAGE(dBody.has_value(), "head body missing for tx " << dTxHash.hex());
    auto bBody = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(bTxHash)}));
    BOOST_CHECK_MESSAGE(
        bBody.has_value(), "previously canonical B's body should be present: " << bTxHash.hex());
}

// U4-F1 regression (MEDIUM, confirmed; closes U6-F4): the engine import gate's
// commitmentsOfHeader and OpScheduler's headerCommitments are two projections of the
// same OpBlockCommitments surface, so the u256 -> uint64 narrowing of blobGasUsed must
// be the SAME bounds-checked rule (bcos::evm::engine::detail::narrowU256ToU64): an
// out-of-range value is a fail-closed OpConsensusError on both, never a silent
// modulo-2^64 truncation. Pre-fix commitmentsOfHeader used a raw static_cast and folded
// 2^64 + 5 onto 5, so two headers differing only above 2^64 compared equal.
//
// Reachability: the value is NOT reachable from the wire — validateOpBlobGasUsed rejects
// blobGasUsed > uint64 max before the projection runs (OpEngineService.cpp). This is a
// drift-protection pin: it keeps the two projections from silently diverging again, and
// is the dedicated unit test for the merged U6-F4 site.
BOOST_AUTO_TEST_CASE(CommitmentsOfHeaderBlobGasUsedNarrowingMatchesSchedulerGuard)
{
    namespace evmDetail = bcos::evm::engine::detail;
    ImportServiceFixture f;
    auto const hf = f.blockFactory->blockHeaderFactory();
    auto const overU64 = bcos::u256(1) << 64;  // uint64 max + 1

    auto const header = hf->createBlockHeader();

    // Out of range: fail closed exactly like the scheduler projection's narrowU256ToU64.
    header->setBlobGasUsed(overU64);
    BOOST_CHECK_THROW(bcos::engine::commitmentsOfHeader(*header), bcos::evm::OpConsensusError);
    BOOST_CHECK_THROW(evmDetail::narrowU256ToU64(overU64, "headerCommitments blobGasUsed"),
        bcos::evm::OpConsensusError);

    // In range: both projections agree on the value.
    header->setBlobGasUsed(bcos::u256(42));
    auto const engineCommitments = bcos::engine::commitmentsOfHeader(*header);
    BOOST_REQUIRE(engineCommitments.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*engineCommitments.blobGasUsed,
        evmDetail::narrowU256ToU64(bcos::u256(42), "headerCommitments blobGasUsed"));
    BOOST_CHECK_EQUAL(*engineCommitments.blobGasUsed, 42U);

    // uint64 max is the inclusive ceiling on both sides.
    header->setBlobGasUsed(bcos::u256(std::numeric_limits<uint64_t>::max()));
    auto const maxCommitments = bcos::engine::commitmentsOfHeader(*header);
    BOOST_REQUIRE(maxCommitments.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*maxCommitments.blobGasUsed, std::numeric_limits<uint64_t>::max());
}

BOOST_AUTO_TEST_SUITE_END()
