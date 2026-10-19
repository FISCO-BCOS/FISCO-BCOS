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
 * @file NodeConfigOpForkScheduleTest.cpp
 * @brief config.genesis [op_fork_schedule] is no longer read: the OP lane's schedule comes
 *        from [op_fork_timestamps] alone.
 */

#include "EthLaneGenesisFixture.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpForkScheduleTest)

namespace
{
std::string genesisFor(int version, std::string const& sections)
{
    return std::string(
               "[version]\ncompatibility_version=3.18.0\n"
               "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
               "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\n"
               "leader_period=1\nnode.0=") +
           std::string(128, '1') +
           ":1:1\n"
           "[tx]\ngas_limit=3000000000\n"
           "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
           "auth_admin_account=0x0000000000000000000000000000000000000001\n"
           "version=" +
           std::to_string(version) + "\n" + sections + ethLaneGenesisSections();
}

std::string pinOf(std::string const& genesis)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    cfg.loadGenesisConfigFromString(genesis);
    return generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
}
}  // namespace

// A v3 genesis that also carries [op_fork_schedule] loads as if the section were absent: a
// canonical that disagrees with [op_fork_timestamps], one the old loader refused as malformed,
// and one with an unknown key all leave the schedule and the genesis pin to
// [op_fork_timestamps].
BOOST_AUTO_TEST_CASE(opLaneIgnoresTheSection)
{
    const std::string timestamps = "[op_fork_timestamps]\njovian_time=0\nkarst_time=500\n";
    auto const expectedPin = pinOf(genesisFor(3, timestamps));
    for (auto const* section : {"[op_fork_schedule]\ncanonical=0:isthmus,100:jovian\n",
             "[op_fork_schedule]\ncanonical=0:holocene,1:jovian\n",
             "[op_fork_schedule]\ncanonica=0:isthmus\n"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_REQUIRE_NO_THROW(
            cfg.loadGenesisConfigFromString(genesisFor(3, timestamps + section)));
        BOOST_REQUIRE(cfg.opForkSchedule().has_value());
        BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_jovianTime, 0U);
        BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_karstTime, 500U);
        BOOST_CHECK_EQUAL(
            generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig()), expectedPin);
    }
}

// Off the OP lane the section is not read either (it used to refuse to start there).
BOOST_AUTO_TEST_CASE(v2IgnoresTheSection)
{
    const std::string timestamps =
        "[fork_timestamps]\nlondon_time=0\nparis_time=0\nshanghai_time=0\ncancun_time=0\n"
        "prague_time=0\nmerge_block=0\n";
    BOOST_CHECK_EQUAL(pinOf(genesisFor(2, timestamps + "[op_fork_schedule]\ncanonical=0:jovian\n")),
        pinOf(genesisFor(2, timestamps)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
