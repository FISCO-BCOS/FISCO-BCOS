#pragma once

#include "bcos-evm/eth/state/State.hpp"

namespace bcos::evm
{
inline bool canTransfer(
    state::EvmStateReader const& state, evmc_address const& from, bcos::u256 const& value)
{
    return state.get_balance(from) >= value;
}

inline void transfer(
    state::State& state, evmc_address const& from, evmc_address const& to, bcos::u256 const& value)
{
    if (value == 0)
    {
        return;
    }
    state.set_balance(from, state.get_balance(from) - value);
    state.set_balance(to, state.get_balance(to) + value);
}
}  // namespace bcos::evm
