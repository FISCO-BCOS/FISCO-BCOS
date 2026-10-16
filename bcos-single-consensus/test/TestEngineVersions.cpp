/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TestEngineVersions.cpp
 * @brief Pins the fork -> Engine API version triple mapping (engineVersionsFor). A silent
 *        edit to the table (e.g. EVMC_SHANGHAI -> EVMC_LONDON, or a changed triple) must go
 *        red here: the failure would otherwise resurface as "cannot produce blocks" on a
 *        live chain with no test naming the cause.
 */

#include "bcos-single-consensus/EngineVersions.h"
#include <boost/test/unit_test.hpp>
#include <optional>

using namespace bcos::single_consensus;

namespace
{
std::uint32_t v(bcos::engine::ApiVersion version)
{
    return static_cast<std::uint32_t>(version);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineVersionsTest)

BOOST_AUTO_TEST_CASE(forkToVersionTriple)
{
    using bcos::engine::ApiVersion;
    struct Case
    {
        std::optional<evmc_revision> revision;
        EngineVersions expected;
    };
    EngineVersions const karst{v(ApiVersion::V3), v(ApiVersion::V5), v(ApiVersion::V4)};
    EngineVersions const shanghai{v(ApiVersion::V2), v(ApiVersion::V2), v(ApiVersion::V2)};
    EngineVersions const legacy{v(ApiVersion::V1), v(ApiVersion::V1), v(ApiVersion::V1)};
    Case const cases[] = {
        // Unknown revision (a chain with no on-chain evmc_revision row): historical Karst
        // dialect.
        {std::nullopt, karst},
        // Cancun and later: V3 attributes / Karst getPayload / newPayload shapes.
        {EVMC_CANCUN, karst},
        {EVMC_PRAGUE, karst},
        {EVMC_OSAKA, karst},
        // Shanghai: V2 (withdrawals, no parentBeaconBlockRoot).
        {EVMC_SHANGHAI, shanghai},
        // Pre-Shanghai: V1 (neither field exists).
        {EVMC_PARIS, legacy},
        {EVMC_LONDON, legacy},
        {EVMC_FRONTIER, legacy},
    };
    for (auto const& c : cases)
    {
        auto const actual = engineVersionsFor(c.revision);
        BOOST_TEST_INFO(
            "revision=" << (c.revision ? std::to_string(static_cast<int>(*c.revision)) : "none"));
        BOOST_CHECK_EQUAL(actual.forkchoice, c.expected.forkchoice);
        BOOST_CHECK_EQUAL(actual.getPayload, c.expected.getPayload);
        BOOST_CHECK_EQUAL(actual.newPayload, c.expected.newPayload);
    }
}

BOOST_AUTO_TEST_SUITE_END()
