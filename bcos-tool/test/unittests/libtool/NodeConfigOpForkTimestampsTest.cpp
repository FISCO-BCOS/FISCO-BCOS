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
// block timestamp in SECONDS, the same keying op-node applies to rollup.json's *_time
// fields (regolith .. karst; bedrock is genesis and has no key). The section and
// executor.version=3 are bound both ways, the lane refuses an executor.evm_revision (it
// derives the revision from this schedule per block), and only the entries equal to 0
// reach the genesis pin.

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

// Karst is Jovian's rules on an Osaka EVM, so it cannot activate first. The same holds for
// any non-adjacent scheduled pair down the full ladder.
BOOST_AUTO_TEST_CASE(decreasingScheduleRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                              "[op_fork_timestamps]\njovian_time=2000\nkarst_time=1000\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "fork activation times must be non-decreasing");
        });

    // Non-adjacent pair, full ladder: ecotone cannot precede canyon.
    NodeConfig ladder(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(ladder.loadGenesisConfigFromString(
                              opGenesis(opExecutor(),
                                  "[op_fork_timestamps]\nisthmus_time=5000\ncanyon_time=2000\n"
                                  "ecotone_time=1000\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "fork activation times must be non-decreasing");
        });
}

// An unscheduled intermediate fork is skipped, not terminal: a later fork's activation
// implies it (a chain may jump straight to a later fork), so karst without jovian and
// canyon without regolith are both valid schedules.
BOOST_AUTO_TEST_CASE(unscheduledIntermediateForksAllowed)
{
    auto cfg = loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\nkarst_time=1000\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_jovianTime, kNever);
    BOOST_CHECK_EQUAL(cfg.opForkSchedule()->m_karstTime, 1000U);

    auto jump = loadOk(opGenesis(
        opExecutor(), "[op_fork_timestamps]\nisthmus_time=500\ncanyon_time=100\n"));
    BOOST_REQUIRE(jump.opForkSchedule().has_value());
    BOOST_CHECK_EQUAL(jump.opForkSchedule()->m_regolithTime, kNever);
    BOOST_CHECK_EQUAL(jump.opForkSchedule()->m_canyonTime, 100U);
    BOOST_CHECK_EQUAL(jump.opForkSchedule()->m_isthmusTime, 500U);
}

// All ten keys parse, decimal and 0x hex alike, and land on the schedule in seconds.
BOOST_AUTO_TEST_CASE(loadsFullLadder)
{
    auto cfg = loadOk(opGenesis(opExecutor(),
        "[op_fork_timestamps]\nregolith_time=100\ncanyon_time=0xc8\ndelta_time=300\n"
        "ecotone_time=400\nfjord_time=500\ngranite_time=600\nholocene_time=700\n"
        "isthmus_time=800\njovian_time=900\nkarst_time=1000\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());
    auto const& s = *cfg.opForkSchedule();
    BOOST_CHECK_EQUAL(s.m_regolithTime, 100U);
    BOOST_CHECK_EQUAL(s.m_canyonTime, 0xc8U);
    BOOST_CHECK_EQUAL(s.m_deltaTime, 300U);
    BOOST_CHECK_EQUAL(s.m_ecotoneTime, 400U);
    BOOST_CHECK_EQUAL(s.m_fjordTime, 500U);
    BOOST_CHECK_EQUAL(s.m_graniteTime, 600U);
    BOOST_CHECK_EQUAL(s.m_holoceneTime, 700U);
    BOOST_CHECK_EQUAL(s.m_isthmusTime, 800U);
    BOOST_CHECK_EQUAL(s.m_jovianTime, 900U);
    BOOST_CHECK_EQUAL(s.m_karstTime, 1000U);
}

// Without isthmus_time, configAt treats Isthmus as the zero-start baseline and never
// consults the pre-Isthmus rungs — so scheduling one there is a silently-dead entry and
// must fail fast. With isthmus_time set, the full ladder is live and the same keys load.
BOOST_AUTO_TEST_CASE(preIsthmusKeysRequireIsthmusTime)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                              "[op_fork_timestamps]\nholocene_time=100\njovian_time=200\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "[op_fork_timestamps].holocene_time requires isthmus_time");
        });

    BOOST_REQUIRE_NO_THROW(loadOk(opGenesis(opExecutor(),
        "[op_fork_timestamps]\nholocene_time=100\nisthmus_time=150\njovian_time=200\n")));
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

    // Genesis-active entries from the full ladder are pinned the same way, in fork order;
    // a future entry (karst_time=500) stays out.
    auto ladder = loadOk(opGenesis(opExecutor(),
        "[op_fork_timestamps]\nregolith_time=0\ncanyon_time=0\n"
        "isthmus_time=0\njovian_time=0\nkarst_time=500\n"));
    auto ladderData =
        bcos::tool::generateGenesisData(ladder.genesisConfig(), *ladder.ledgerConfig());
    BOOST_CHECK(ladderData.find("[opForkTimestamps]\nregolith_time:0\ncanyon_time:0\n"
                                "isthmus_time:0\njovian_time:0\n") != std::string::npos);
    BOOST_CHECK(ladderData.find("karst_time") == std::string::npos);
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

// Both keys are optional, so a typo would otherwise read as "not scheduled" and the chain
// would run Isthmus forever without a word. The section's other key is still parsed, so the
// refusal is about the name, not about the section being unreadable.
BOOST_AUTO_TEST_CASE(unrecognisedKeyRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(
                              opExecutor(), "[op_fork_timestamps]\njovain_time=0\nkarst_time=0\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(
                e, "[op_fork_timestamps] has an unrecognised key \"jovain_time\"");
        });
}

// Reload on the same NodeConfig is a supported shape (loadForkTimestamps resets its schedule
// the same way): a second genesis without the section must not keep the first one's
// schedule, or it would leak into the genesis pin of a chain that has none.
BOOST_AUTO_TEST_CASE(reloadWithoutSectionDropsTheSchedule)
{
    auto cfg =
        loadOk(opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n"));
    BOOST_REQUIRE(cfg.opForkSchedule().has_value());

    BOOST_REQUIRE_NO_THROW(
        cfg.loadGenesisConfigFromString(opGenesis("version=2\nevm_revision=prague\n", "")));
    BOOST_CHECK(!cfg.opForkSchedule().has_value());
    auto data = bcos::tool::generateGenesisData(cfg.genesisConfig(), *cfg.ledgerConfig());
    BOOST_CHECK(data.find("opForkTimestamps") == std::string::npos);

    // And the other way round: the v2 genesis's evm_revision must not survive into the OP
    // lane's "no configured revision" check on the next load.
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(
        opGenesis(opExecutor(), "[op_fork_timestamps]\njovian_time=0\nkarst_time=0\n")));
    BOOST_CHECK(cfg.opForkSchedule().has_value());
    BOOST_CHECK(!cfg.genesisConfig().m_evmcRevision.has_value());
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
