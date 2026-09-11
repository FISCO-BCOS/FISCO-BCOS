/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// [op_fork_timestamps]: the OP lane's fork schedule in config.genesis. Activation is by L2
// block timestamp in SECONDS, the same keying op-node applies to rollup.json's jovian_time /
// karst_time. The section and executor.version=3 are bound both ways, the lane refuses an
// executor.evm_revision (it derives the revision from this schedule per block), and only the
// entries equal to 0 reach the genesis pin.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpForkTimestampsTest)

namespace
{
constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();

/// Genesis with a configurable [executor] tail and an optional [op_fork_timestamps] section;
/// everything else is fixed so the OP checks are the only guards that can fire.
std::string opGenesis(std::string const& executorTail, std::string const& opSection)
{
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    return "[version]\ncompatibility_version=3.18.0\n"
           "[chain]\nsm_crypto=false\ngroup_id=group0\nchain_id=1\n"
           "[web3]\nchain_id=1\n"
           "[consensus]\nconsensus_type=pbft\nblock_tx_count_limit=1000\nleader_period=1\n"
           "node.0=" +
           node +
           ":1:1\n"
           "[tx]\ngas_limit=3000000000\n"
           "[executor]\nis_wasm=false\nis_auth_check=false\nis_serial_execute=false\n"
           "auth_admin_account=0x0000000000000000000000000000000000000001\n" +
           executorTail + opSection;
}

/// The OP lane's accepted [executor] tail: version 3, no evm_revision.
std::string opExecutor()
{
    return "version=3\n";
}

NodeConfig loadOk(std::string const& genesis)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(genesis));
    return cfg;
}
}  // namespace

// Decimal and 0x hex are both accepted, and both land in the schedule as seconds.
BOOST_AUTO_TEST_CASE(loadsDecimalAndHexTimestamps)
{
    auto cfg = loadOk(opGenesis(
        opExecutor(), "[op_fork_timestamps]\njovian_time=1700000000\nkarst_time=0x65600100\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_jovianTime, 1700000000U);
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_karstTime, 0x65600100U);
}

// An absent key is op-node's nil: the fork is not scheduled and never activates.
BOOST_AUTO_TEST_CASE(absentKeyMeansNeverActivates)
{
    auto cfg = loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_jovianTime, 0U);
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_karstTime, kNever);
}

// Karst is Jovian's rules on an Osaka EVM, so it cannot activate first.
BOOST_AUTO_TEST_CASE(decreasingScheduleRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                              "[op_fork_timestamps]\njovian_time=2000\nkarst_time=1000\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "fork activation times must be non-decreasing");
        });

    // UINT64_MAX ("not scheduled") is terminal: karst cannot be scheduled after it.
    NodeConfig unscheduledJovian(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(unscheduledJovian.loadGenesisConfigFromString(
                              opGenesis(opExecutor(), "[op_fork_timestamps]\nkarst_time=1000\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "fork activation times must be non-decreasing");
        });
}

// Garbage and negatives must fail fast rather than yield a wrong schedule.
BOOST_AUTO_TEST_CASE(malformedTimestampRejected)
{
    for (const auto* value : {"-1", "1700000000abc", "", "0x"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(opGenesis(
                opExecutor(), std::string("[op_fork_timestamps]\njovian_time=") + value + "\n")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "[op_fork_timestamps].jovian_time invalid timestamp");
            });
    }
}

// The section only makes sense on the OP lane; on a v2 chain nothing would read it.
BOOST_AUTO_TEST_CASE(sectionWithoutOpLaneRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(
            opGenesis("version=2\nevm_revision=prague\n", "[op_fork_timestamps]\njovian_time=0\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "[op_fork_timestamps] requires executor.version >= 3 (OP lane)");
        });
}

// And an OP chain without one has no way to say when Jovian or Karst activate.
BOOST_AUTO_TEST_CASE(opLaneWithoutSectionRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(), "")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "executor.version >= 3 (OP lane) requires an [op_fork_timestamps] section");
        });
}

