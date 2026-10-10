/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0

 *
 * @file NodeConfigLaneMatrixTest.cpp
 * @brief Lane-key rule matrix for the NodeConfig OP/EL lanes: required/rejected section binding per lane and the dual-channel agreement check. */

// The lane x key matrix: which config-genesis key belongs to which executor lane, whether it is
// required/forbidden/optional there, and whether it must reach the genesis pin. Before this,
// "is this key allowed here?" was hand-written if-pairs in validateL2Invariants (plus more in
// validateELModeInvariants, libinitializer and transaction-scheduler), so a new lane-specific
// key had no defined home. This suite pins the OP section family's contract as a table.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include "EthLaneGenesisFixture.h"
#include <bcos-tool/ChainLaneConfig.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <map>
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
        // isthmus_time=0 activates the full Bedrock..Karst ladder (an unset isthmus_time
        // means the Isthmus zero-start baseline, which would hide the pre-Isthmus rungs
        // under test); no loader guard couples jovian_time to isthmus_time.
        return "isthmus_time=0\njovian_time=0\n";
    }
    if (section == "op_fork_schedule")
    {
        // [op_fork_schedule].canonical is required (loadOpForkSchedule) — an empty section
        // leaves the presence hook unset and the rule never fires.
        return "canonical=0:isthmus\n";
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
    // (section, pinned, presence, rejectMessage): the table IS the enforcement source now
    // (validateL2Invariants walks laneKeyRules), so every column is pinned exactly — the
    // old permissive `Required || Optional` check let the strictest column rot.
    struct Expected
    {
        std::string_view section;
        bool pinned;
        KeyPresence presence;
        std::string_view rejectMessage;
    };
    const Expected expected[] = {
        {"op_fork_schedule", false, KeyPresence::Optional,
            "[op_fork_schedule] requires executor.version >= 3 (OP lane)"},
        {"op_fork_timestamps", true, KeyPresence::Required,
            "[op_fork_timestamps] requires executor.version >= 3 (OP lane)"},
        {"op_eip1559", true, KeyPresence::Optional,
            "[op_eip1559] requires executor.version >= 3 (OP lane)"},
    };
    for (auto const& expected_rule : expected)
    {
        auto const* rule = find(expected_rule.section);
        BOOST_REQUIRE_MESSAGE(rule != nullptr, "matrix is missing " << expected_rule.section);
        BOOST_CHECK(rule->lane == ChainLane::Op);
        BOOST_CHECK(rule->presence == expected_rule.presence);
        BOOST_CHECK_EQUAL(rule->pinned, expected_rule.pinned);
        BOOST_CHECK_MESSAGE(!rule->reason.empty(),
            expected_rule.section << " must cite its provenance");
        BOOST_CHECK_EQUAL(rule->rejectMessage, expected_rule.rejectMessage);
        // A Required rule must carry the absence message too (validateL2Invariants throws it).
        if (rule->presence == KeyPresence::Required)
        {
            BOOST_CHECK_MESSAGE(!rule->requiredMessage.empty(),
                expected_rule.section << " is Required but cites no absence message");
        }
    }
}

// The same shape the hand-written check used to reject, now sourced from the table: a v2 chain
// declaring an OP-only section gets that rule's own rejection message.
BOOST_AUTO_TEST_CASE(opSectionOnANonOpLaneIsRejectedByTheMatrix)
{
    for (auto const* section : {"op_fork_timestamps", "op_fork_schedule", "op_eip1559"})
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
            "[" + section + "]\n" + opSectionBody(section) + ethLaneGenesisSections();
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(genesis), InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "requires executor.version >= 3 (OP lane)");
            });
    }
}

// Required enforcement from the table: an OP chain whose [op_fork_timestamps] section
// is absent must fail with the rule's own requiredMessage (the old hand-written check's
// message, now sourced from the table).
BOOST_AUTO_TEST_CASE(opLaneWithoutTheRequiredSectionThrowsTheTableMessage)
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
        "version=3\n" +
        ethLaneGenesisSections();
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(genesis), InvalidConfig,
        [](auto const& e) {
            return errinfoContains(e,
                "executor.version >= 3 (OP lane) requires an [op_fork_timestamps] section");
        });
}

