// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "block.hpp"
#include "transaction.hpp"

namespace evmone::state
{
static constexpr auto GAS_LIMIT_ELASTICITY_MULTIPLIER = 2;
static constexpr auto BASE_FEE_MAX_CHANGE_DENOMINATOR = 8;

uint64_t calc_base_fee(
    int64_t parent_gas_limit, int64_t parent_gas_used, uint64_t parent_base_fee) noexcept
{
    auto parent_gas_target = parent_gas_limit / GAS_LIMIT_ELASTICITY_MULTIPLIER;

    // Special logic for block activating EIP-1559 is not implemented, because test don't cover it.
    if (parent_gas_used == parent_gas_target)
    {
        return parent_base_fee;
    }
    else if (parent_gas_used > parent_gas_target)
    {
        const auto gas_used_delta = parent_gas_used - parent_gas_target;
        const auto base_fee_delta =
            std::max(intx::uint256{parent_base_fee} * gas_used_delta / parent_gas_target /
                         BASE_FEE_MAX_CHANGE_DENOMINATOR,
                intx::uint256{1});
        return parent_base_fee + static_cast<uint64_t>(base_fee_delta);
    }
    else
    {
        const auto gas_used_delta = parent_gas_target - parent_gas_used;
        const auto base_fee_delta = intx::uint256{parent_base_fee} * gas_used_delta /
                                    parent_gas_target / BASE_FEE_MAX_CHANGE_DENOMINATOR;
        return parent_base_fee - static_cast<uint64_t>(base_fee_delta);
    }
}

uint64_t max_blob_gas_per_block(const BlobParams& blob_params) noexcept
{
    return blob_params.max * GAS_PER_BLOB;
}


intx::uint256 compute_blob_gas_price(
    const BlobParams& blob_params, uint64_t excess_blob_gas) noexcept
{
    /// A helper function approximating `factor * e ** (numerator / denominator)`.
    /// https://eips.ethereum.org/EIPS/eip-4844#helpers
    static constexpr auto fake_exponential = [](uint64_t factor, uint64_t numerator,
                                                 uint64_t denominator) noexcept {
        intx::uint256 i = 1;
        intx::uint256 output = 0;
        intx::uint256 numerator_accum = factor * denominator;
        const intx::uint256 numerator256 = numerator;
        while (numerator_accum > 0)
        {
            output += numerator_accum;
            // Ensure the multiplication won't overflow 256 bits.
            if (const auto p = intx::umul(numerator_accum, numerator256);
                p <= std::numeric_limits<intx::uint256>::max())
                numerator_accum = intx::uint256(p) / (denominator * i);
            else
                return std::numeric_limits<intx::uint256>::max();
            i += 1;
        }
        return output / denominator;
    };

    static constexpr auto MIN_BLOB_GASPRICE = 1;
    const auto fraction = blob_params.base_fee_update_fraction;
    return fake_exponential(MIN_BLOB_GASPRICE, excess_blob_gas, fraction);
}

uint64_t calc_excess_blob_gas(evmc_revision rev, const BlobParams& blob_params,
    uint64_t parent_blob_gas_used, uint64_t parent_excess_blob_gas, uint64_t parent_base_fee,
    const intx::uint256& parent_blob_base_fee) noexcept
{
    /// The base cost of a blob (EIP-7918).
    constexpr auto BLOB_BASE_COST = 0x2000;

    const auto target_blob_gas_per_block = uint64_t{blob_params.target} * GAS_PER_BLOB;
    if (parent_excess_blob_gas + parent_blob_gas_used < target_blob_gas_per_block)
        return 0;

    if (rev >= EVMC_OSAKA && BLOB_BASE_COST * parent_base_fee > GAS_PER_BLOB * parent_blob_base_fee)
        return parent_excess_blob_gas +
               parent_blob_gas_used * (blob_params.max - blob_params.target) / blob_params.max;

    return parent_excess_blob_gas + parent_blob_gas_used - target_blob_gas_per_block;
}
}  // namespace evmone::state
