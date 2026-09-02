#include "TestPrinters.h"
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
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

BOOST_AUTO_TEST_SUITE(OpHostSuite)

BOOST_AUTO_TEST_CASE(GetTxContextUsesConfiguredChainId)
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
    BOOST_CHECK_EQUAL(intx::be::load<intx::uint256>(ctx.chain_id), intx::uint256{1234});
}

BOOST_AUTO_TEST_CASE(GetTxContextGasPriceZeroForSystemCall)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};
    BOOST_CHECK_EQUAL(
        intx::be::load<intx::uint256>(host.get_tx_context().tx_gas_price), intx::uint256{0});
}

// OP Ecotone+ L2s serve no blobs: BLOBBASEFEE must push MIN_BLOB_GASPRICE = 1, never 0 or the
// L1 blob market price. toBlockInfo never sets blob_base_fee (nullopt on both the block-execution
// and eth_call paths), so the host's value_or(1) is what every BLOBBASEFEE reads.
BOOST_AUTO_TEST_CASE(BlobBaseFeeDefaultsToOneWhenUnset)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    auto block = makeBlock();
    BOOST_CHECK(!block.blob_base_fee.has_value());  // toBlockInfo never sets it
    OpHost host{EVMC_PRAGUE, vm, st, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};
    const auto ctx = host.get_tx_context();
    BOOST_CHECK_EQUAL(
        intx::be::load<intx::uint256>(ctx.blob_base_fee), intx::uint256{1});  // MIN_BLOB_GASPRICE
}

BOOST_AUTO_TEST_CASE(OverrideTableInterceptsP256)
{
    BOOST_CHECK(
        isthmusPrecompileOverrides().contains(0x0000000000000000000000000000000000000100_address));
}

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kP256 = 0x0000000000000000000000000000000000000100_address;
constexpr int64_t kP256Gas = 3450;
}  // namespace

BOOST_AUTO_TEST_CASE(CallToP256EmptyAccountIsNotSilentSuccess)
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
    BOOST_CHECK_EQUAL(r.gas_left, msg.gas - kP256Gas);
}

BOOST_AUTO_TEST_CASE(CallToP256TransfersValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000_u256, .storage = {}, .code = {}};
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
    BOOST_CHECK_EQUAL(r.gas_left, msg.gas - kP256Gas);
    BOOST_CHECK_EQUAL(st.get(kSender).balance, intx::uint256{1000 - 42});
    BOOST_CHECK_EQUAL(st.get(kP256).balance, intx::uint256{42});
}

BOOST_AUTO_TEST_CASE(DelegateCallToP256IsNotSilentSuccess)
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
    BOOST_CHECK_EQUAL(r.gas_left, msg.gas - kP256Gas);
}

BOOST_AUTO_TEST_CASE(DelegatedFlagToP256FallsBackToEmptyCode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    // Host::prepare_message(depth==0) 会 get(sender)，必须先入账。
    ts[kSender] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = {}};
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
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.gas_left, msg.gas);
}

BOOST_AUTO_TEST_CASE(JovianBn256PairingInputOverLimitFails)
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
    BOOST_CHECK_EQUAL(r.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(r.gas_left, 0);
}

// D-11 边界（红队 F-9）：恰在 81984（=427×192）上限的输入必须执行（全零点对 → pairing 成功）。
// 挡住 OpHost 限长比较符 > 被改成 >= 的回归。gas = 45000 + 427*34000 = 14,563,000。
BOOST_AUTO_TEST_CASE(JovianBn256PairingInputAtLimitExecutes)
{
    constexpr auto kBn256 = 0x0000000000000000000000000000000000000008_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0,
        .balance = intx::uint256{0},
        .storage = {},
        .code = {}};  // prepare_message(depth==0) 会
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
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(r.gas_left, 0);
}

// D-12：0x100 在 Isthmus（rev=PRAGUE）必须预热（evmone is_precompile 门槛 OSAKA），
// 且不得产生幽灵空账户进入 state diff 的 deleted_accounts。
BOOST_AUTO_TEST_CASE(OverrideTablePrecompilesAreWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    BOOST_CHECK_EQUAL(host.access_account(k100), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(state.find(k100), nullptr);
}

// D-12 反作弊（红队 F-8）：覆写不得吞掉基类冷→暖迁移（「不委托基类」的手滑在此暴露）
BOOST_AUTO_TEST_CASE(OffTableAccessTransitionsColdToWarm)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    evmone::state::State state{ts};
    test::TestBlockHashes hashes;
    evmone::state::Transaction tx;
    const auto block = makeBlock();
    OpHost host{EVMC_PRAGUE, vm, state, block, hashes, tx, 1234, &isthmusPrecompileOverrides()};

    constexpr auto kPlain = 0x00000000000000000000000000000000000000ce_address;
    BOOST_CHECK_EQUAL(host.access_account(kPlain), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_account(kPlain), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_SUITE_END()
