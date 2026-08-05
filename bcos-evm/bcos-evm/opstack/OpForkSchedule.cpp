#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>

namespace bcos::evm::opstack
{
const OpForkConfig& ecotoneConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Ecotone,
        .rev = EVMC_CANCUN,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = true,
    };
    return cfg;
}

const OpForkConfig& fjordConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Fjord,
        .rev = EVMC_CANCUN,
        .precompiles = &fjordPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& graniteConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Granite;
        c.precompiles = &granitePrecompileOverrides();
        return c;
    }();
    return cfg;
}

const OpForkConfig& holoceneConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = fjordConfig();
        c.fork = OpFork::Holocene;
        c.precompiles = &granitePrecompileOverrides();
        return c;
    }();
    return cfg;
}

const OpForkConfig& isthmusConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Isthmus,
        .rev = EVMC_PRAGUE,
        .precompiles = &isthmusPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& jovianConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Jovian,
        .rev = EVMC_PRAGUE,
        .precompiles = &jovianPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

// Karst execution/receipt behavior is temporarily identical to Jovian (see README); derived from
// jovianConfig, changing only the fork tag, the same pattern as granite/holocene deriving from
// fjord -- future Jovian changes are automatically carried into Karst, avoiding parallel-literal
// drift.
//
// Not wired into configAt on purpose (audit D4, 2026-08-03): op-geth's `IsOptimismKarst`
// (params/config.go:1087-1088) has zero execution-layer call sites -- KarstTime is only used for
// config compat checks, the startup banner, and LatestFork ordering. Karst currently carries no
// behavior distinct from Jovian on either side, so leaving this config unreachable matches
// op-geth. Revisit configAt (`timestamp >= karstTime -> karstConfig()`) only if op-geth later
// defines Karst-specific semantics.
const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = jovianConfig();
        c.fork = OpFork::Karst;
        return c;
    }();
    return cfg;
}

const OpForkConfig& configAt(uint64_t timestamp, const OpForkTimestamps& thresholds) noexcept
{
    // decision A5: [isthmusTime, jovianTime) -> Isthmus, [jovianTime, +inf) -> Jovian; timestamps
    // below isthmusTime also fall through to Isthmus (see header comment — no pre-Isthmus config
    // exists in this minimal loop).
    if (timestamp >= thresholds.jovianTime)
        return jovianConfig();
    return isthmusConfig();
}
}  // namespace bcos::evm::opstack
