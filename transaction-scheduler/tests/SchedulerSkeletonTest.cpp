/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Task 2 smoke test: SchedulerSkeleton CRTP hook dispatch + lock-free fast-path.
 *        A minimal FakeDerived implements the 5 CRTP hooks + 4 pure virtuals +
 *        classifyException + the shared-mechanism stubs (which Task 3 migrates into
 *        the skeleton as real protected methods) and drives coExecuteBlock /
 *        coCommitBlock through their public SchedulerInterface face.
 *
 *        NOTE on the classify test: coExecuteBlock's catch (std::exception&) matches
 *        ANY std::exception subclass, with no type discrimination. bcos-task
 *        propagates a failed child as an exception_ptr — PromiseBase::unhandled_exception()
 *        stores current_exception(), and Awaitable::await_resume() rethrows the
 *        original exception at the parent co_await point — so both a raw
 *        std::runtime_error and a bcos::Exception subclass are caught alike. The fake
 *        throws a DERIVE_BCOS_EXCEPTION type only because that is the real-world error
 *        type (richer boost::diagnostic_information); it is not required for the catch
 *        to match.
 * @file SchedulerSkeletonTest.cpp
 */

#include "bcos-transaction-scheduler/SchedulerSkeleton.h"

#include "bcos-framework/storage/Entry.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include <boost/test/unit_test.hpp>

#include <deque>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::scheduler_v1;

namespace
{
/// Minimal MultiLayerStorage stand-in: the skeleton only ever touches the nested
/// ViewType / MutableStorage types (all real storage access goes through derived()).
struct FakeView
{
    void newMutable() {}
};
struct FakeMutableStorage
{
};
struct FakeStorage
{
    using ViewType = FakeView;
    using MutableStorage = FakeMutableStorage;
};
/// Unused mode-specific stand-ins (the skeleton never names Executor/SchedulerImpl/Ledger).
struct FakeExecutor
{
};
struct FakeSchedulerImpl
{
};
struct FakeLedger
{
};

/// Thrown by the fake's loadLedgerConfig to exercise the A3 classify path. Uses a
/// DERIVE_BCOS_EXCEPTION type (bcos::Exception) as the real-world error type; the
/// coroutine's catch (std::exception&) would equally match a bare std::runtime_error
/// (bcos-task rethrows the original exception at the parent co_await point).
DERIVE_BCOS_EXCEPTION(FakeExecuteBoom);

/// A minimal Derived that implements the skeleton's whole contract.
///
/// The 5 CRTP hooks + 4 pure virtuals + classifyException are the contract the
/// skeleton enforces; the *mechanism* methods below (fastPathHit, tryExecuteLock,
/// continuityCheck, backpressureOk, forkView, loadLedgerConfig, pushResult,
/// updateLastExecutedBlockNumber, tryCommitLock, commitContinuityCheck, peekResult,
/// mergeBackStorage, postMergeCommitObserver, popResult, loadCommitLedgerConfig,
/// updateLastCommittedBlockNumber, notifyBlockNumber) are trivial stubs that Task 3a
/// migrates into the skeleton as real protected methods (spec v3 §2.1).
struct FakeDerived
  : SchedulerSkeleton<FakeStorage, FakeExecutor, FakeSchedulerImpl, FakeLedger, FakeDerived>
{
    // ---- test observation state ----
    std::vector<std::string> m_calls;
    bool m_throwInLoadLedgerConfig = false;
    scheduler::SchedulerError m_classifiedError = scheduler::SchedulerError::UnknownError;

    // ---- fast-path state (FakeDerived's own m_results; Task 3 migrates the real deque) ----
    struct CachedExecuteResult
    {
        protocol::BlockHeader::Ptr m_executedBlockHeader;
        bool m_sysBlock = false;
    };
    std::deque<std::shared_ptr<CachedExecuteResult>> m_results;

    // ---- execute -> commit hand-off (what pushResult stores for commit's peek) ----
    std::optional<protocol::BlockNumber> m_pendingNumber;
    std::shared_ptr<SchedulerExecuteResult> m_pendingResult;

    void record(std::string call) { m_calls.emplace_back(std::move(call)); }

    void addCached(protocol::BlockHeader::Ptr header, bool sysBlock)
    {
        m_results.push_front(std::make_shared<CachedExecuteResult>(
            CachedExecuteResult{std::move(header), sysBlock}));
    }

    // ============ 5 CRTP hooks (non-virtual; hide the skeleton's declarations) ============

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block&, FakeView&)
    {
        record("getTransactions");
        co_return std::vector<protocol::Transaction::ConstPtr>{};
    }