// On the OP lane the revision is a function of the schedule, so a configured one can only
// disagree with what the chain runs.
BOOST_AUTO_TEST_CASE(opLaneRejectsExplicitEvmRevision)
{
    for (const auto* tail :
        {"version=3\nevm_revision=osaka\n", "version=3\nevm_revision_forks=0:prague,100:osaka\n"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(
                                  tail, "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e,
                    "the OP lane derives the EVM revision from [op_fork_timestamps]; remove "
                    "executor.evm_revision / evm_revision_forks");
            });
    }
}

// boost's INI reader creates no node for a section with no keys, so a header alone reads as
// an absent section. Pin that, and pin that the error says so — an operator staring at the
// section header they just typed needs to be told why it does not count.
BOOST_AUTO_TEST_CASE(emptySectionReadsAsAbsent)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(opGenesis(opExecutor(), "[op_fork_timestamps]\n")),
        InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "carrying at least one entry"); });
}

// The accepted shape. It must ALSO clear the v2 guard that demands an explicit revision —
// that guard's second exemption is exactly this lane.
BOOST_AUTO_TEST_CASE(opLaneWithScheduleAndNoRevisionLoads)
{
    auto cfg =
        loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\nkarst_time=500\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_jovianTime, 0U);
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_karstTime, 500U);
    BOOST_CHECK(!cfg.genesisConfig().m_evmcRevision.has_value());
    BOOST_CHECK(cfg.genesisConfig().m_evmcRevisionForks.empty());
}

// Only the entries that are 0 are pinned. A fork scheduled for the future must stay editable
// on a running chain, so karst_time=500 does NOT reach the genesis string.
BOOST_AUTO_TEST_CASE(genesisPinCarriesOnlyGenesisActiveForks)
{
    auto cfg =
        loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\nkarst_time=500\n"));
    BOOST_REQUIRE(cfg.ledgerConfig());
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("[opForkTimestamps]\njovian_time:0\n") != std::string::npos);
    BOOST_CHECK(data.find("karst_time") == std::string::npos);

    // Both at genesis: both pinned, in schedule order.
    auto both =
        loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n"));
    auto bothData = bcos::tool::generateGenesisData(both.genesisConfig(), *both.ledgerConfig());
    BOOST_CHECK(
        bothData.find("[opForkTimestamps]\njovian_time:0\nkarst_time:0\n") != std::string::npos);
}

// An all-future (or unscheduled) schedule emits no section at all, so the genesis string of a
// chain that has not activated anything yet stays as short as before.
BOOST_AUTO_TEST_CASE(allFutureScheduleEmitsNoSection)
{
    auto cfg =
        loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=100\nkarst_time=200\n"));
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("opForkTimestamps") == std::string::npos);
}

// Node admission compares genesis strings byte for byte, so a v2 chain's string must be
// exactly what it was before this section existed.
BOOST_AUTO_TEST_CASE(legacyV2GenesisStringUnchanged)
{
    auto cfg = loadOk(opGenesis("version=2\nevm_revision=prague\n", ""));
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("opForkTimestamps") == std::string::npos);
    // The exact expected string: any drift in the pin's shape shows up here, not in a
    // production node's admission failure.
    const std::string node =
        "1234567890123456789012345678901234567890123456789012345678901234"
        "1234567890123456789012345678901234567890123456789012345678901234";
    const std::string expected =
        "[chain]\nsm_crypto:0\nchainID: 1\ngrouID: group0\n"
        "[consensys]\nconsensus_type: pbft\nblock_tx_count_limit:1000\nleader_period:1\n"
        "[version]\ncompatibility_version:3.18.0\n"
        "[tx]\ngaslimit:3000000000\n"
        "[executor]\niswasm: 0\nisAuthCheck:0\n"
        "authAdminAccount:0x0000000000000000000000000000000000000001\nisSerialExecute:0\n"
        "evmRevision:0:prague\nepochSealerNum:4\nepochBlockNum:1000\n"
        "node.0:" +
        node + ",1\n";
    BOOST_CHECK_EQUAL(data, expected);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
