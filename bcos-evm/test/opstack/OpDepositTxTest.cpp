#include <bcos-evm/opstack/OpTransition.h>
#include "TestPrinters.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::evm::opstack;
using namespace evmc::literals;

BOOST_AUTO_TEST_SUITE(OpDepositTxSuite)

BOOST_AUTO_TEST_CASE(ContractCreationHasNullTo)
{
    DepositTx tx{
        .source_hash = 0x01_bytes32,
        .from = 0x000000000000000000000000000000000000dead_address,
        .to = std::nullopt,  // nullopt = 合约创建
        .mint = intx::uint256{5},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    BOOST_CHECK(!(tx.to.has_value()));
    BOOST_REQUIRE(tx.mint.has_value());
    BOOST_CHECK_EQUAL(*tx.mint, intx::uint256{5});
    BOOST_CHECK(!(tx.is_system_tx));
}

BOOST_AUTO_TEST_CASE(AbsentMintDiffersFromZero)
{
    DepositTx tx{};                       // 值初始化：mint 默认 nullopt
    BOOST_CHECK(!(tx.mint.has_value()));    // absent ≠ 0：执行层据此跳过加余额
}

BOOST_AUTO_TEST_CASE(MintAndValueAreIndependent)
{
    DepositTx tx{};
    tx.mint = intx::uint256{7};           // mint 有值 → 无条件加 from 余额
    tx.value = intx::uint256{3};          // value → call 中转账（两字段独立）
    BOOST_REQUIRE(tx.mint.has_value());
    BOOST_CHECK_NE(*tx.mint, tx.value);
}

BOOST_AUTO_TEST_SUITE_END()
