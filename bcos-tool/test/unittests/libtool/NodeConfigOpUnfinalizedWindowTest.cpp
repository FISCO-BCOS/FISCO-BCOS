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
 * @file NodeConfigOpUnfinalizedWindowTest.cpp
 * @brief [op_engine_rpc] unfinalized_window: default, bounds, and the mpt_prune_window pairing
 */

#include "NodeConfigLoaderProbe.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
namespace
{
bool errinfoContains(InvalidConfig const& e, std::string const& needle)
{
    auto const* comment = boost::get_error_info<errinfo_comment>(e);
    return comment != nullptr && comment->find(needle) != std::string::npos;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeConfigOpUnfinalizedWindowTest)

// Default 1024 ≈ 34 min at 2 s/block: op-node's ~12.8 min L1 finality plus batcher lag.
BOOST_AUTO_TEST_CASE(unfinalizedWindowDefault)
{
    LoaderProbe probe;
    probe.loadOpEngineRpcConfig({});
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 1024);
}

BOOST_AUTO_TEST_CASE(unfinalizedWindowPopulated)
{
    LoaderProbe probe;
    auto pt = fromIni("[op_engine_rpc]\nenable=true\nunfinalized_window=4096\n");
    probe.loadOpEngineRpcConfig(pt);
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 4096);
    // Both bounds are inclusive.
    for (auto const* ini : {"[op_engine_rpc]\nunfinalized_window=16\n",
             "[op_engine_rpc]\nunfinalized_window=100000\n"})
    {
        LoaderProbe edge;
        BOOST_REQUIRE_NO_THROW(edge.loadOpEngineRpcConfig(fromIni(ini)));
    }
}

// Below 16 op-node could never keep its unsafe head ahead of finalized; above 100000 the
// window is no bound at all; non-numeric is a parse error like every other int key.
BOOST_AUTO_TEST_CASE(unfinalizedWindowOutOfRangeRejected)
{
    for (auto const* ini : {"[op_engine_rpc]\nunfinalized_window=15\n",
             "[op_engine_rpc]\nunfinalized_window=0\n", "[op_engine_rpc]\nunfinalized_window=-1\n",
             "[op_engine_rpc]\nunfinalized_window=100001\n"})
    {
        LoaderProbe probe;
        BOOST_CHECK_EXCEPTION(
            probe.loadOpEngineRpcConfig(fromIni(ini)), InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "op_engine_rpc.unfinalized_window must be in");
            });
    }
}

// Pairing with storage.mpt_prune_window (validateELModeInvariants): an unfinalized block's
// incremental MPT build dereferences its parent's trie nodes down to the finalized tip, so
// pruning (when enabled) must retain at least the window's depth. Same shape as the
// [ethereum] reorg_window gate; only gated when the OP Engine listener is enabled.
BOOST_AUTO_TEST_CASE(unfinalizedWindowVsMptPruneWindow)
{
    auto load = [](LoaderProbe& probe, std::string const& ini) {
        auto pt = fromIni(ini);
        probe.loadOpEngineRpcConfig(pt);
        probe.loadStorageConfig(pt);
    };
    // Pruning disabled: always fine.
    {
        LoaderProbe probe;
        load(probe, "[op_engine_rpc]\nenable=true\n");
        BOOST_REQUIRE_NO_THROW(probe.validateELModeInvariants());
    }
    // Pruning at or above the window: accepted.
    {
        LoaderProbe probe;
        load(probe,
            "[op_engine_rpc]\nenable=true\nunfinalized_window=100\n[storage]\nmpt_prune_window="
            "100\n");
        BOOST_REQUIRE_NO_THROW(probe.validateELModeInvariants());
    }
    // Pruning below the window: rejected.
    {
        LoaderProbe probe;
        load(probe, "[op_engine_rpc]\nenable=true\n[storage]\nmpt_prune_window=100\n");
        BOOST_CHECK_EXCEPTION(probe.validateELModeInvariants(), InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "must be -1 (disabled) or >= op_engine_rpc.unfinalized_window");
        });
    }
    // Listener disabled: the window is unused, so the pairing is not enforced.
    {
        LoaderProbe probe;
        load(probe, "[op_engine_rpc]\nenable=false\n[storage]\nmpt_prune_window=100\n");
        BOOST_REQUIRE_NO_THROW(probe.validateELModeInvariants());
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