// The two fork-schedule declaration channels may coexist only when they agree on WHEN
// jovian/karst activate (review AF): the canonical text feeds the stored SYS_CONFIG row
// (snapshot readers / RPC gates), the shorthand drives the executor — a divergent dual
// declaration would boot and then price against a different ladder than it executes.
BOOST_AUTO_TEST_CASE(dualForkScheduleDeclarationMustAgreeOnJovianKarst)
{
    auto genesisWith = [](std::string_view timestamps, std::string_view canonical) {
        return std::string(
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
            "version=3\n"
            "[op_fork_timestamps]\n" +
            std::string(timestamps) + "[op_fork_schedule]\ncanonical=" + std::string(canonical) +
            "\n" + ethLaneGenesisSections();
    };

    // Agreeing: jovian at genesis on both channels.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_NO_THROW(cfg.loadGenesisConfigFromString(
            genesisWith("jovian_time=0\n", "0:jovian")));
    }
    // Agreeing via the implied jump: shorthand karst-only folds jovian to karst's second,
    // and a canonical that declares exactly that shape matches.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_NO_THROW(cfg.loadGenesisConfigFromString(
            genesisWith("karst_time=5\n", "0:isthmus,5:karst")));
    }
    // Diverging: the canonical activates jovian at 100 while the shorthand says 0 — the
    // stored row and the executor would disagree; reject with both values named.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(genesisWith("jovian_time=0\n", "0:isthmus,100:jovian")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "activates jovian/karst at");
            });
    }
    // The karst rung diverges on its own too (the jovian pair above agrees): shorthand
    // karst_time=5 vs canonical karst at 100.
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(genesisWith("karst_time=5\n", "0:isthmus,100:karst")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "activates jovian/karst at");
            });
    }
}

// The pinned field is the mechanical guard: a pinned rule's section must reach the
// genesis pin, or two nodes can disagree on a consensus-changing key without admission
// catching it. Walk the table and prove the emission, per rule, driven by this section ->
// pin-token table: a new rule added with pinned=true has no entry here and fails the
// BOOST_REQUIRE inside the loop, instead of passing vacuously as it would with a
// hardcoded per-section if-chain.
BOOST_AUTO_TEST_CASE(pinnedRulesActuallyReachTheGenesisPin)
{
    // std::less<> so a std::string_view rule.section can be looked up without a copy.
    std::map<std::string, std::string, std::less<>> const expectedPinToken{
        {"op_fork_timestamps", "[opForkTimestamps]"}, {"op_eip1559", "eip1559:"}};
    for (auto const& rule : laneKeyRules())
    {
        if (!rule.pinned)
        {
            continue;
        }
        auto const expected = expectedPinToken.find(rule.section);
        BOOST_REQUIRE_MESSAGE(expected != expectedPinToken.end(),
            "pinned rule " << rule.section
                           << " has no genesis-pin expectation: add its emitted token to "
                              "expectedPinToken (the rule's emission must be pinned, or the "
                              "table claims a guard nothing enforces)");
        // An OP-lane config carrying the section (jovian active at genesis so the
        // schedule pin is emitted; the shorthand and the canonical agree by fold).
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
            "version=3\n"
            "[op_fork_timestamps]\njovian_time=0\n"
            "[op_fork_schedule]\ncanonical=0:jovian\n"
            "[op_eip1559]\nelasticity=2\ndenominator=8\n" +
            ethLaneGenesisSections();
        BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
        auto const pin = generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
        BOOST_CHECK_MESSAGE(pin.find(expected->second) != std::string::npos,
            "pinned rule " << rule.section << " not emitted in the genesis pin (expected token "
                           << expected->second << ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
