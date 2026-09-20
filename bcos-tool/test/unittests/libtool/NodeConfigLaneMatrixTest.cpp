/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// The lane x key matrix: which config-genesis key belongs to which executor lane, whether it is
// required/forbidden/optional there, and whether it must reach the genesis pin. Before this,
// "is this key allowed here?" was hand-written if-pairs in validateL2Invariants (plus more in
// validateELModeInvariants, libinitializer and transaction-scheduler), so a new lane-specific
// key had no defined home. This suite pins the OP section family's contract as a table.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/ChainLaneConfig.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigLaneMatrixTest)

namespace
{
std::string opSectionBody(std::string_view section)
{
    if (section == "op_fork_timestamps")
    {
        return "jovian_time=0\n";
    }
    if (section == "op_eip1559")
    {
        return "elasticity=2\ndenominator=8\n";
    }
    return {};
}
}  // namespace

// The table must actually cover the OP section family, and every rule must cite where its
// behaviour was reverse-engineered from — an uncited rule is a guess someone will trust.
BOOST_AUTO_TEST_CASE(laneMatrixCoversTheOpSectionFamily)
{
    auto const& rules = laneKeyRules();
    auto find = [&](std::string_view section) -> LaneKeyRule const* {
        for (auto const& rule : rules)
        {
            if (rule.section == section)
            {
                return &rule;
            }
        }
        return nullptr;
    };
    // (section, pinned): op_fork_schedule's canonical lives in chain metadata bound to the
    // genesis hash instead of the pin string; the timestamps' zero-valued entries and the
    // eip1559 triple are pinned outright.
    std::pair<std::string_view, bool> const expected[] = {
        {"op_fork_schedule", false}, {"op_fork_timestamps", true}, {"op_eip1559", true}};
    for (auto const& [section, pinned] : expected)
    {
        auto const* rule = find(section);
        BOOST_REQUIRE_MESSAGE(rule != nullptr, "matrix is missing " << section);
        BOOST_CHECK(rule->lane == ChainLane::Op);
        BOOST_CHECK(
            rule->presence == KeyPresence::Required || rule->presence == KeyPresence::Optional);
        BOOST_CHECK_EQUAL(rule->pinned, pinned);
        BOOST_CHECK_MESSAGE(!rule->reason.empty(), section << " must cite its provenance");
        BOOST_CHECK_MESSAGE(
            rule->rejectMessage.find("requires executor.version >= 3") != std::string::npos,
            section << " rejection must name the lane requirement");
    }
}

// The same shape the hand-written check used to reject, now sourced from the table: a v2 chain
// declaring an OP-only section gets that rule's own rejection message.
BOOST_AUTO_TEST_CASE(opSectionOnANonOpLaneIsRejectedByTheMatrix)
{
    for (auto const* section : {"op_fork_timestamps", "op_eip1559"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        auto const genesis =
            std::string(
                "[version]\ncompatibility_version=3.18.0\n"
                "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
                "[web3]\nchain_id=1\n"
                "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\n"
                "leader_period=1\nnode.0=") +
            std::string(128, '1') +
            ":1:1\n"
            "[tx]\ngas_limit=3000000000\n"
            "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
            "auth_admin_account=0x0000000000000000000000000000000000000001\n"
            "version=2\nevm_revision=prague\n" +
            "[" + section + "]\n" + opSectionBody(section);
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(genesis), InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "requires executor.version >= 3 (OP lane)");
            });
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
