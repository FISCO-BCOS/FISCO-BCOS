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
struct WindowConfigProbe : LoaderProbe
{
    explicit WindowConfigProbe(int executorVersion = ledger::OPSTACK_EXECUTOR_VERSION)
    {
        boost::property_tree::ptree genesis;
        genesis.put("executor.version", executorVersion);
        genesis.put("executor.auth_admin_account", "0x0000000000000000000000000000000000000001");
        loadExecutorConfig(genesis);
    }
};

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
    WindowConfigProbe probe;
    probe.loadEngineRpcConfig({});
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 1024);
}

BOOST_AUTO_TEST_CASE(unfinalizedWindowPopulated)
{
    WindowConfigProbe probe;
    auto pt = fromIni("[engine_rpc]\nunfinalized_window=2048\n");
    probe.loadEngineRpcConfig(pt);
    BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 2048);
    // Both bounds are inclusive.
    for (auto const* ini :
        {"[engine_rpc]\nunfinalized_window=16\n", "[engine_rpc]\nunfinalized_window=4096\n"})
    {
        WindowConfigProbe edge;
        BOOST_REQUIRE_NO_THROW(edge.loadEngineRpcConfig(fromIni(ini)));
    }
}

BOOST_AUTO_TEST_CASE(unfinalizedWindowOutOfRangeRejected)
{
    for (auto const* ini :
        {"[engine_rpc]\nunfinalized_window=15\n", "[engine_rpc]\nunfinalized_window=0\n",
            "[engine_rpc]\nunfinalized_window=-1\n", "[engine_rpc]\nunfinalized_window=4097\n"})
    {
        WindowConfigProbe probe;
        BOOST_CHECK_EXCEPTION(
            probe.loadEngineRpcConfig(fromIni(ini)), InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "engine_rpc.unfinalized_window must be in [16, 4096]");
            });
    }
}

BOOST_AUTO_TEST_CASE(unfinalizedWindowRejectsNonNumericValue)
{
    WindowConfigProbe probe;
    BOOST_CHECK_THROW(probe.loadEngineRpcConfig(fromIni("[engine_rpc]\nunfinalized_window=abc\n")),
        boost::property_tree::ptree_bad_data);
}

BOOST_AUTO_TEST_CASE(nonOpLanesIgnoreWindowConfig)
{
    for (auto const version : {1, ledger::ETHEREUM_EXECUTOR_VERSION})
    {
        WindowConfigProbe probe(version);
        BOOST_REQUIRE_NO_THROW(
            probe.loadEngineRpcConfig(fromIni("[engine_rpc]\nunfinalized_window=abc\n")));
        BOOST_CHECK_EQUAL(probe.opUnfinalizedWindow(), 1024);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
