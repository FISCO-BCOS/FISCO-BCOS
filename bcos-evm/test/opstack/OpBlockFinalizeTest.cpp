#include <bcos-evm/opstack/OpBlockFinalize.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmc::literals;

// D-10：OP 块收尾不执行 EIP-6110/7002/7251 requests（op-geth core/state_processor.go:140-156
// 对 OP Isthmus 显式禁用）；disable_prague_requests 在此被真实消费。
TEST(OpBlockFinalize, IsthmusFinalizeProducesNoSystemSideEffects)
{
    evmone::test::TestState ts;
    constexpr auto kCoinbase = 0x0000000000000000000000000000000000000011_address;
    const auto diff = finalizeOpBlock(ts, isthmusConfig(), kCoinbase);
    EXPECT_TRUE(diff.modified_accounts.empty());
    EXPECT_TRUE(diff.deleted_accounts.empty());
}

TEST(OpBlockFinalize, PragueRequestsEnabledIsRejected)
{
    evmone::test::TestState ts;
    OpForkConfig cfg = isthmusConfig();
    cfg.disable_prague_requests = false;
    EXPECT_THROW(finalizeOpBlock(ts, cfg, evmc::address{}), std::invalid_argument);
}
