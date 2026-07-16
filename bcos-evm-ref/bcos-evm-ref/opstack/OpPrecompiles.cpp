#include <bcos-evm-ref/opstack/OpPrecompiles.h>

namespace bcos::evmref::opstack
{
namespace
{
constexpr PrecompileOverrides::Entry kIsthmusEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 513760},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 488448},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 235008},
};

constexpr PrecompileOverrides::Entry kJovianEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 81984},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
    {.addr = evmc::address{0x0c}, .gas_cost_override = -1, .max_input_size = 288960},
    {.addr = evmc::address{0x0e}, .gas_cost_override = -1, .max_input_size = 278784},
    {.addr = evmc::address{0x0f}, .gas_cost_override = -1, .max_input_size = 156672},
};

// op-geth: 0x100 P256VERIFY is active from Fjord (contracts.go:193); the bn256 112687 limit is
// from Granite (params:172). Fjord has no bn256 limit and no BLS (CANCUN).
constexpr PrecompileOverrides::Entry kFjordEntries[] = {
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
};

constexpr PrecompileOverrides::Entry kGraniteEntries[] = {
    {.addr = evmc::address{0x08}, .gas_cost_override = -1, .max_input_size = 112687},
    {.addr = kP256VerifyAddress, .gas_cost_override = 3450, .max_input_size = 0},
};
}  // namespace

const PrecompileOverrides& isthmusPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kIsthmusEntries};
    return overrides;
}

const PrecompileOverrides& jovianPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kJovianEntries};
    return overrides;
}

const PrecompileOverrides& fjordPrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kFjordEntries};
    return overrides;
}

const PrecompileOverrides& granitePrecompileOverrides() noexcept
{
    static const PrecompileOverrides overrides{.entries = kGraniteEntries};
    return overrides;
}
}  // namespace bcos::evmref::opstack
