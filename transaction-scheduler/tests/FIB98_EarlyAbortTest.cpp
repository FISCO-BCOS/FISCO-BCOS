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
 * @file FIB98_EarlyAbortTest.cpp
 * @brief Verify that executeStep1 aborts early when m_hasRAW is set (FIB-98)
 */

#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <boost/test/unit_test.hpp>
#include <atomic>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
using namespace std::string_view_literals;

namespace
{

// Executor that counts how many times createExecuteContext is called.
struct CountingExecutor
{
    std::atomic<int> createCount{0};

    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        ++createCount;
        co_return {};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const&, auto&& waitOperator, auto&&...)
    {
        co_return {};
    }
};

}  // namespace

class FIB98Fixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    FIB98Fixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        multiLayerStorage(backendStorage)
    {}

    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
};

BOOST_FIXTURE_TEST_SUITE(FIB98_EarlyAbortTest, FIB98Fixture)

BOOST_AUTO_TEST_CASE(executeStep1AbortsWhenHasRAWIsSet)
{
    // This test constructs a ChunkStatus directly and verifies that
    // executeStep1 stops creating ExecuteContext objects once the
    // hasRAW flag has been set prior to the call.

    task::syncWait([&, this]() -> task::Task<void> {
        CountingExecutor executor;

        auto view = multiLayerStorage.fork();
        view.newMutable();

        constexpr int TX_COUNT = 50;
        auto transactions =
            ::ranges::iota_view<int, int>(0, TX_COUNT) | ::ranges::views::transform([](int index) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            ::ranges::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        std::vector<protocol::TransactionReceipt::Ptr> receipts(TX_COUNT);
        std::vector<ExecutionContext> contexts;
        contexts.reserve(TX_COUNT);
        for (int i = 0; i < TX_COUNT; ++i)
        {
            contexts.emplace_back(i, std::addressof(*transactions[i]), std::addressof(receipts[i]));
        }

        using ContextIterator = ::ranges::iterator_t<decltype(contexts)>;
        using ContextRange = ::ranges::subrange<ContextIterator>;
        using ViewType = decltype(view);
        using ChunkType = ChunkStatus<MutableStorage, ViewType, CountingExecutor, ContextRange>;

        // Case 1: hasRAW already set before calling executeStep1 -- expect zero contexts created.
        {
            boost::atomic_flag hasRAW;
            hasRAW.test_and_set();  // Set the flag BEFORE creating the chunk

            ContextRange contextRange(contexts);
            executor.createCount.store(0);
            ChunkType chunk(0, hasRAW, contextRange, executor, view);

            bcostars::protocol::BlockHeaderImpl blockHeader(
                [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
            ledger::LedgerConfig ledgerConfig;
            co_await chunk.executeStep1(blockHeader, ledgerConfig);

            BOOST_CHECK_EQUAL(executor.createCount.load(), 0);
        }

        // Case 2: hasRAW NOT set -- expect all contexts created.
        {
            boost::atomic_flag hasRAW;  // Not set

            ContextRange contextRange(contexts);
            executor.createCount.store(0);
            ChunkType chunk(0, hasRAW, contextRange, executor, view);

            bcostars::protocol::BlockHeaderImpl blockHeader(
                [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
            ledger::LedgerConfig ledgerConfig;
            co_await chunk.executeStep1(blockHeader, ledgerConfig);

            BOOST_CHECK_EQUAL(executor.createCount.load(), TX_COUNT);
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