    task::Task<SchedulerExecuteResult> execute(FakeView&, protocol::BlockHeader const&,
        std::vector<protocol::Transaction::ConstPtr> const&, ledger::LedgerConfig const&)
    {
        record("execute");
        co_return SchedulerExecuteResult{};
    }

    task::Task<protocol::BlockHeader::Ptr> finishExecute(FakeView&, SchedulerExecuteResult const&,
        protocol::BlockHeader const& blockHeader, protocol::Block&,
        std::vector<protocol::Transaction::ConstPtr> const&, ledger::LedgerConfig const&,
        bool& sysBlock)
    {
        record("finishExecute");
        sysBlock = false;
        auto executedHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
        executedHeader->setNumber(blockHeader.number());
        co_return executedHeader;
    }

    void verifyResult(protocol::BlockHeader::Ptr, protocol::BlockHeader const&)
    {
        record("verifyResult");
    }

    task::Task<std::shared_ptr<FakeMutableStorage>> commit(
        FakeView&, protocol::BlockHeader::Ptr, SchedulerExecuteResult const&)
    {
        record("commit");
        co_return std::make_shared<FakeMutableStorage>();
    }

    // ============ 4 pure virtuals + classifyException ============

    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) override
    {
        record("call");
    }

    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)>) override
    {
        record("getCode");
    }

    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)>) override
    {
        record("getABI");
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, protocol::BlockNumber) override
    {
        record("getPendingStorageAt");
        co_return std::nullopt;
    }

    scheduler::SchedulerError classifyException(std::exception_ptr) const override
    {
        return m_classifiedError;
    }

    // ============ shared-mechanism stubs (Task 3 migrates real bodies into skeleton) ============

    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number)
    {
        // Mirror of BaselineScheduler.h:397-412 (front = newest, back = oldest).
        if (!m_results.empty())
        {
            auto frontNumber = m_results.front()->m_executedBlockHeader->number();
            auto backNumber = m_results.back()->m_executedBlockHeader->number();
            if (number <= frontNumber && number >= backNumber)
            {
                auto& result = m_results.at(frontNumber - number);
                return std::pair{result->m_executedBlockHeader, result->m_sysBlock};
            }
        }
        return std::nullopt;
    }

    bool tryExecuteLock() { return true; }
    bool continuityCheck(protocol::BlockNumber) { return true; }
    bool backpressureOk() { return true; }
    FakeView forkView() { return FakeView{}; }

    ledger::LedgerConfig::Ptr loadLedgerConfig(FakeView&, protocol::BlockNumber)
    {
        if (m_throwInLoadLedgerConfig)
        {
            throw FakeExecuteBoom{};
        }
        return std::make_shared<ledger::LedgerConfig>();
    }

    void pushResult(protocol::BlockNumber number, protocol::Block::Ptr, protocol::BlockHeader::Ptr,
        SchedulerExecuteResult result, bool)
    {
        record("pushResult");
        m_pendingNumber = number;
        m_pendingResult = std::make_shared<SchedulerExecuteResult>(std::move(result));
    }

    void updateLastExecutedBlockNumber(protocol::BlockNumber)
    {
        record("updateLastExecutedBlockNumber");
    }

    bool tryCommitLock() { return true; }
    bool commitContinuityCheck(protocol::BlockNumber) { return true; }

    std::shared_ptr<SchedulerExecuteResult> peekResult(protocol::BlockNumber number)
    {
        if (m_pendingNumber && *m_pendingNumber == number)
        {
            return m_pendingResult;
        }
        return nullptr;
    }

    task::Task<void> mergeBackStorage(FakeMutableStorage&)
    {
        record("mergeBackStorage");
        co_return;
    }

    void postMergeCommitObserver(protocol::BlockNumber, SchedulerExecuteResult const&)
    {
        record("postMergeCommitObserver");
    }

    void popResult(protocol::BlockNumber number)
    {
        record("popResult");
        if (m_pendingNumber && *m_pendingNumber == number)
        {
            m_pendingNumber.reset();
            m_pendingResult.reset();
        }
    }

    ledger::LedgerConfig::Ptr loadCommitLedgerConfig(protocol::BlockHeader::Ptr)
    {
        return std::make_shared<ledger::LedgerConfig>();
    }

    void updateLastCommittedBlockNumber(protocol::BlockNumber)
    {
        record("updateLastCommittedBlockNumber");
    }

    void notifyBlockNumber(protocol::BlockNumber) { record("notifyBlockNumber"); }
};

using ExecuteResultTuple = std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>;
using CommitResultTuple = std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>;

