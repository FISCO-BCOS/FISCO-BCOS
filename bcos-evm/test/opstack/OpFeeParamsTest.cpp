#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

namespace
{
// 造一个 32 字节大端 word：在 [byteOff, byteOff+len) 放入 value 的低 len 字节。
evmc::bytes32 wordWith(size_t byteOff, uint64_t value, size_t len)
{
    evmc::bytes32 w{};
    for (size_t i = 0; i < len; ++i)
    {
        w.bytes[byteOff + len - 1 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    return w;
}
evmc::bytes32 fullWord(uint64_t low)  // 整槽放一个小数值（低 8 字节）
{
    return wordWith(24, low, 8);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpFeeParamsSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(UnpacksScalarsFromPackedSlots, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto slot1 = fullWord(1000);  // l1_base_fee = 1000
    const auto slot3 = [] {             // baseFeeScalar=7, blobBaseFeeScalar=9
        evmc::bytes32 w = wordWith(16, 7, 4);
        auto blob = wordWith(20, 9, 4);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | blob.bytes[i]);
        return w;
    }();
    const auto slot7 = fullWord(2000);  // blob_base_fee = 2000
    const auto slot8 = [] {             // opScalar=11, opConstant=13
        evmc::bytes32 w = wordWith(20, 11, 4);
        auto c = wordWith(24, 13, 8);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | c.bytes[i]);
        return w;
    }();

    const auto p = unpackOpFeeParams(slot1, slot3, slot7, slot8);
    BOOST_CHECK_EQUAL(p.l1_base_fee, intx::uint256{1000});
    BOOST_CHECK_EQUAL(p.base_fee_scalar, 7u);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, 9u);
    BOOST_CHECK_EQUAL(p.blob_base_fee, intx::uint256{2000});
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, 11u);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, 13u);
}

// clang-format off
BOOST_AUTO_TEST_CASE(LoadFromStateEqualsManualUnpack, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    using namespace evmone;
    test::TestState ts;
    auto key = [](uint8_t s) {
        evmc::bytes32 k{};
        k.bytes[31] = s;
        return k;
    };
    auto low8 = [](uint64_t v) {
        evmc::bytes32 w{};
        for (int i = 0; i < 8; ++i)
            w.bytes[31 - i] = static_cast<uint8_t>(v >> (8 * i));
        return w;
    };
    ts[OP_L1_BLOCK].storage[key(1)] = low8(1000000000);
    ts[OP_L1_BLOCK].storage[key(7)] = low8(10000000);

    const auto loaded = loadOpFeeParams(ts);
    const auto manual =
        unpackOpFeeParams(ts.get_storage(OP_L1_BLOCK, key(1)), ts.get_storage(OP_L1_BLOCK, key(3)),
            ts.get_storage(OP_L1_BLOCK, key(7)), ts.get_storage(OP_L1_BLOCK, key(8)));
    BOOST_CHECK_EQUAL(loaded.l1_base_fee, manual.l1_base_fee);
    BOOST_CHECK_EQUAL(loaded.blob_base_fee, manual.blob_base_fee);
    BOOST_CHECK_EQUAL(loaded.l1_base_fee, 1000000000_u256);
}

// clang-format off
BOOST_AUTO_TEST_CASE(UnpacksDaFootprintGasScalarFromSlot8, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    const auto slot1 = fullWord(1000);
    const auto slot3 = [] {
        evmc::bytes32 w = wordWith(16, 7, 4);
        auto blob = wordWith(20, 9, 4);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | blob.bytes[i]);
        return w;
    }();
    const auto slot7 = fullWord(2000);
    const auto slot8 = [] {
        // da=0x1234 at [18,20), opScalar=11 at [20,24), opConstant=13 at [24,32)
        evmc::bytes32 w = wordWith(18, 0x1234, 2);
        auto s = wordWith(20, 11, 4);
        auto c = wordWith(24, 13, 8);
        for (size_t i = 0; i < 32; ++i)
            w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | s.bytes[i] | c.bytes[i]);
        return w;
    }();

    const auto p = unpackOpFeeParams(slot1, slot3, slot7, slot8);
    BOOST_CHECK_EQUAL(p.da_footprint_gas_scalar, 0x1234u);
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, 11u);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, 13u);
}

// clang-format off
BOOST_AUTO_TEST_CASE(LoadReadsBedrockOverheadAndScalarSlots, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    using namespace evmone;
    test::TestState ts;
    auto key = [](uint8_t s) {
        evmc::bytes32 k{};
        k.bytes[31] = s;
        return k;
    };
    ts[OP_L1_BLOCK].storage[key(5)] = fullWord(2100);
    ts[OP_L1_BLOCK].storage[key(6)] = fullWord(1'000'000);

    const auto fee = loadOpFeeParams(ts);
    BOOST_CHECK_EQUAL(fee.overhead, intx::uint256{2100});
    BOOST_CHECK_EQUAL(fee.bedrock_scalar, intx::uint256{1'000'000});
}

// clang-format off
BOOST_AUTO_TEST_CASE(MissingSlotsStayZero, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    using namespace evmone;
    test::TestState ts;
    const auto fee = loadOpFeeParams(ts);
    BOOST_CHECK_EQUAL(fee.overhead, intx::uint256{0});
    BOOST_CHECK_EQUAL(fee.bedrock_scalar, intx::uint256{0});
    BOOST_CHECK_EQUAL(fee.l1_base_fee, intx::uint256{0});
}

// clang-format off
BOOST_AUTO_TEST_CASE(EcotoneL1SlotsLiveDetectsPackedScalars, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    using namespace evmone;
    test::TestState ts;
    BOOST_CHECK(!ecotoneL1SlotsLive(loadOpFeeParams(ts)));
    // slot3 bytes[16:24) and slot7 any non-zero => live
    evmc::bytes32 slot3{};
    slot3.bytes[19] = 1;  // base_fee_scalar LSB
    auto key = [](uint8_t s) {
        evmc::bytes32 k{};
        k.bytes[31] = s;
        return k;
    };
    ts[OP_L1_BLOCK].storage[key(3)] = slot3;
    BOOST_CHECK(ecotoneL1SlotsLive(loadOpFeeParams(ts)));
}

BOOST_AUTO_TEST_SUITE_END()
