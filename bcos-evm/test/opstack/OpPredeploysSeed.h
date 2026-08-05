#pragma once

// Test-genesis seeding for the OP predeploy/vault accounts (moved out of the production
// opstack module: every caller is a test; the production side only consumes the address
// constants in OpPredeploys.h).
// TODO(eth-utils-removal): seedOpPredeploys 形参 TestState→自研内存账本,
// 需连同全部测试调用方同步改。

#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/eth/utils/test_state.hpp>

namespace bcos::evm::opstack
{
/// Pre-seed the 6 predeploy/vault accounts as empty accounts with balance 0 (M5 block-level
/// harness genesis preparation). The four fee vaults carry a 1-byte stub code (0x00) to avoid
/// being deleted as empty accounts by EIP-161 under zero fees. The code for OP_L1_BLOCK /
/// OP_GAS_PRICE_ORACLE is still managed by the harness setter itself.
inline void seedOpPredeploys(evmone::test::TestState& state)
{
    for (const auto& addr : {OP_L1_BLOCK, OP_GAS_PRICE_ORACLE, OP_SEQUENCER_FEE_VAULT,
             OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[addr];  // insert a default account
    }
    // The four fee vaults: minimal non-empty runtime code (1-byte STOP), so they are not deleted as
    // empty accounts under a zero-value diff.
    for (const auto& v :
        {OP_SEQUENCER_FEE_VAULT, OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[v].code = evmc::bytes{0x00};
    }
}
}  // namespace bcos::evm::opstack
