#pragma once

#include <cstdint>
#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
enum class OpFork
{
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

struct PrecompileOverrides;

struct OpForkConfig
{
    OpFork fork;
    evmc_revision rev;
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;
    bool has_operator_fee;
    bool has_jovian_operator_formula;
    bool has_da_footprint;
    bool has_ecotone_l1_formula;  // true -> Ecotone calldataGas L1; false -> Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;

/// Fork-activation timestamps for the OP validator loop (op-validator-minimal-loop design §4.2,
/// decision A5). Injected via OpSchedulerSeam's constructor (same channel as chainId) rather than
/// read from SystemConfigs — the minimal loop only distinguishes Isthmus/Jovian, so this struct
/// intentionally does not grow the full `OpFork` enum's historical fork set.
struct OpForkTimestamps
{
    uint64_t isthmusTime;
    uint64_t jovianTime;
};

/// Resolves the OP fork config active at `timestamp` given the two injected activation
/// timestamps (decision A5): `timestamp` in [isthmusTime, jovianTime) -> Isthmus,
/// `timestamp` in [jovianTime, +inf) -> Jovian. Timestamps below `isthmusTime` also resolve to
/// Isthmus (the minimal loop is Isthmus+-only; there is no pre-Isthmus config to fall back to,
/// and the two documented test partitions never exercise this sub-isthmusTime branch — see
/// task-4-brief.md Step 1(e)). This is the single function backing both the OpSchedulerSeam
/// execution-time fork selection (design §4.2) and the engine newPayload -38005 timestamp x
/// version gate (design §6.1 step 1) — the threshold comparison must not be reimplemented at the
/// second call site.
const OpForkConfig& configAt(uint64_t timestamp, const OpForkTimestamps& thresholds) noexcept;
}  // namespace bcos::evm::opstack
