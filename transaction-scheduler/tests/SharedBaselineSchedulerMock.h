#pragma once

// Shared mock executor / scheduler / storage for the BaselineScheduler unit tests.
//
// Before this header existed, each test file defined its own mock executor + scheduler +
// storage backend types, which meant every file instantiated a DISTINCT
// BaselineScheduler<Storage, Executor, Scheduler, Ledger> specialization — ~15s of
// template instantiation front-end time per specialization per TU. The mocks turned out
// to be behavioural subsets of one another, so they live here once:
//
//   testBaselineScheduler / FIB101  -> Trivial executor (+ optional version-99 timestamp
//                                      probe), no state writes, receipts WITH log entries
//   TestExecuteStateRootRegression  -> Trivial executor, two state rows written EVERY
//                                      block, receipts without logs
//   TestCommitSingleBatch           -> keeps its own merge-counting backend (that is the
//                                      point of the test), so it is NOT on the shared
//                                      storage; its executor/scheduler shapes match the
//                                      trivial / plan-writing ones here
//   TestEthCallHistory              -> probe executor reading an account slot through
//                                      EVMAccount (Mode::ReadSlot / WriteThenReadSlot),
//                                      plan-driven state writes
//   TestMPTSchedulerWiring          -> Trivial executor, plan-driven writes AND removes
//                                      (RowOp::value == nullopt)
//
// The getLedgerConfig tag_invoke stub also lives here exactly once; the per-test stubs
// only existed because each test anchored its own stub on its own storage type for ADL.
// The stub is found by ADL through the shared storage's template arguments (this
// namespace is an associated namespace of the ViewType), and it hands out
// g_stubFeatures — EVERY fixture using the shared types must assign g_stubFeatures in
// its constructor (ledger::Features{} reproduces the old trivial stubs), because tests
// in this binary share the one global.
//
// The single BaselineScheduler specialization is explicitly instantiated in
// SharedBaselineSchedulerInst.cpp; the extern template declaration at the bottom keeps
// every consumer TU from instantiating it again.

#include "TrivialCheckpointStorage.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-task/Task.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <range/v3/iterator/operations.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/transform.hpp>
#include <string>
#include <utility>
#include <vector>

namespace bcos::test::sharedmock
{
using SharedMutableStorage =
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
// Behaviourally a stock flat MemoryStorage, but a DISTINCT subclass on purpose: it anchors
// this namespace as an associated namespace of the MultiLayerStorage ViewType, which is
// what lets ADL find the getLedgerConfig tag_invoke stub below (an alias to MemoryStorage
// would leave every ViewType component in bcos::storage2 and the stub would be invisible
// to ADL — the generic LedgerMethods.h implementation would win instead).
struct SharedBackendStorage
  : storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::CONCURRENT),
        std::hash<executor_v1::StateKey>>
{
};
using SharedCheckpointBackend = storage2::TrivialCheckpointStorage<executor_v1::StateKey,
    executor_v1::StateValue, SharedBackendStorage>;
using SharedMultiLayerStorage =
    storage2::MultiLayerStorage<SharedMutableStorage, void, SharedCheckpointBackend>;

/// Superset of the per-test mock executors. Mode::Trivial reproduces the stock mock
/// (empty receipt); the two probe modes reproduce TestEthCallHistory's HCProbeExecutor:
/// read the probed account's slot through whatever storage the scheduler hands in —
/// EVMAccount over the (possibly historical) stack, exactly the account type the
/// production HostContext uses — and return the 32-byte value as the receipt output.
/// WriteThenReadSlot first SSTOREs m_writeValue, so the output proves (or disproves)
/// read-your-writes on the stack. m_checkTimestamp99 reproduces testBaselineScheduler's
/// version-99 timestamp assertion.
struct SharedMockExecutor
{
    enum class Mode : uint8_t
    {
        Trivial,
        ReadSlot,
        WriteThenReadSlot,
    };
    Mode m_mode{Mode::Trivial};
    evmc_address m_probeAddress{};
    evmc_bytes32 m_probeSlot{};
    evmc_bytes32 m_writeValue{};
    protocol::BlockNumber m_lastExecutedHeaderNumber{-1};
    bool m_checkTimestamp99{};

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int /*contextID*/, ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
    {
        if (m_checkTimestamp99 && transaction.version() == 99)
        {
            BOOST_CHECK_EQUAL(blockHeader.timestamp(), 10088);
        }
        m_lastExecutedHeaderNumber = blockHeader.number();
        if (m_mode == Mode::Trivial)
        {
            co_return {};
        }

        ledger::account::EVMAccount account(
            storage, m_probeAddress, false, /*treatSystemAsUser=*/false);
        if (m_mode == Mode::WriteThenReadSlot)
        {
            co_await account.setStorage(m_probeSlot, m_writeValue);
        }
        auto value = co_await account.storage(m_probeSlot);
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        receipt->inner().data.output.assign(value.bytes, value.bytes + sizeof(value.bytes));
        co_return receipt;
    }

    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& /*blockHeader*/,
        protocol::Transaction const& /*transaction*/, int32_t /*contextID*/,
        ledger::LedgerConfig const& /*ledgerConfig*/,
        bool /*call*/) -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};

