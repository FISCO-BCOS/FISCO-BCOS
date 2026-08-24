#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
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
evmc::bytes32 maxWord()  // 全 0xFF（槽内每个字节都是最大值）
{
    evmc::bytes32 w{};
    for (auto& b : w.bytes)
        b = 0xFF;
    return w;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpFeeParamsSuite)

BOOST_AUTO_TEST_CASE(UnpacksScalarsFromPackedSlots)
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

BOOST_AUTO_TEST_CASE(LoadFromStateEqualsManualUnpack)
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

BOOST_AUTO_TEST_CASE(UnpacksDaFootprintGasScalarFromSlot8)
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

// 全 max 槽解包：整槽(slot1/slot7)取 2^256-1，打包槽(slot3/slot8)各字段取其类型上限。
BOOST_AUTO_TEST_CASE(UnpacksMaxValueScalars)
{
    const auto slot1 = maxWord();  // l1_base_fee = 2^256-1
    const auto slot3 = maxWord();  // baseFeeScalar=[16,20), blobBaseFeeScalar=[20,24) 均 0xffffffff
    const auto slot7 = maxWord();  // blob_base_fee = 2^256-1
    const auto slot8 = maxWord();  // da=[18,20)=0xffff, opScalar=[20,24)=0xffffffff,
                                   // opConst=[24,32)=0xffffffffffffffff

    const auto p = unpackOpFeeParams(slot1, slot3, slot7, slot8);
    BOOST_CHECK_EQUAL(p.l1_base_fee, ~intx::uint256{0});
    BOOST_CHECK_EQUAL(p.base_fee_scalar, 0xffffffffu);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, 0xffffffffu);
    BOOST_CHECK_EQUAL(p.blob_base_fee, ~intx::uint256{0});
    BOOST_CHECK_EQUAL(p.da_footprint_gas_scalar, 0xffffu);
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, 0xffffffffu);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, ~0ull);
}

// slot8 打包三区 [18,20)/[20,24)/[24,32) 相邻，互不串扰；区外字节置 0xFF 证明读取不越界泄漏。
BOOST_AUTO_TEST_CASE(PackedByteBleedIsolation)
{
    evmc::bytes32 slot8{};
    for (size_t i = 0; i < 18; ++i)
        slot8.bytes[i] = 0xFF;  // 区外垃圾，不得影响任何字段
    slot8.bytes[18] = 0xab;     // da = 0xabcd
    slot8.bytes[19] = 0xcd;
    slot8.bytes[20] = 0x11;  // opScalar = 0x11223344
    slot8.bytes[21] = 0x22;
    slot8.bytes[22] = 0x33;
    slot8.bytes[23] = 0x44;
    const uint64_t opConst = 0x8877665544332211ull;
    for (size_t i = 0; i < 8; ++i)
        slot8.bytes[24 + i] =
            static_cast<uint8_t>(opConst >> (8 * (7 - i)));  // opConst = 0x8877665544332211

    const auto p = unpackOpFeeParams(fullWord(0), fullWord(0), fullWord(0), slot8);
    BOOST_CHECK_EQUAL(p.da_footprint_gas_scalar, 0xabcdu);
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, 0x11223344u);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, 0x8877665544332211ull);
    // 区外 0xFF 与未打包槽的零值不得泄漏
    BOOST_CHECK_EQUAL(p.l1_base_fee, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(p.blob_base_fee, intx::uint256{0});
}

// 4 槽全零 → 全参 0（缺失槽按零 word 处理）。
BOOST_AUTO_TEST_CASE(MissingAllSlotsZero)
{
    const evmc::bytes32 zero{};
    const auto p = unpackOpFeeParams(zero, zero, zero, zero);
    BOOST_CHECK_EQUAL(p.l1_base_fee, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(p.blob_base_fee, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.da_footprint_gas_scalar, 0u);
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, 0u);
}

