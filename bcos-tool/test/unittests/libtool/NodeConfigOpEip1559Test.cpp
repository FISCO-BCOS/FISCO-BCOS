/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// [op_eip1559]: the chain's EIP-1559 triple (op-deployer's config.optimism, the same numbers
// rollup.json carries as chain_op_config) as a chain-level, genesis-frozen key. The engine
// hardcoded the OP mainnet preset before this section existed, so a chain with its own
// denominator (the corpus devnet and the C2 e2e both declare 8) priced every pre-Canyon block
// differently from its own op-geth — and engine_newPayload rejected those valid blocks.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/engine/OpEip1559Params.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpEip1559Test)

namespace
{
/// The OP schedule section every version=3 case needs (the lane requires one to be present).
constexpr const char* kSchedule = "[op_fork_timestamps]\njovian_time=0\n";

/// Genesis with a configurable [executor] tail and the OP sections passed verbatim; everything
/// else is fixed so the EIP-1559 checks are the only guards that can fire (same skeleton as
/// NodeConfigOpForkTimestampsTest::opGenesis, kept local: no shared header exists for these).
/// The caller composes the OP sections so a case can isolate one section — e.g. the lane check
/// for [op_eip1559] must not be pre-empted by the schedule section's own lane violation.
std::string opGenesis(std::string const& executorTail, std::string const& opSections)
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
           executorTail + opSections;
}

/// The OP lane's accepted [executor] tail: version 3, no evm_revision.
std::string opExecutor()
{
    return "version=3\n";
}
}  // namespace

BOOST_AUTO_TEST_CASE(effectiveValueFallsBackToTheLegacyPreset)
{
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).elasticity, 6U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).denominator, 50U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).denominatorCanyon, 250U);

    bcos::engine::OpEip1559Params const declared{
        .elasticity = 2, .denominator = 8, .denominatorCanyon = 250};
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(declared).elasticity, 2U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(declared).denominator, 8U);
}

BOOST_AUTO_TEST_CASE(loadsTheTripleAndDefaultsTheCanyonDenominator)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(opGenesis(
        opExecutor(), std::string(kSchedule) + "[op_eip1559]\nelasticity=2\ndenominator=8\n")));
    BOOST_REQUIRE(cfg.opEip1559().has_value());
    BOOST_CHECK_EQUAL(cfg.opEip1559()->elasticity, 2U);
    BOOST_CHECK_EQUAL(cfg.opEip1559()->denominator, 8U);
    // Absent denominator_canyon normalizes to op-deployer's standard value AT PARSE TIME, so
    // exactly one place applies the default and the pin records the effective triple.
    BOOST_CHECK_EQUAL(cfg.opEip1559()->denominatorCanyon, 250U);
}

BOOST_AUTO_TEST_CASE(absentSectionLeavesTheTripleUnset)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(), kSchedule)));
    BOOST_CHECK(!cfg.opEip1559().has_value());
    // ... and the engine/pin both fall back to the legacy preset through ONE function.
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(cfg.opEip1559()).denominator, 50U);
}

BOOST_AUTO_TEST_CASE(missingRequiredKeyRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                              std::string(kSchedule) + "[op_eip1559]\ndenominator=8\n")),
        InvalidConfig,
        [](auto const& e) { return errinfoContains(e, "[op_eip1559].elasticity is required"); });
}

BOOST_AUTO_TEST_CASE(zeroValuesRejected)
{
    for (const auto* body : {"elasticity=0\ndenominator=8\n", "elasticity=2\ndenominator=0\n",
             "elasticity=2\ndenominator=8\ndenominator_canyon=0\n"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(
                                  opExecutor(), std::string(kSchedule) + "[op_eip1559]\n" + body)),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "[op_eip1559] values must be non-zero");
            });
    }
}

BOOST_AUTO_TEST_CASE(hexValuesAcceptedLikeTheSiblingScheduleSection)
{
    // [op_fork_timestamps] accepts both spellings (loadsDecimalAndHexTimestamps); the EIP-1559
    // triple must not surprise an operator who writes one section hex-formatted, the other
    // decimal.
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(opGenesis(
        opExecutor(), std::string(kSchedule) + "[op_eip1559]\nelasticity=0x2\ndenominator=0x8\n")));
    BOOST_REQUIRE(cfg.opEip1559().has_value());
    BOOST_CHECK_EQUAL(cfg.opEip1559()->elasticity, 2U);
    BOOST_CHECK_EQUAL(cfg.opEip1559()->denominator, 8U);
}

BOOST_AUTO_TEST_CASE(malformedValueRejected)
{
    for (const auto* value : {"abc", "-1", "8abc", "0x"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                                  std::string(kSchedule) + "[op_eip1559]\nelasticity=" + value +
                                      "\ndenominator=8\n")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "[op_eip1559].elasticity is not a valid uint64");
            });
    }
}

