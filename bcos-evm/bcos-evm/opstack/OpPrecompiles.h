#pragma once

#include <evmc/evmc.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace bcos::evmref::opstack
{
/// The only gas-override (non-length-limit-only) precompile currently implemented: 0x100
/// P256Verify.
inline constexpr evmc::address kP256VerifyAddress{0x100};

struct PrecompileOverrides
{
    struct Entry
    {
        evmc::address addr;
        int64_t gas_cost_override;
        size_t max_input_size;
    };

    std::span<const Entry> entries;

    [[nodiscard]] const Entry* find(const evmc::address& addr) const noexcept
    {
        for (const auto& entry : entries)
        {
            if (entry.addr == addr)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(const evmc::address& addr) const noexcept
    {
        return find(addr) != nullptr;
    }
};

const PrecompileOverrides& isthmusPrecompileOverrides() noexcept;
const PrecompileOverrides& jovianPrecompileOverrides() noexcept;
const PrecompileOverrides& fjordPrecompileOverrides() noexcept;
const PrecompileOverrides& granitePrecompileOverrides() noexcept;
}  // namespace bcos::evmref::opstack