protocol::Block::Ptr makeBlock(protocol::BlockNumber number)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    block->blockHeader()->setNumber(number);
    return block;
}

protocol::BlockHeader::Ptr makeHeader(protocol::BlockNumber number)
{
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    header->setNumber(number);
    return header;
}

ExecuteResultTuple driveExecuteBlock(
    scheduler::SchedulerInterface& scheduler, protocol::Block::Ptr block)
{
    std::promise<ExecuteResultTuple> promise;
    scheduler.executeBlock(std::move(block), false,
        [&promise](Error::Ptr error, protocol::BlockHeader::Ptr header, bool sysBlock) {
            promise.set_value({std::move(error), std::move(header), sysBlock});
        });
    return promise.get_future().get();
}

CommitResultTuple driveCommitBlock(
    scheduler::SchedulerInterface& scheduler, protocol::BlockHeader::Ptr header)
{
    std::promise<CommitResultTuple> promise;
    scheduler.commitBlock(
        std::move(header), [&promise](Error::Ptr error, ledger::LedgerConfig::Ptr cfg) {
            promise.set_value({std::move(error), std::move(cfg)});
        });
    return promise.get_future().get();
}

struct SchedulerSkeletonFixture
{
    FakeDerived fake;
};

BOOST_FIXTURE_TEST_SUITE(SchedulerSkeletonSuite, SchedulerSkeletonFixture)

BOOST_AUTO_TEST_CASE(cacheHitSkipsReExecution)
{
    // Pre-seed the fast-path cache: block 100 already executed.
    fake.addCached(makeHeader(100), /*sysBlock=*/false);

    auto [error, header, sysBlock] = driveExecuteBlock(fake, makeBlock(100));

    BOOST_CHECK(!error);
    BOOST_REQUIRE(header);
    BOOST_CHECK_EQUAL(header->number(), 100);
    BOOST_CHECK(!sysBlock);
    // The lock-free fast path must serve the cached result without re-running hooks.
    BOOST_CHECK(fake.m_calls.empty());
}

BOOST_AUTO_TEST_CASE(hooksDispatchInOrder)
{
    // Empty cache: coExecuteBlock must drive hooks ①..④ then push, in order.
    auto [error, header, sysBlock] = driveExecuteBlock(fake, makeBlock(200));

    BOOST_CHECK(!error);
    BOOST_REQUIRE(header);
    BOOST_CHECK_EQUAL(header->number(), 200);
    BOOST_CHECK(!sysBlock);

    std::vector<std::string> const expectedExecuteCalls = {"getTransactions", "execute",
        "finishExecute", "verifyResult", "pushResult", "updateLastExecutedBlockNumber"};
    BOOST_CHECK_EQUAL_COLLECTIONS(fake.m_calls.begin(), fake.m_calls.end(),
        expectedExecuteCalls.begin(), expectedExecuteCalls.end());

    // Now drive coCommitBlock: hook ⑤ (commit) must run after the skeleton's
    // peek, and the single merge + post-merge observer + pop + notify must follow.
    fake.m_calls.clear();
    auto [commitError, cfg] = driveCommitBlock(fake, makeHeader(200));

    BOOST_CHECK(!commitError);
    BOOST_REQUIRE(cfg);

    std::vector<std::string> const expectedCommitCalls = {"commit", "mergeBackStorage",
        "postMergeCommitObserver", "popResult", "updateLastCommittedBlockNumber",
        "notifyBlockNumber"};
    BOOST_CHECK_EQUAL_COLLECTIONS(fake.m_calls.begin(), fake.m_calls.end(),
        expectedCommitCalls.begin(), expectedCommitCalls.end());
}

BOOST_AUTO_TEST_CASE(classifyExceptionMapsError)
{
    fake.m_throwInLoadLedgerConfig = true;
    // A sentinel distinct from the default UnknownError proves classifyException
    // is the source of the surfaced error code (A3 wiring).
    fake.m_classifiedError = scheduler::SchedulerError::InvalidStatus;

    auto [error, header, sysBlock] = driveExecuteBlock(fake, makeBlock(300));

    BOOST_REQUIRE(error);
    BOOST_CHECK_EQUAL(
        error->errorCode(), static_cast<int64_t>(scheduler::SchedulerError::InvalidStatus));
    BOOST_CHECK(!header);
    BOOST_CHECK(!sysBlock);
    // getTransactions ran before the throwing loadLedgerConfig; execute must not.
    BOOST_REQUIRE_EQUAL(fake.m_calls.size(), 1u);
    BOOST_CHECK_EQUAL(fake.m_calls[0], "getTransactions");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
