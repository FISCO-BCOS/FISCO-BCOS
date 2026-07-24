// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "utils.hpp"

namespace evmone::test
{

evmc_revision to_rev(std::string_view s)
{
    if (s == "Frontier")
        return EVMC_FRONTIER;
    if (s == "Homestead")
        return EVMC_HOMESTEAD;
    if (s == "Tangerine Whistle" || s == "EIP150")
        return EVMC_TANGERINE_WHISTLE;
    if (s == "Spurious Dragon" || s == "EIP158")
        return EVMC_SPURIOUS_DRAGON;
    if (s == "Byzantium")
        return EVMC_BYZANTIUM;
    if (s == "Constantinople")
        return EVMC_CONSTANTINOPLE;
    if (s == "Petersburg" || s == "ConstantinopleFix")
        return EVMC_PETERSBURG;
    if (s == "Istanbul")
        return EVMC_ISTANBUL;
    if (s == "Berlin")
        return EVMC_BERLIN;
    if (s == "London" || s == "ArrowGlacier")
        return EVMC_LONDON;
    if (s == "Paris" || s == "Merge")
        return EVMC_PARIS;
    if (s == "Shanghai")
        return EVMC_SHANGHAI;
    if (s == "Cancun")
        return EVMC_CANCUN;
    if (s == "Prague")
        return EVMC_PRAGUE;
    if (s == "Osaka")
        return EVMC_OSAKA;
    if (s == "OsakaToBPO1AtTime15k")
        return EVMC_OSAKA;
    if (s == "BPO1ToBPO2AtTime15k")
        return EVMC_OSAKA;
    if (s == "BPO2ToBPO3AtTime15k")
        return EVMC_OSAKA;
    if (s == "BPO3ToBPO4AtTime15k")
        return EVMC_OSAKA;
    if (s == "Experimental")
        return EVMC_EXPERIMENTAL;
    throw std::invalid_argument{"unknown revision: " + std::string{s}};
}

RevisionSchedule to_rev_schedule(std::string_view s)
{
    if (s == "BerlinToLondonAt5")
        return {EVMC_BERLIN, EVMC_LONDON, 5};
    if (s == "ParisToShanghaiAtTime15k")
        return {EVMC_PARIS, EVMC_SHANGHAI, 15'000};
    if (s == "ShanghaiToCancunAtTime15k")
        return {EVMC_SHANGHAI, EVMC_CANCUN, 15'000};
    if (s == "CancunToPragueAtTime15k")
        return {EVMC_CANCUN, EVMC_PRAGUE, 15'000};
    if (s == "PragueToOsakaAtTime15k")
        return {EVMC_PRAGUE, EVMC_OSAKA, 15'000};

    const auto single_rev = to_rev(s);
    return {single_rev, single_rev, 0};
}

}  // namespace evmone::test
