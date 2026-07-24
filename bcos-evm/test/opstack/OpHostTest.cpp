#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
state::BlockInfo makeBlock()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = 0x4200000000000000000000000000000000000011_address;
    return b;
}
}  // namespace

TEST(OpHost, GetTxContextUsesConfiguredChainId)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = 0x00000000000000000000000000000000000000aa_address;
    const auto block = makeBlock();
    OpHost host{
        EVMC_PRAGUE, vm, st, block, hashes, tx, /*chainId=*/1234, &isthmusPrecompileOverrides()};
    const auto ctx = host.get_tx_context();
    EXPECT_EQ(intx::be::load<intx::uint256>(ctx.chain_id), intx::uint256{1234});
}

TEST(OpHost, GetTxContextGasPriceZeroForSystemCall)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};
    EXPECT_EQ(intx::be::load<intx::uint256>(host.get_tx_context().tx_gas_price), intx::uint256{0});
}

TEST(OpHost, OverrideTableInterceptsP256)
{
    EXPECT_TRUE(
        isthmusPrecompileOverrides().contains(0x0000000000000000000000000000000000000100_address));
}

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kP256 = 0x0000000000000000000000000000000000000100_address;
constexpr int64_t kP256Gas = 3450;
}  // namespace

TEST(OpHost, CallToP256EmptyAccountIsNotSilentSuccess)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kP256;
    msg.code_address = msg.recipient;
    msg.sender = tx.sender;
    msg.gas = 100000;
    const auto r = host.call(msg);
    EXPECT_EQ(r.gas_left, msg.gas - kP256Gas);
}

TEST(OpHost, CallToP256TransfersValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000_u256};
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kP256;
    msg.code_address = kP256;
    msg.sender = kSender;
    msg.gas = 100000;
    msg.value = intx::be::store<evmc::uint256be>(intx::uint256{42});

    const auto r = host.call(msg);
    EXPECT_EQ(r.gas_left, msg.gas - kP256Gas);
    EXPECT_EQ(st.get(kSender).balance, intx::uint256{1000 - 42});
    EXPECT_EQ(st.get(kP256).balance, intx::uint256{42});
}

TEST(OpHost, DelegateCallToP256IsNotSilentSuccess)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    evmc_message msg{};
    msg.kind = EVMC_DELEGATECALL;
    msg.recipient = kSender;
    msg.code_address = kP256;
    msg.sender = kSender;
    msg.gas = 100000;
    const auto r = host.call(msg);
    EXPECT_EQ(r.gas_left, msg.gas - kP256Gas);
}

TEST(OpHost, DelegatedFlagToP256FallsBackToEmptyCode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    // Host::prepare_message(depth==0) 会 get(sender)，必须先入账。
    ts[kSender] = {.nonce = 0, .balance = 0_u256};
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = EVMC_DELEGATED;
    msg.recipient = kP256;
    msg.code_address = kP256;
    msg.sender = kSender;
    msg.gas = 100000;
    const auto r = host.call(msg);
    // 母本：DELEGATED 命中 precompile 地址 → 空 code 成功，保留全 gas。
    EXPECT_EQ(r.status_code, EVMC_SUCCESS);
    EXPECT_EQ(r.gas_left, msg.gas);
}

TEST(OpHost, JovianBn256PairingInputOverLimitFails)
{
    constexpr auto kBn256Pairing = 0x0000000000000000000000000000000000000008_address;
    constexpr size_t kJovianMax = 81984;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &jovianPrecompileOverrides()};

    std::vector<uint8_t> input(kJovianMax + 1, 0x00);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kBn256Pairing;
    msg.code_address = kBn256Pairing;
    msg.sender = kSender;
    msg.gas = 100000;
    msg.input_data = input.data();
    msg.input_size = input.size();

    const auto r = host.call(msg);
    EXPECT_EQ(r.status_code, EVMC_FAILURE);
    EXPECT_EQ(r.gas_left, 0);
}

// D-11 边界（红队 F-9）：恰在 81984（=427×192）上限的输入必须执行（全零点对 → pairing 成功）。
// 挡住 OpHost 限长比较符 > 被改成 >= 的回归。gas = 45000 + 427*34000 = 14,563,000。
TEST(OpHost, JovianBn256PairingInputAtLimitExecutes)
{
    constexpr auto kBn256 = 0x0000000000000000000000000000000000000008_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = intx::uint256{0}};  // prepare_message(depth==0) 会
                                                              // get(sender)，必须先入账
                                                              // （同 OpHostTest.cpp:139 先例）
    evmone::state::State st{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    tx.sender = kSender;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &jovianPrecompileOverrides()};

    std::vector<uint8_t> input(81984, 0x00);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kBn256;
    msg.code_address = kBn256;
    msg.sender = kSender;
    msg.gas = 15'000'000;
    msg.input_data = input.data();
    msg.input_size = input.size();
    const auto r = host.call(msg);
    EXPECT_EQ(r.status_code, EVMC_SUCCESS);
    EXPECT_GT(r.gas_left, 0);
}

// D-12：0x100 在 Isthmus（rev=PRAGUE）必须预热（evmone is_precompile 门槛 OSAKA），
// 且不得产生幽灵空账户进入 state diff 的 deleted_accounts。
TEST(OpHost, OverrideTablePrecompilesAreWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    EXPECT_EQ(host.access_account(k100), EVMC_ACCESS_WARM);
    EXPECT_EQ(state.find(k100), nullptr);
}

// D-12 反作弊（红队 F-8）：覆写不得吞掉基类冷→暖迁移（「不委托基类」的手滑在此暴露）
TEST(OpHost, OffTableAccessTransitionsColdToWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto kPlain = 0x00000000000000000000000000000000000000ce_address;
    EXPECT_EQ(host.access_account(kPlain), EVMC_ACCESS_COLD);
    EXPECT_EQ(host.access_account(kPlain), EVMC_ACCESS_WARM);
}
