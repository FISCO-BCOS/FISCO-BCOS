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
 * @file FIB100_RevertWAWCorruptionTest.cpp
 * @brief End-to-end reproduction of CertiK FIB-100 scenario 2 (WAW + revert).
 *
 * Two chunks, each with its own storage view over a shared backend where the
 * contended key K pre-exists with value 0:
 *   - chunk 0 (Tx1): writes K = 1, commits.
 *   - chunk 1 (Tx2): writes K = 2 then REVERTS (HostContext-style rollback to
 *     the call-frame savepoint, via Rollbackable — exactly the production stack
 *     Rollbackable<ReadWriteSetStorage<View>>).
 *
 * Sequential semantics: Tx1 sets K=1, Tx2's effect is reverted -> final K = 1.
 *
 * The question this test answers: does the real SchedulerParallelImpl's WAW
 * detection (Stage 5) prevent chunk 1's reverted pre-image (K=0, read stale
 * from the backend because chunk 0's write is not yet merged) from overwriting
 * chunk 0's K=1 at the Stage 7 storage merge?
 */
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-task/Wait.h>
#include <bcos-transaction-executor/RollbackableStorage.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
using namespace std::string_view_literals;

namespace
{
// Mirrors the production stack: the chunk hands a ReadWriteSetStorage to the
// executor, which wraps it in Rollbackable and takes a savepoint at the start
// of the (only) call frame. Tx with contextID 1 reverts after writing.
struct MockRevertExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        Rollbackable<Storage> rollbackable;
        int contextID;
        StateKey key;

        ExecuteContext(Storage& storage, int id, StateKey contendedKey)
          : rollbackable(storage), contextID(id), key(std::move(contendedKey))
        {}

        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            // Mirror production: the EVM (and any REVERT) runs in step 1.
            if constexpr (step == 1)
            {
                auto savepoint = rollbackable.current();

                storage::Entry entry;
                entry.set(contextID == 0 ? "1" : "2");
                co_await storage2::writeOne(rollbackable, key, std::move(entry));

                if (contextID == 1)
                {
                    // Tx2 reverts — exactly HostContext.h:530 on EVMC_REVERT.
                    co_await rollbackable.rollback(savepoint);
                }
            }
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& /*blockHeader*/,
        protocol::Transaction const& /*transaction*/, int32_t contextID,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return ExecuteContext<std::decay_t<decltype(storage)>>(
            storage, contextID, StateKey{"t_test"sv, "K"});
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& /*storage*/,
        protocol::BlockHeader const& /*blockHeader*/, protocol::Transaction const& /*transaction*/,
        int /*contextID*/, ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
    {
        co_return {};
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FIB100_RevertWAWCorruptionTest)

BOOST_AUTO_TEST_CASE(revertedLaterChunkMustNotOverwriteEarlierWrite)
{
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage(backendStorage);

        StateKey key{"t_test"sv, "K"};

        // Pre-existing K = 0 in a committed layer.
        {
            auto view1 = multiLayerStorage.fork();
            view1.newMutable();
            auto& front = mutableStorage(view1);
            storage::Entry zero;
            zero.set("0");
            co_await storage2::writeOne(front, key, std::move(zero));
            multiLayerStorage.pushView(std::move(view1));
        }

        MockRevertExecutor executor;
        SchedulerParallelImpl<MutableStorage> scheduler;
        scheduler.m_grainSize = 1;       // each tx is its own chunk
        scheduler.m_maxConcurrency = 2;  // allow the two chunks to overlap

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });

        auto transactions =
            ::ranges::iota_view<int, int>(0, 2) | ::ranges::views::transform([](int /*index*/) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            ::ranges::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();
        auto transactionRefs =
            transactions | ::ranges::views::transform([](auto& ptr) -> auto& { return *ptr; });

        auto view = multiLayerStorage.fork();
        view.newMutable();
        ledger::LedgerConfig ledgerConfig;
        co_await scheduler.executeBlock(view, executor, blockHeader, transactionRefs, ledgerConfig);
        multiLayerStorage.pushView(std::move(view));

        // Read K through a fresh fork so all layers fall through.
        auto readView = multiLayerStorage.fork();
        auto entry = co_await storage2::readOne(readView, key);

        BOOST_REQUIRE_MESSAGE(entry.has_value(), "K disappeared entirely after execution");
        auto finalValue = boost::lexical_cast<int>(entry->get());
        BOOST_TEST_MESSAGE("Final K = " << finalValue << " (expected 1; 0 means corruption)");
        BOOST_CHECK_EQUAL(finalValue, 1);
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
