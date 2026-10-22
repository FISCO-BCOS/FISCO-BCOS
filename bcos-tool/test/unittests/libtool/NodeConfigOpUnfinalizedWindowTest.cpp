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
 * @brief [engine_rpc] unfinalized_window: default and bounds
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
    probe.loadEngineRpcConfig({});
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 1024);
}

BOOST_AUTO_TEST_CASE(unfinalizedWindowPopulated)
{
    LoaderProbe probe;
    auto pt = fromIni("[engine_rpc]\nunfinalized_window=4096\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 4096);
    // Both bounds are inclusive.
    for (auto const* ini :
        {"[engine_rpc]\nunfinalized_window=16\n", "[engine_rpc]\nunfinalized_window=100000\n"})
    {
        LoaderProbe edge;
        BOOST_REQUIRE_NO_THROW(edge.loadEngineRpcConfig(fromIni(ini)));
    }
}

// Below 16 op-node could never keep its unsafe head ahead of finalized; above 100000 the
// window is no bound at all; non-numeric is a parse error like every other int key.
BOOST_AUTO_TEST_CASE(unfinalizedWindowOutOfRangeRejected)
{
    for (auto const* ini :
        {"[engine_rpc]\nunfinalized_window=15\n", "[engine_rpc]\nunfinalized_window=0\n",
            "[engine_rpc]\nunfinalized_window=-1\n", "[engine_rpc]\nunfinalized_window=100001\n"})
    {
        LoaderProbe probe;
        BOOST_CHECK_EXCEPTION(
            probe.loadEngineRpcConfig(fromIni(ini)), InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "engine_rpc.unfinalized_window must be in");
            });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
