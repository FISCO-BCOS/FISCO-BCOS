// OpCommon.h 工具函数单元测试——bounds-checked 窄化、hex 格式化、fixed-size 转换。
// 这些是 block-execution 的转换面（narrowGasUsed 防回执 gas 污染 gas 池、hexCumulative 是
// 回执根叶子编码输入、toEvmc* 是 bcos↔evmc 的 memcpy 契约），此前零测试。
#include <opstack-executor/OpCommon.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <limits>

using namespace bcos::evm::opstack;
using namespace bcos::evm::engine;
using namespace bcos::evm::engine::detail;
using intx::operator""_u256;

BOOST_AUTO_TEST_SUITE(OpCommonTests)

// narrowGasUsed：界内直通、超 int64 throw。
BOOST_AUTO_TEST_CASE(narrowGasUsed_bounds)
{
    BOOST_CHECK_EQUAL(narrowGasUsed(bcos::u256{0}), 0);
    BOOST_CHECK_EQUAL(narrowGasUsed(bcos::u256{std::numeric_limits<int64_t>::max()}),
        std::numeric_limits<int64_t>::max());
    // int64 上界 + 1 = 2^63：字符串构造避免 boost cpp_int 运算符的 nodiscard 告警。
    // narrowGasUsed 带 [[nodiscard]]——BOOST_CHECK_THROW 宏会丢弃返回值，用 if 消费。
    const bcos::u256 overMax = bcos::u256{"9223372036854775808"};
    bool threw = false;
    try
    {
        if (narrowGasUsed(overMax) != 0)
        {
        }
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    BOOST_CHECK(threw);
}

// hexCumulative："0x" + 小写 hex，最小表示（op-geth hexutil.Uint64 语义）。
BOOST_AUTO_TEST_CASE(hexCumulative_format)
{
    BOOST_CHECK_EQUAL(hexCumulative(0), "0x0");
    BOOST_CHECK_EQUAL(hexCumulative(1), "0x1");
    BOOST_CHECK_EQUAL(hexCumulative(21000), "0x5208");
    BOOST_CHECK_EQUAL(hexCumulative(0xffffffffffffffffULL), "0xffffffffffffffff");
}

// narrowU256ToU64：界内直通、超 uint64 throw OpConsensusError。
BOOST_AUTO_TEST_CASE(narrowU256ToU64_bounds)
{
    BOOST_CHECK_EQUAL(narrowU256ToU64(bcos::u256{42}, "field"), 42u);
    BOOST_CHECK_EQUAL(narrowU256ToU64(bcos::u256{std::numeric_limits<uint64_t>::max()}, "field"),
        std::numeric_limits<uint64_t>::max());
    // uint64 上界 + 1 = 2^64：字符串构造。narrowU256ToU64 带 [[nodiscard]]——if 消费。
    const bcos::u256 overU64 = bcos::u256{"18446744073709551616"};
    bool threw = false;
    try
    {
        if (narrowU256ToU64(overU64, "field") != 0)
        {
        }
    }
    catch (const OpConsensusError&)
    {
        threw = true;
    }
    BOOST_CHECK(threw);
}

// narrowU256ToI64：int64 界内直通（含 UINT64 中段 (INT64_MAX, UINT64_MAX]——该区间能被
// narrowU256ToU64 放行却在赋给 int64_t 字段时回绕成负数，正是本 helper 要挡的）。
BOOST_AUTO_TEST_CASE(narrowU256ToI64_bounds)
{
    BOOST_CHECK_EQUAL(narrowU256ToI64(bcos::u256{42}, "field"), 42);
    BOOST_CHECK_EQUAL(narrowU256ToI64(bcos::u256{std::numeric_limits<int64_t>::max()}, "field"),
        std::numeric_limits<int64_t>::max());
    // int64 上界 + 1 = 2^63：字符串构造。narrowU256ToI64 带 [[nodiscard]]——if 消费。
    const bcos::u256 overI64 = bcos::u256{"9223372036854775808"};
    bool threw = false;
    try
    {
        if (narrowU256ToI64(overI64, "field") != 0)
        {
        }
    }
    catch (const OpConsensusError&)
    {
        threw = true;
    }
    BOOST_CHECK(threw);
}
BOOST_AUTO_TEST_CASE(fixed_size_conversions_preserve_bytes)
{
    const bcos::Address addr = bcos::Address("00112233445566778899aabbccddeeff00112233");
    const auto evmcAddr = toEvmcAddress(addr);
    BOOST_CHECK_EQUAL(std::memcmp(evmcAddr.bytes, addr.data(), sizeof(evmcAddr.bytes)), 0);

    const bcos::h256 h =
        bcos::h256("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    const auto evmcB32 = toEvmcBytes32(h);
    BOOST_CHECK_EQUAL(std::memcmp(evmcB32.bytes, h.data(), sizeof(evmcB32.bytes)), 0);
}

// classifyTxType：deposit(0x7e)→0x7e, legacy(>=0xc0)→0, typed(0x01/0x02/0x04)→passthrough。
BOOST_AUTO_TEST_CASE(classifyTxType_branches)
{
    // Deposit
    BOOST_CHECK_EQUAL(classifyTxType(0x7e), 0x7e);
    // Legacy: RLP list prefix range
    BOOST_CHECK_EQUAL(classifyTxType(0xc0), 0);
    BOOST_CHECK_EQUAL(classifyTxType(0xf7), 0);
    BOOST_CHECK_EQUAL(classifyTxType(0xff), 0);
    // Typed transactions
    BOOST_CHECK_EQUAL(classifyTxType(0x01), 0x01);  // EIP-2930
    BOOST_CHECK_EQUAL(classifyTxType(0x02), 0x02);  // EIP-1559
    BOOST_CHECK_EQUAL(classifyTxType(0x04), 0x04);  // EIP-7702
    // Unknown type byte — passthrough (caller decides); 0x7e 两侧边界都钉住
    BOOST_CHECK_EQUAL(classifyTxType(0x05), 0x05);
    BOOST_CHECK_EQUAL(classifyTxType(0x7f), 0x7f);  // just above deposit
    BOOST_CHECK_EQUAL(classifyTxType(0x7d), 0x7d);  // just below deposit
}

BOOST_AUTO_TEST_SUITE_END()
