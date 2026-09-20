#pragma once

// Test-only Karst schedule helpers. Production parse names `karst` after a
// Jovian baseline or activation. TestBypass remains for fixtures that skip
// the ledger codec. Do not call from Initializer.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <cstdint>
#include <memory>

namespace opstack_test
{
[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> karstOnlySchedule(
    uint64_t karstTs);

[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> isthmusThenJovian(
    uint64_t jovianTs);

[[nodiscard]] std::shared_ptr<bcos::evm::opstack::OpForkSchedule> legacySchedule(bool jovianActive);
}  // namespace opstack_test
