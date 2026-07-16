#pragma once

#include <evmc/evmc.hpp>

namespace bcos::evmref::opstack
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
    bool has_ecotone_l1_formula;  // true → Ecotone calldataGas L1；false → Fjord+ FastLZ
};

const OpForkConfig& ecotoneConfig() noexcept;
const OpForkConfig& fjordConfig() noexcept;
const OpForkConfig& graniteConfig() noexcept;
const OpForkConfig& holoceneConfig() noexcept;
const OpForkConfig& isthmusConfig() noexcept;
const OpForkConfig& jovianConfig() noexcept;
const OpForkConfig& karstConfig() noexcept;
}  // namespace bcos::evmref::opstack
