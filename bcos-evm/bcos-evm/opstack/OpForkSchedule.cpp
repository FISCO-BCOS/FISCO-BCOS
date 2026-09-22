#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/GenesisConfig.h>

namespace bcos::evm::opstack
{
// Bedrock and Regolith run a Paris EVM: op-sepolia-class OP chains are post-merge (the
// Merge happened at genesis), so the pre-Canyon ladder never touches London. nullptr
// precompiles selects evmone's built-in table for the revision (same pattern as
// ecotoneConfig); the OP-specific override tables only exist Fjord+.
const OpForkConfig& bedrockConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Bedrock,
        .rev = EVMC_PARIS,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .has_ecotone_l1_formula = false,
        .has_legacy_l1_formula = true,
        .regolith_deposit_fixes = false,
        .has_deposit_receipt_version = false,
        .has_withdrawals = false,
    };
    return cfg;
}

const OpForkConfig& regolithConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = bedrockConfig();
        c.fork = OpFork::Regolith;
        c.regolith_deposit_fixes = true;
        return c;
    }();
    return cfg;
}

// Canyon moves the EVM base to Shanghai (EIP-1153/5656/6780; 4895 is consensus-only on an
// L2 — headers carry an always-empty withdrawals list) and introduces depositReceiptVersion.
const OpForkConfig& canyonConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = regolithConfig();
        c.fork = OpFork::Canyon;
        c.rev = EVMC_SHANGHAI;
        c.has_deposit_receipt_version = true;
        c.has_withdrawals = true;
        return c;
    }();
    return cfg;
}

// Delta changes nothing on the EL (span batches are a derivation-layer feature); it is
// kept in the ladder to mirror op-node's naming and rollup.json keying.
const OpForkConfig& deltaConfig() noexcept
{
    static const OpForkConfig cfg = [] {
        OpForkConfig c = canyonConfig();
        c.fork = OpFork::Delta;
        return c;
    }();
    return cfg;
}

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
        .has_legacy_l1_formula = false,
        .regolith_deposit_fixes = true,
        .has_deposit_receipt_version = true,
        .has_withdrawals = true,
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
        .has_legacy_l1_formula = false,
        .regolith_deposit_fixes = true,
        .has_deposit_receipt_version = true,
        .has_withdrawals = true,
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
        .has_legacy_l1_formula = false,
        .regolith_deposit_fixes = true,
        .has_deposit_receipt_version = true,
        .has_withdrawals = true,
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
        .has_legacy_l1_formula = false,
        .regolith_deposit_fixes = true,
        .has_deposit_receipt_version = true,
        .has_withdrawals = true,
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
    // op-node keying (op-node/rollup/types.go): IsKarst(ts) / IsJovian(ts) / ... are
    // `Time != nil && ts >= *Time`, with UINT64_MAX standing in for nil, so an unscheduled
    // fork never activates and `ts >= UINT64_MAX` skips its rung. Latest fork first — a
    // chain that activates two forks at the same second runs the later one, matching
    // op-node's own ordering of the IsX checks. The schedule's non-decreasing order over
    // the scheduled entries is a config-load invariant (NodeConfig::loadOpForkTimestamps),
    // not re-checked here.
    if (timestampSec >= schedule.m_karstTime)
    {
        return karstConfig();
    }
    if (timestampSec >= schedule.m_jovianTime)
    {
        return jovianConfig();
    }
    // Baseline compatibility: an unset isthmus_time means "Isthmus is the zero-start
    // baseline" — the only shape existing chains have (they configure jovian/karst at
    // most). Every timestamp below jovian_time resolves to Isthmus and the pre-Isthmus
    // rungs are never consulted, keeping the two-key schedule's behaviour bit-identical.
    // An explicitly set isthmus_time turns the full Bedrock..Karst ladder live, with
    // Bedrock — the genesis fork, which has no schedule entry — as the fallback.
    if (schedule.m_isthmusTime == std::numeric_limits<uint64_t>::max())
    {
        return isthmusConfig();
    }
    if (timestampSec >= schedule.m_isthmusTime)
    {
        return isthmusConfig();
    }
    if (timestampSec >= schedule.m_holoceneTime)
    {
        return holoceneConfig();
    }
    if (timestampSec >= schedule.m_graniteTime)
    {
        return graniteConfig();
    }
    if (timestampSec >= schedule.m_fjordTime)
    {
        return fjordConfig();
    }
    if (timestampSec >= schedule.m_ecotoneTime)
    {
        return ecotoneConfig();
    }
    if (timestampSec >= schedule.m_deltaTime)
    {
        return deltaConfig();
    }
    if (timestampSec >= schedule.m_canyonTime)
    {
        return canyonConfig();
    }
    if (timestampSec >= schedule.m_regolithTime)
    {
        return regolithConfig();
    }
    return bedrockConfig();
}
}  // namespace bcos::evm::opstack
