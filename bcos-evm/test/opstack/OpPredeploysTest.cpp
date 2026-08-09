#include "OpPredeploysSeed.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpPredeploys.h>
#include <boost/test/unit_test.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace evmc::literals;

BOOST_AUTO_TEST_SUITE(OpPredeploysSuite)

BOOST_AUTO_TEST_CASE(AddressesMatchOpStackNamespace)
{
    BOOST_CHECK_EQUAL(OP_L1_BLOCK, 0x4200000000000000000000000000000000000015_address);
    BOOST_CHECK_EQUAL(OP_GAS_PRICE_ORACLE, 0x420000000000000000000000000000000000000f_address);
    BOOST_CHECK_EQUAL(OP_SEQUENCER_FEE_VAULT, 0x4200000000000000000000000000000000000011_address);
    BOOST_CHECK_EQUAL(OP_BASE_FEE_VAULT, 0x4200000000000000000000000000000000000019_address);
    BOOST_CHECK_EQUAL(OP_L1_FEE_VAULT, 0x420000000000000000000000000000000000001a_address);
    BOOST_CHECK_EQUAL(OP_OPERATOR_FEE_VAULT, 0x420000000000000000000000000000000000001b_address);
    BOOST_CHECK_EQUAL(OP_DEPOSITOR, 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address);
}

BOOST_AUTO_TEST_CASE(SeedCreatesSixPredeployAccounts)
{
    evmone::test::TestState state;
    seedOpPredeploys(state);
    BOOST_CHECK(state.contains(OP_L1_BLOCK));
    BOOST_CHECK(state.contains(OP_GAS_PRICE_ORACLE));
    BOOST_CHECK(state.contains(OP_SEQUENCER_FEE_VAULT));
    BOOST_CHECK(state.contains(OP_BASE_FEE_VAULT));
    BOOST_CHECK(state.contains(OP_L1_FEE_VAULT));
    BOOST_CHECK(state.contains(OP_OPERATOR_FEE_VAULT));
    BOOST_CHECK(!(state.contains(OP_DEPOSITOR)));  // 合成 sender 不预填
}

BOOST_AUTO_TEST_CASE(VaultsHaveNonEmptyCode)
{
    evmone::test::TestState ts;
    seedOpPredeploys(ts);
    for (const auto& v :
        {OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT, OP_SEQUENCER_FEE_VAULT})
    {
        BOOST_CHECK_MESSAGE(!(ts[v].code.empty()), "vault should have stub code");
    }
    // L1Block / oracle 的 code 不由本函数写入（harness setter 自管）。
    BOOST_CHECK(ts[OP_L1_BLOCK].code.empty());
    BOOST_CHECK(ts[OP_GAS_PRICE_ORACLE].code.empty());
}

BOOST_AUTO_TEST_SUITE_END()
