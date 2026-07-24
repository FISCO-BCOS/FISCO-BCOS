// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.cpp
#include <bcos-evm/adapter/StateRootCompute.h>

#include <bcos-evm/eth/utils/mpt_hash.hpp>

namespace bcos::evmref
{
evmone::hash256 stateRootOf(const evmone::test::TestState& state)
{
    return evmone::state::mpt_hash(state);
}
}  // namespace bcos::evmref
