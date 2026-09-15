#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace bcos::evm::opstack
{
// The codec's protocol table is the single source of truth for fork names and this
// enum indexes it, so any insert or size change on either side must be matched on
// the other. Emits a compile error here rather than a mis-mapped name at run time
// (an append past Karst is caught by the -Wswitch in configForFork).
static_assert(ledger::detail::c_opForkNames.size() == static_cast<std::size_t>(OpFork::Karst) + 1,
    "OpFork and c_opForkNames disagree: fork order/count changed on one side only");

namespace
{
OpFork forkFromName(std::string_view forkName)
{
    const int index = ledger::detail::forkOrder(forkName);
    if (index < 0)
    {
        ledger::throwInvalidOpForkSchedule("unknown fork");
    }
    return static_cast<OpFork>(index);
}

const OpForkConfig& configForFork(OpFork fork)
{
    switch (fork)
    {
    case OpFork::Regolith:
        return regolithConfig();
    case OpFork::Canyon:
        return canyonConfig();
    case OpFork::Ecotone:
        return ecotoneConfig();
    case OpFork::Fjord:
        return fjordConfig();
    case OpFork::Granite:
        return graniteConfig();
    case OpFork::Holocene:
        return holoceneConfig();
    case OpFork::Isthmus:
        return isthmusConfig();
    case OpFork::Jovian:
        return jovianConfig();
    case OpFork::Karst:
        return karstConfig();
    }
    ledger::throwInvalidOpForkSchedule("unsupported fork config");
}

std::string forkNameFromEnum(OpFork fork)
{
    const auto index = static_cast<std::size_t>(fork);
    if (index >= ledger::detail::c_opForkNames.size())
    {
        ledger::throwInvalidOpForkSchedule("unknown fork");
    }
    return std::string(ledger::detail::c_opForkNames[index]);
}

void validateActivations(std::span<const OpForkActivation> activations)
{
    std::vector<ledger::OpForkActivationRecord> records;
    records.reserve(activations.size());
    for (const auto& activation : activations)
    {
        records.push_back(ledger::OpForkActivationRecord{
            .forkName = forkNameFromEnum(activation.fork),
            .timestamp = activation.timestamp,
        });
    }
    ledger::detail::validateScheduleRecords(records);
}

void ensureKarstIsOsaka(std::span<const OpForkActivation> activations)
{
    for (const auto& activation : activations)
    {
        // Same family as codec errors so Initializer's InvalidOpForkSchedule
        // catch maps this to InvalidConfig. karstConfig().rev is EVMC_OSAKA;
        // this fires only if that pin regresses to a Jovian alias.
        if (activation.fork == OpFork::Karst && karstConfig().rev != EVMC_OSAKA)
            ledger::throwInvalidOpForkSchedule("Karst execution config is not Osaka");
    }
}
}  // namespace

const OpForkConfig& regolithConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Regolith,
        .rev = EVMC_LONDON,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Bedrock,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& canyonConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Canyon,
        .rev = EVMC_SHANGHAI,
        .precompiles = nullptr,
        .disable_prague_requests = true,
        .has_operator_fee = false,
        .has_jovian_operator_formula = false,
        .has_da_footprint = false,
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Bedrock,
        .has_ecotone_l1_formula = false,
    };
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
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Ecotone,
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
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Fjord,
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
        c.deposit_exempt_from_max_tx_gas = false;
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
        c.deposit_exempt_from_max_tx_gas = false;
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
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Fjord,
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
        .deposit_exempt_from_max_tx_gas = false,
        .l1_fee_model = L1FeeModel::Fjord,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

const OpForkConfig& karstConfig() noexcept
{
    static const OpForkConfig cfg{
        .fork = OpFork::Karst,
        .rev = EVMC_OSAKA,
        .precompiles = &karstPrecompileOverrides(),
        .disable_prague_requests = true,
        .has_operator_fee = true,
        .has_jovian_operator_formula = true,
        .has_da_footprint = true,
        .deposit_exempt_from_max_tx_gas = true,
        .l1_fee_model = L1FeeModel::Fjord,
        .has_ecotone_l1_formula = false,
    };
    return cfg;
}