// ---- Ecotone calldata round-trip test ----
// Constructs setL1BlockValuesEcotone calldata (spec §L1 Attributes) for known fee
// params, simulates the storage writes L1Block.sol would perform, then calls
// loadOpFeeParams and asserts all values match — closing the gap that no test
// exercises real L1Block storage layout with real calldata encoding.
namespace
{
// Build setL1BlockValuesEcotone calldata per spec (164 bytes total).
evmc::bytes buildEcotoneCalldata(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar,
    intx::uint256 basefee, intx::uint256 blobBaseFee)
{
    evmc::bytes calldata(164, 0x00);
    // selector 0x440a5e20
    calldata[0] = 0x44;
    calldata[1] = 0x0a;
    calldata[2] = 0x5e;
    calldata[3] = 0x20;
    // baseFeeScalar at [4:8)
    calldata[4] = static_cast<uint8_t>(baseFeeScalar >> 24);
    calldata[5] = static_cast<uint8_t>(baseFeeScalar >> 16);
    calldata[6] = static_cast<uint8_t>(baseFeeScalar >> 8);
    calldata[7] = static_cast<uint8_t>(baseFeeScalar);
    // blobBaseFeeScalar at [8:12)
    calldata[8] = static_cast<uint8_t>(blobBaseFeeScalar >> 24);
    calldata[9] = static_cast<uint8_t>(blobBaseFeeScalar >> 16);
    calldata[10] = static_cast<uint8_t>(blobBaseFeeScalar >> 8);
    calldata[11] = static_cast<uint8_t>(blobBaseFeeScalar);
    // basefee at [36:68)
    for (int i = 0; i < 32; ++i)
        calldata[36 + i] = static_cast<uint8_t>(basefee >> (8 * (31 - i)));
    // blobBaseFee at [68:100)
    for (int i = 0; i < 32; ++i)
        calldata[68 + i] = static_cast<uint8_t>(blobBaseFee >> (8 * (31 - i)));
    return calldata;
}
}  // namespace

BOOST_AUTO_TEST_CASE(EcotoneCalldataRoundTrip)
{
    using namespace evmone;
    using namespace evmone::test;

    constexpr uint32_t kBaseFeeScalar = 12345;
    constexpr uint32_t kBlobBaseFeeScalar = 67890;
    constexpr intx::uint256 kBasefee{42};
    constexpr intx::uint256 kBlobBaseFee{99};

    // Verify calldata layout matches spec §L1 Attributes.
    auto calldata =
        buildEcotoneCalldata(kBaseFeeScalar, kBlobBaseFeeScalar, kBasefee, kBlobBaseFee);
    BOOST_CHECK_EQUAL(calldata.size(), 164u);
    // selector
    BOOST_CHECK_EQUAL(calldata[0], 0x44);
    BOOST_CHECK_EQUAL(calldata[1], 0x0a);
    BOOST_CHECK_EQUAL(calldata[2], 0x5e);
    BOOST_CHECK_EQUAL(calldata[3], 0x20);

    // Simulate L1Block.sol storage writes for slots 1, 3, 7.
    TestState ts;
    evmc::bytes32 key1{};
    key1.bytes[31] = 1;
    ts[OP_L1_BLOCK].storage[key1] = intx::be::store<evmc::bytes32>(kBasefee);

    evmc::bytes32 key3{};
    key3.bytes[31] = 3;
    // op-geth layout: baseFeeScalar at [16:20), blobBaseFeeScalar at [20:24)
    evmc::bytes32 slot3{};
    slot3.bytes[16] = static_cast<uint8_t>(kBaseFeeScalar >> 24);
    slot3.bytes[17] = static_cast<uint8_t>(kBaseFeeScalar >> 16);
    slot3.bytes[18] = static_cast<uint8_t>(kBaseFeeScalar >> 8);
    slot3.bytes[19] = static_cast<uint8_t>(kBaseFeeScalar);
    slot3.bytes[20] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 24);
    slot3.bytes[21] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 16);
    slot3.bytes[22] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 8);
    slot3.bytes[23] = static_cast<uint8_t>(kBlobBaseFeeScalar);
    ts[OP_L1_BLOCK].storage[key3] = slot3;

    evmc::bytes32 key7{};
    key7.bytes[31] = 7;
    ts[OP_L1_BLOCK].storage[key7] = intx::be::store<evmc::bytes32>(kBlobBaseFee);

    // Load and assert.
    const auto p = loadOpFeeParams(ts);
    BOOST_CHECK_EQUAL(p.l1_base_fee, kBasefee);
    BOOST_CHECK_EQUAL(p.base_fee_scalar, kBaseFeeScalar);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, kBlobBaseFeeScalar);
    BOOST_CHECK_EQUAL(p.blob_base_fee, kBlobBaseFee);
}