BOOST_AUTO_TEST_CASE(malformedCanyonDenominatorRejected)
{
    // denominator_canyon is optional (default 250), but a PRESENT value parses through the same
    // strict path as the other two keys. get_optional<uint64_t> would silently truncate "0xfa"
    // to 0, silently default "abc" or an overflowing value to 250, and silently accept "250abc"
    // as 250 — three silent mispricings of every pre-Canyon block on this chain.
    for (const auto* value : {"abc", "250abc", "", "18446744073709551616"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(
            cfg.loadGenesisConfigFromString(opGenesis(opExecutor(),
                std::string(kSchedule) +
                    "[op_eip1559]\nelasticity=2\ndenominator=8\ndenominator_canyon=" + value +
                    "\n")),
            InvalidConfig, [](auto const& e) {
                return errinfoContains(e, "[op_eip1559].denominator_canyon is not a valid uint64");
            });
    }
}

BOOST_AUTO_TEST_CASE(canyonDenominatorHexParsesStrictly)
{
    // "0xfa" is exactly the value get_optional<uint64_t> silently truncated to 0 (rejected as
    // zero for the wrong reason); through the strict path it parses as hex 250 — the same
    // effective value as the omitted-key default.
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(cfg.loadGenesisConfigFromString(opGenesis(
        opExecutor(), std::string(kSchedule) +
                          "[op_eip1559]\nelasticity=2\ndenominator=8\ndenominator_canyon=0xfa\n")));
    BOOST_REQUIRE(cfg.opEip1559().has_value());
    BOOST_CHECK_EQUAL(cfg.opEip1559()->denominatorCanyon, 250U);
}

BOOST_AUTO_TEST_CASE(valuesExceedingUint32Rejected)
{
    // The Holocene extraData encodes denominator/elasticity as uint32 (4-byte big-endian spans
    // in encodeOptimismExtraData): a value above UINT32_MAX would silently truncate there, so
    // every key refuses it at load. 2^32 fits a uint64, so this exercises the bound, not the
    // from_chars range check.
    for (const auto* body :
        {"elasticity=4294967296\ndenominator=8\n", "elasticity=2\ndenominator=0x100000000\n",
            "elasticity=2\ndenominator=8\ndenominator_canyon=4294967296\n"})
    {
        NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
        BOOST_CHECK_EXCEPTION(cfg.loadGenesisConfigFromString(opGenesis(
                                  opExecutor(), std::string(kSchedule) + "[op_eip1559]\n" + body)),
            InvalidConfig,
            [](auto const& e) { return errinfoContains(e, "exceeds the uint32 range"); });
    }
}

BOOST_AUTO_TEST_CASE(sectionWithoutOpLaneRejected)
{
    NodeConfig cfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_CHECK_EXCEPTION(
        cfg.loadGenesisConfigFromString(opGenesis(
            "version=2\nevm_revision=prague\n", "[op_eip1559]\nelasticity=2\ndenominator=8\n")),
        InvalidConfig, [](auto const& e) {
            return errinfoContains(e, "[op_eip1559] requires executor.version >= 3 (OP lane)");
        });
}

// The triple is a chain-level, genesis-frozen property: two nodes that disagree about it would
// price the same height differently, so it must be part of the genesis pin. It is emitted ONLY
// when declared (mirroring txGasPrice / evmRevision / excessBlobGas), so every pre-existing
// chain's pin string stays byte-identical; the value emitted is the EFFECTIVE one, so an
// explicit denominator_canyon=250 and an omitted key pin the same string.
BOOST_AUTO_TEST_CASE(genesisDataCarriesTheDeclaredEip1559Triple)
{
    NodeConfig withoutCfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(
        withoutCfg.loadGenesisConfigFromString(opGenesis(opExecutor(), kSchedule)));
    BOOST_REQUIRE(withoutCfg.ledgerConfig());
    auto const without =
        bcos::tool::generateGenesisData(withoutCfg.genesisConfig(), *withoutCfg.ledgerConfig());
    BOOST_CHECK(without.find("eip1559") == std::string::npos);

    NodeConfig withCfg(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    BOOST_REQUIRE_NO_THROW(withCfg.loadGenesisConfigFromString(opGenesis(
        opExecutor(), std::string(kSchedule) + "[op_eip1559]\nelasticity=2\ndenominator=8\n")));
    BOOST_REQUIRE(withCfg.ledgerConfig());
    auto const with =
        bcos::tool::generateGenesisData(withCfg.genesisConfig(), *withCfg.ledgerConfig());
    // denominator_canyon was omitted, so the pin must carry the normalized 250.
    BOOST_CHECK(with.find("eip1559:2,8,250") != std::string::npos);
    BOOST_CHECK_NE(without, with);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
