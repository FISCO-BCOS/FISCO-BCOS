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
 * @file MPTDeltaLayerTest.cpp
 * @brief MPTDeltaLayer contract + CommitObserver hook (spec §5.7, §5.6, §4.8)
 */
#include "bcos-ledger/mpt/MPTDeltaLayer.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include <boost/test/unit_test.hpp>
#include <optional>
#include <type_traits>

using namespace bcos::ledger::mpt;

BOOST_AUTO_TEST_SUITE(MPTDeltaLayerSuite)

BOOST_AUTO_TEST_CASE(EmptyLayerDefaults)
{
    MPTDeltaLayer layer;
    BOOST_CHECK(layer.newNodes.empty());
    BOOST_CHECK(layer.obsoletedNodes.empty());
    BOOST_CHECK(layer.preheatManifestsToDelete.empty());
    BOOST_CHECK(layer.stateRoot == bcos::h256{});
}

BOOST_AUTO_TEST_CASE(BuilderOutputIsTheDeltaLayer)
{
    // MPTBuildOutput (the #5310 name every builder test uses) and MPTDeltaLayer (the
    // MultiLayerStorage-facing contract, spec §5.7) must stay the SAME type — two structs
    // with identical fields would drift apart silently.
    static_assert(std::is_same_v<MPTBuildOutput, MPTDeltaLayer>);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(NoopObserverDoesNothing)
{
    NoopCommitObserver observer;
    MPTDeltaLayer layer;
    BOOST_CHECK_NO_THROW(observer.onCommit(/*blockNumber=*/42, layer));
}

BOOST_AUTO_TEST_CASE(CustomObserverReceivesDeltaOnCommit)
{
    struct CapturingObserver : CommitObserver
    {
        std::optional<bcos::protocol::BlockNumber> lastBlock;
        std::optional<MPTDeltaLayer> lastDelta;
        void onCommit(bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
        {
            lastBlock = blockNumber;
            lastDelta = delta;
        }
    };

    MPTDeltaLayer layer;
    layer.stateRoot =
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000aa"};
    layer.newNodes[layer.stateRoot] = bcos::bytes{0x01, 0x02};
    layer.preheatManifestsToDelete.push_back(bcos::Address{});

    CapturingObserver observer;
    CommitObserver& iface = observer;  // dispatch through the virtual interface
    iface.onCommit(7, layer);

    BOOST_REQUIRE(observer.lastBlock.has_value());
    BOOST_CHECK_EQUAL(*observer.lastBlock, 7);
    BOOST_REQUIRE(observer.lastDelta.has_value());
    BOOST_CHECK(observer.lastDelta->stateRoot == layer.stateRoot);
    BOOST_CHECK_EQUAL(observer.lastDelta->newNodes.size(), 1U);
    BOOST_CHECK_EQUAL(observer.lastDelta->preheatManifestsToDelete.size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