// Isthmus variant: slots 1/3/7/8 with operator fee + DA scalar.
BOOST_AUTO_TEST_CASE(IsthmusCalldataRoundTrip)
{
    using namespace evmone;
    using namespace evmone::test;

    constexpr uint32_t kBaseFeeScalar = 100;
    constexpr uint32_t kBlobBaseFeeScalar = 200;
    constexpr intx::uint256 kBasefee{5000000000ULL};
    constexpr intx::uint256 kBlobBaseFee{3000000000ULL};
    constexpr uint32_t kOpScalar = 1000;
    constexpr uint64_t kOpConstant = 50000;
    constexpr uint16_t kDaScalar = 400;

    TestState ts;

    // slot 1: basefee
    evmc::bytes32 key1{};
    key1.bytes[31] = 1;
    ts[OP_L1_BLOCK].storage[key1] = intx::be::store<evmc::bytes32>(kBasefee);

    // slot 3: scalars
    evmc::bytes32 slot3{};
    slot3.bytes[16] = static_cast<uint8_t>(kBaseFeeScalar >> 24);
    slot3.bytes[17] = static_cast<uint8_t>(kBaseFeeScalar >> 16);
    slot3.bytes[18] = static_cast<uint8_t>(kBaseFeeScalar >> 8);
    slot3.bytes[19] = static_cast<uint8_t>(kBaseFeeScalar);
    slot3.bytes[20] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 24);
    slot3.bytes[21] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 16);
    slot3.bytes[22] = static_cast<uint8_t>(kBlobBaseFeeScalar >> 8);
    slot3.bytes[23] = static_cast<uint8_t>(kBlobBaseFeeScalar);
    evmc::bytes32 key3{};
    key3.bytes[31] = 3;
    ts[OP_L1_BLOCK].storage[key3] = slot3;

    // slot 7: blobBaseFee
    evmc::bytes32 key7{};
    key7.bytes[31] = 7;
    ts[OP_L1_BLOCK].storage[key7] = intx::be::store<evmc::bytes32>(kBlobBaseFee);

    // slot 8: operator fee + DA scalar (Isthmus/Jovian layout)
    evmc::bytes32 slot8{};
    // daScalar u16 at [18:20)
    slot8.bytes[18] = static_cast<uint8_t>(kDaScalar >> 8);
    slot8.bytes[19] = static_cast<uint8_t>(kDaScalar);
    // opScalar u32 at [20:24)
    slot8.bytes[20] = static_cast<uint8_t>(kOpScalar >> 24);
    slot8.bytes[21] = static_cast<uint8_t>(kOpScalar >> 16);
    slot8.bytes[22] = static_cast<uint8_t>(kOpScalar >> 8);
    slot8.bytes[23] = static_cast<uint8_t>(kOpScalar);
    // opConstant u64 at [24:32)
    for (int i = 0; i < 8; ++i)
        slot8.bytes[24 + i] = static_cast<uint8_t>(kOpConstant >> (8 * (7 - i)));
    evmc::bytes32 key8{};
    key8.bytes[31] = 8;
    ts[OP_L1_BLOCK].storage[key8] = slot8;

    const auto p = loadOpFeeParams(ts);
    BOOST_CHECK_EQUAL(p.l1_base_fee, kBasefee);
    BOOST_CHECK_EQUAL(p.base_fee_scalar, kBaseFeeScalar);
    BOOST_CHECK_EQUAL(p.blob_base_fee_scalar, kBlobBaseFeeScalar);
    BOOST_CHECK_EQUAL(p.blob_base_fee, kBlobBaseFee);
    BOOST_CHECK_EQUAL(p.operator_fee_scalar, kOpScalar);
    BOOST_CHECK_EQUAL(p.operator_fee_constant, kOpConstant);
    BOOST_CHECK_EQUAL(p.da_footprint_gas_scalar, kDaScalar);
}

BOOST_AUTO_TEST_SUITE_END()
