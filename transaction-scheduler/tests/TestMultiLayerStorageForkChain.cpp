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
 * @file TestMultiLayerStorageForkChain.cpp
 * @brief MultiLayerStorage::forkChain — explicit ancestor-chain views (unfinalized window)
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace std::string_view_literals;

namespace
{
class ForkChainFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;
    using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendStorage>;
    using MLS = MultiLayerStorage<MutableStorage, void, CheckpointBackend>;

    ForkChainFixture() : checkpointBackend(backendStorage), multiLayerStorage(checkpointBackend) {}

    /// A layer holding exactly one row: key -> value.
    std::shared_ptr<MutableStorage> layerWith(std::string_view key, std::string_view value)
    {
        auto layer = std::make_shared<MutableStorage>();
        storage::Entry entry;
        entry.set(std::string(value));
        task::syncWait(storage2::writeOne(*layer, StateKey{"t"sv, key}, std::move(entry)));
        return layer;
    }

    std::optional<std::string> read(auto& view, std::string_view key)
    {
        auto entry = task::syncWait(storage2::readOne(view, StateKeyView{"t"sv, key}));
        if (!entry)
        {
            return std::nullopt;
        }
        return std::string(entry->get());
    }

    BackendStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(TestMultiLayerStorageForkChain, ForkChainFixture)

// layers = [B1, B2] (oldest first): the newest layer wins on a shared key, an older layer
// is read through where the newer one is silent, and the backend sits under both.
BOOST_AUTO_TEST_CASE(layerOrderNewestWins)
{
    {
        auto seed = multiLayerStorage.fork();
        seed.newMutable();
        storage::Entry entry;
        entry.set(std::string("backend"));
        task::syncWait(storage2::writeOne(seed, StateKey{"t"sv, "k"sv}, std::move(entry)));
        storage::Entry only;
        only.set(std::string("backend-only"));
        task::syncWait(storage2::writeOne(seed, StateKey{"t"sv, "b"sv}, std::move(only)));
        task::syncWait(multiLayerStorage.mergeView(std::move(seed)));
    }
    auto b1 = layerWith("k"sv, "b1"sv);
    auto b2 = layerWith("k"sv, "b2"sv);
    auto b1Only = layerWith("x"sv, "b1x"sv);

    auto view = multiLayerStorage.forkChain({b1, b1Only, b2});
    BOOST_CHECK_EQUAL(read(view, "k"sv).value_or("<none>"), "b2");
    BOOST_CHECK_EQUAL(read(view, "x"sv).value_or("<none>"), "b1x");
    BOOST_CHECK_EQUAL(read(view, "b"sv).value_or("<none>"), "backend-only");

    // Reversed order flips the winner: the caller owns the ordering.
    auto reversed = multiLayerStorage.forkChain({b2, b1});
    BOOST_CHECK_EQUAL(read(reversed, "k"sv).value_or("<none>"), "b1");

    // A null entry is skipped rather than dereferenced.
    auto withNull = multiLayerStorage.forkChain({b1, nullptr, b2});
    BOOST_CHECK_EQUAL(read(withNull, "k"sv).value_or("<none>"), "b2");
}

// Two sibling chains over the same parent: each chain's view sees its own layer and the
// shared parent, never the sibling's writes. The anonymous deque (pushView) is ignored.
BOOST_AUTO_TEST_CASE(siblingLayerInvisibleAndDequeIgnored)
{
    auto parent = layerWith("p"sv, "parent"sv);
    auto siblingA = layerWith("s"sv, "a"sv);
    auto siblingB = layerWith("s"sv, "b"sv);

    // Something queued on the anonymous deque must not leak into forkChain views.
    {
        auto queued = multiLayerStorage.fork();
        queued.newMutable();
        storage::Entry entry;
        entry.set(std::string("queued"));
        task::syncWait(storage2::writeOne(queued, StateKey{"t"sv, "q"sv}, std::move(entry)));
        multiLayerStorage.pushView(std::move(queued));
    }

    auto viewA = multiLayerStorage.forkChain({parent, siblingA});
    auto viewB = multiLayerStorage.forkChain({parent, siblingB});
    BOOST_CHECK_EQUAL(read(viewA, "s"sv).value_or("<none>"), "a");
    BOOST_CHECK_EQUAL(read(viewB, "s"sv).value_or("<none>"), "b");
    BOOST_CHECK_EQUAL(read(viewA, "p"sv).value_or("<none>"), "parent");
    BOOST_CHECK_EQUAL(read(viewB, "p"sv).value_or("<none>"), "parent");
    BOOST_CHECK(!read(viewA, "q"sv).has_value());
    BOOST_CHECK(!read(viewB, "q"sv).has_value());
    // fork() still sees the deque — forkChain is a different plane, not a replacement.
    auto plain = multiLayerStorage.fork();
    BOOST_CHECK_EQUAL(read(plain, "q"sv).value_or("<none>"), "queued");
    multiLayerStorage.popFrontStorage();

    // An empty chain is the committed plane.
    auto committed = multiLayerStorage.forkChain({});
    BOOST_CHECK(!read(committed, "s"sv).has_value());
    BOOST_CHECK(!read(committed, "p"sv).has_value());

    // Writes on top of a chain view land in its own mutable, not in the chain's layers.
    auto viewC = multiLayerStorage.forkChain({parent, siblingA});
    viewC.newMutable();
    storage::Entry entry;
    entry.set(std::string("c"));
    task::syncWait(storage2::writeOne(viewC, StateKey{"t"sv, "s"sv}, std::move(entry)));
    BOOST_CHECK_EQUAL(read(viewC, "s"sv).value_or("<none>"), "c");
    BOOST_CHECK_EQUAL(read(viewA, "s"sv).value_or("<none>"), "a");
}

BOOST_AUTO_TEST_SUITE_END()
