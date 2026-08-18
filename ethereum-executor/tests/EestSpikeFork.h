/**
 * @file EestSpikeFork.h
 * @brief Fork-key selector for the eest-runner opTransition spike track.
 *
 * Kept as a dependency-free inline header so the predicate can be unit-tested by the
 * standalone eest-json-tests target (which links only jsoncpp_static and
 * Boost::unit_test_framework) without pulling in EESTRunner.cpp or the evmone headers.
 */
#pragma once

#include <cctype>
#include <string>

namespace bcos::test
{
/// Pure predicate: select which EEST post fork-key enters the opTransition spike track.
///
/// Only pure "Prague" is semantically aligned with isthmusConfig/jovianConfig (both carry
/// rev = EVMC_PRAGUE). Everything else is skipped:
///   - "Osaka" — Osaka-only EIPs systematically fail under the Prague revision;
///   - fork transitions (e.g. "CancunToPragueAtTime15000") — forkNameToRevision would map the
///     "to" fork to EVMC_OSAKA and wrongly admit the fixture under a Prague revision;
///   - pre-Prague forks ("Cancun", "London", ...).
///
/// Case-insensitive: EEST post keys are capitalized ("Prague"), while the unit tests also
/// exercise the normalized lowercase form.
inline bool spikeForkSelect(std::string forkKey)
{
    for (auto& c : forkKey)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return forkKey == "prague";
}
}  // namespace bcos::test
