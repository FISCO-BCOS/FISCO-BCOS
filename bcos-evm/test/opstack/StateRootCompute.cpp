// Test-side stateRootOf (moved from bcos-evm/adapter/: today its only consumer is
#include "StateRootCompute.h"

// TODO(eth-utils-removal): 去除本 include,mpt_hash 调用改为自研 MPT 建根实现。
#include <test/utils/mpt_hash.hpp>

namespace bcos::evm
{
evmone::hash256 stateRootOf(const evmone::test::TestState& state)
{
    return evmone::state::mpt_hash(state);
}
}  // namespace bcos::evm
