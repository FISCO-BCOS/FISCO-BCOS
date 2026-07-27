// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.cpp
#include <bcos-evm/adapter/StateRootCompute.h>

// TODO(eth-utils-removal): 去除本 include,mpt_hash 调用改为自研 MPT 建根实现。
#include <bcos-evm/eth/utils/mpt_hash.hpp>

namespace bcos::evmref
{
evmone::hash256 stateRootOf(const evmone::test::TestState& state)
{
    return evmone::state::mpt_hash(state);
}
}  // namespace bcos::evmref
