#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
/// Test-only Karst schedule via `TestBypass`. Production `parse("…:karst")` is
/// valid after Jovian; this helper still skips codec validation.
/// Lives in `opstack` (not a nested `::test`) to avoid colliding with `evmone::test`
/// under the unity-build `using namespace bcos::evm::opstack`.
inline OpForkSchedule karstOnly()
{
    return OpForkSchedule{{{OpFork::Isthmus, 0}, {OpFork::Jovian, 1}, {OpFork::Karst, 2}},
        OpForkSchedule::TestBypass{}};
}
}  // namespace bcos::evm::opstack