/// Superset of the per-test mock schedulers: writes m_everyBlockRows on EVERY block
/// (TestExecuteStateRootRegression's fixed rows), then the plan rows for this block
/// number (plan-driven tests; RowOp::value == nullopt removes the row — the
/// TestMPTSchedulerWiring shape), then fabricates one receipt per transaction.
/// m_receiptsWithLogs adds the log entry testBaselineScheduler / FIB101 receipts carry.
struct SharedMockScheduler
{
    struct RowOp
    {
        std::string table;
        std::string key;
        std::optional<std::string> value;  // nullopt => remove the row
    };

    std::map<protocol::BlockNumber, std::vector<RowOp>> const* m_plan{};
    std::vector<RowOp> m_everyBlockRows;
    bool m_receiptsWithLogs{};

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        auto writeRows = [&storage](std::vector<RowOp> const& rows) -> task::Task<void> {
            for (auto const& row : rows)
            {
                if (row.value)
                {
                    storage::Entry entry;
                    entry.set(*row.value);
                    co_await storage2::writeOne(
                        storage, executor_v1::StateKey{row.table, row.key}, std::move(entry));
                }
                else
                {
                    co_await storage2::removeOne(
                        storage, executor_v1::StateKey{row.table, row.key});
                }
            }
        };
        co_await writeRows(m_everyBlockRows);
        if (m_plan)
        {
            if (auto it = m_plan->find(blockHeader.number()); it != m_plan->end())
            {
                co_await writeRows(it->second);
            }
        }

        auto receipts =
            ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
            ::ranges::views::transform([this](size_t) -> protocol::TransactionReceipt::Ptr {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
                constexpr static std::string_view str = "abc";
                auto& inner = receipt->inner();
                inner.dataHash.assign(str.begin(), str.end());
                inner.data.gasUsed = "100";
                if (m_receiptsWithLogs)
                {
                    bytes logAddress;
                    logAddress.assign(str.begin(), str.end());
                    bcos::protocol::LogEntry logEntry{
                        logAddress, bcos::h256s{bcos::h256{}}, bcos::bytes{}};
                    std::vector<bcos::protocol::LogEntry> logs;
                    logs.emplace_back(std::move(logEntry));
                    receipt->setLogEntries(logs);
                }
                return receipt;
            }) |
            ::ranges::to<std::vector<protocol::TransactionReceipt::Ptr>>();
        co_return receipts;
    }
};

/// The Features the shared getLedgerConfig stub hands out — assigned by every fixture
/// that drives the shared scheduler (see the file-level comment).
inline ledger::Features g_stubFeatures{};

/// RAII guard for g_stubFeatures: resets it to the default Features at fixture teardown.
/// The global is shared by every suite in the binary; without the reset, a fixture that
/// forgets to assign it would silently inherit whichever features the previously-run
/// suite left behind (mpt_state_root / l2_ethereum_compat leak across suites). Declare
/// the guard BEFORE the BaselineScheduler member so it is destructed after it; every
/// fixture using the shared scheduler should hold one and still assign g_stubFeatures
/// in its constructor.
struct ScopedStubFeatures
{
    ScopedStubFeatures() = default;
    ScopedStubFeatures(ScopedStubFeatures const&) = delete;
    ScopedStubFeatures& operator=(ScopedStubFeatures const&) = delete;
    ~ScopedStubFeatures() { g_stubFeatures = ledger::Features{}; }
};

/// Storage-level getLedgerConfig stub, found by ADL through the shared ViewType's
/// template arguments. Replaces the per-test stubs, which differed only in the Features
/// value they handed out (or in not touching ledgerConfig at all — the default
/// g_stubFeatures reproduces that: a fresh LedgerConfig already holds a default
/// Features).
inline task::AwaitableValue<void> tag_invoke(ledger::tag_t<ledger::getLedgerConfig> /*unused*/,
    SharedMultiLayerStorage::ViewType& /*storage*/, ledger::LedgerConfig& ledgerConfig,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    ledgerConfig.setFeatures(g_stubFeatures);
    return {};
}

using SharedBaselineScheduler = scheduler_v1::BaselineScheduler<SharedMultiLayerStorage,
    SharedMockExecutor, SharedMockScheduler, ledger::LedgerInterface>;
}  // namespace bcos::test::sharedmock

// Explicitly instantiated once in SharedBaselineSchedulerInst.cpp — consumer TUs must
// not instantiate this specialization themselves.
extern template class bcos::scheduler_v1::BaselineScheduler<
    bcos::test::sharedmock::SharedMultiLayerStorage, bcos::test::sharedmock::SharedMockExecutor,
    bcos::test::sharedmock::SharedMockScheduler, bcos::ledger::LedgerInterface>;
