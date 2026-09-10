#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/GenesisConfig.h>

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

// Karst (OP "Upgrade 19") on top of Jovian: the EVM revision moves to Osaka -- EIP-7825 per-tx
// gas cap (normal transactions only; deposits stay exempt, see runDeposit), EIP-7823/7883 MODEXP,
// EIP-7939 CLZ and EIP-7951 P256VERIFY all gate on EVMC_OSAKA in the vendored state layer -- and
// bn256Pairing's input limit tightens to 57600 (karstPrecompileOverrides, which also stops
// overriding 0x100 so EIP-7951 pricing applies). Fee/receipt semantics (operator fee, DA
// footprint) are derived from jovianConfig so future Jovian changes carry into Karst.
const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = jovianConfig();
        c.fork = OpFork::Karst;
        c.rev = EVMC_OSAKA;
        c.precompiles = &karstPrecompileOverrides();
        return c;
    }();
    return cfg;
}

const OpForkConfig& configAt(
    const bcos::ledger::OpForkSchedule& schedule, uint64_t timestampSec) noexcept
{
    // op-node keying (op-node/rollup/types.go): IsKarst(ts) / IsJovian(ts) are
    // `Time != nil && ts >= *Time`, with UINT64_MAX standing in for nil, so an unscheduled
    // fork never activates. Latest fork first — a chain that activates Jovian and Karst at
    // the same second is Karst, matching op-node's own ordering of the IsX checks.
    // The schedule's non-decreasing order is a config-load invariant
    // (NodeConfig::loadOpForkTimestamps), not re-checked here.
    if (timestampSec >= schedule.m_karstTime)
    {
        return karstConfig();
    }
    if (timestampSec >= schedule.m_jovianTime)
    {
        return jovianConfig();
    }
    return isthmusConfig();
}
}  // namespace bcos::evm::opstack