OpForkSchedule OpForkSchedule::parse(std::string_view canonical)
{
    const auto records = ledger::parseOpForkSchedule(canonical);
    std::vector<OpForkActivation> activations;
    activations.reserve(records.size());
    for (const auto& record : records)
    {
        activations.push_back(OpForkActivation{
            .fork = forkFromName(record.forkName),
            .timestamp = record.timestamp,
        });
    }
    return OpForkSchedule(std::move(activations));
}

OpForkSchedule OpForkSchedule::legacy(bool jovianActive)
{
    return parse(ledger::legacyOpForkScheduleCanonical(jovianActive));
}

OpForkSchedule OpForkSchedule::fromLedgerSchedule(const bcos::ledger::OpForkSchedule& schedule)
{
    constexpr uint64_t kUnset = std::numeric_limits<uint64_t>::max();
    if (schedule.m_jovianTime == kUnset)
    {
        return legacy(false);
    }
    std::vector<OpForkActivation> activations;
    if (schedule.m_jovianTime == 0)
    {
        // jovian_time == 0 makes Jovian the baseline itself; legacy(true) is "0:jovian".
        activations.push_back(OpForkActivation{.fork = OpFork::Jovian, .timestamp = 0});
    }
    else
    {
        activations.push_back(OpForkActivation{.fork = OpFork::Isthmus, .timestamp = 0});
        activations.push_back(
            OpForkActivation{.fork = OpFork::Jovian, .timestamp = schedule.m_jovianTime});
    }
    if (schedule.m_karstTime != kUnset)
    {
        activations.push_back(
            OpForkActivation{.fork = OpFork::Karst, .timestamp = schedule.m_karstTime});
    }
    return OpForkSchedule(std::move(activations));
}

OpForkSchedule::OpForkSchedule(std::vector<OpForkActivation> activations)
  : m_activations(std::move(activations))
{
    validateActivations(m_activations);
    ensureKarstIsOsaka(m_activations);
}

OpForkSchedule::OpForkSchedule(std::vector<OpForkActivation> activations, TestBypass)
  : m_activations(std::move(activations))
{}

OpFork OpForkSchedule::forkAt(uint64_t timestampSeconds) const
{
    if (m_activations.empty())
        ledger::throwInvalidOpForkSchedule("empty schedule");

    OpFork activeFork = m_activations.front().fork;
    for (const auto& activation : m_activations)
    {
        if (activation.timestamp > timestampSeconds)
            break;
        activeFork = activation.fork;
    }
    return activeFork;
}

const OpForkConfig& OpForkSchedule::configAt(uint64_t timestampSeconds) const
{
    return configForFork(forkAt(timestampSeconds));
}

uint64_t OpForkSchedule::baselineTimestamp() const
{
    if (m_activations.empty())
        ledger::throwInvalidOpForkSchedule("empty schedule");
    return m_activations.front().timestamp;
}

std::vector<OpForkActivation> OpForkSchedule::jovianAndLaterActivations() const
{
    std::vector<OpForkActivation> out;
    out.reserve(m_activations.size());
    for (auto const& activation : m_activations)
    {
        // No default: a new OpFork enumerator must be classified or -Wswitch/-Werror fails.
        switch (activation.fork)
        {
        case OpFork::Regolith:
        case OpFork::Canyon:
        case OpFork::Ecotone:
        case OpFork::Fjord:
        case OpFork::Granite:
        case OpFork::Holocene:
        case OpFork::Isthmus:
            break;
        case OpFork::Jovian:
        case OpFork::Karst:
            out.push_back(activation);
            break;
        }
    }
    return out;
}

// Karst (OP "Upgrade 19") on top of Jovian: the EVM revision moves to Osaka — EIP-7825 per-tx
// gas cap (normal transactions only; deposits stay exempt, see runDeposit), EIP-7823/7883
// MODEXP, EIP-7939 CLZ and EIP-7951 P256VERIFY all gate on EVMC_OSAKA in the vendored state
// layer — and bn256Pairing's input limit tightens to 57600 (karstPrecompileOverrides, which
// also stops overriding 0x100 so EIP-7951 pricing applies). Fee/receipt semantics (operator
// fee, DA footprint) match jovianConfig so future Jovian changes carry into Karst.
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
