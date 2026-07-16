#include <bcos-evm/opstack/OpPredeploys.h>
#include <test/utils/test_state.hpp>

namespace bcos::evmref::opstack
{
void seedOpPredeploys(evmone::test::TestState& state)
{
    for (const auto& addr : {OP_L1_BLOCK, OP_GAS_PRICE_ORACLE, OP_SEQUENCER_FEE_VAULT,
             OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[addr];  // insert default account
    }
    // The four fee vaults: minimal non-empty runtime code (1-byte STOP), so they are not deleted as
    // empty accounts under a zero-value diff.
    for (const auto& v :
        {OP_SEQUENCER_FEE_VAULT, OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT})
    {
        state[v].code = evmc::bytes{0x00};
    }
}
}  // namespace bcos::evmref::opstack
