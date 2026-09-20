#pragma once

// Test-only compatibility wrapper: production selects forks via OpForkSchedule timestamps.
// Do not include from production translation units.

#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
struct OpForkFlags
{
    bool jovianActive = false;
};

inline const OpForkConfig& configAt(const OpForkFlags& flags) noexcept
{
    return flags.jovianActive ? jovianConfig() : isthmusConfig();
}
}  // namespace bcos::evm::opstack
