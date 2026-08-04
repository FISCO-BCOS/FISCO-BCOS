#include <bcos-codec/rlp/OpReceiptMetaCodec.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <gtest/gtest.h>
#include <vector>

using namespace bcos::evm::opstack;
using intx::operator""_u256;

TEST(OpReceiptMeta, IsthmusHasFeesWithoutDa)
{
    OpFeeParams fee{};
    fee.l1_base_fee = 1000_u256;
    fee.base_fee_scalar = 7;
    fee.blob_base_fee = 2000_u256;
    fee.blob_base_fee_scalar = 9;
    fee.operator_fee_scalar = 11;
    fee.operator_fee_constant = 13;
    std::vector<uint8_t> env{0x02};
    const auto m =
        deriveOpReceiptMeta(isthmusConfig(), fee, flzCompressLen({env.data(), env.size()}),
            /*l1=*/100_u256, /*opUsed=*/50_u256, /*fill_operator_scalars=*/true);
    ASSERT_TRUE(m.l1_fee.has_value());
    EXPECT_EQ(*m.l1_fee, 100_u256);
    ASSERT_TRUE(m.l1_gas_price.has_value());
    EXPECT_EQ(*m.l1_gas_price, 1000_u256);
    EXPECT_EQ(*m.l1_blob_base_fee, 2000_u256);
    EXPECT_EQ(*m.l1_base_fee_scalar, 7u);
    EXPECT_EQ(*m.l1_blob_base_fee_scalar, 9u);
    ASSERT_TRUE(m.operator_fee.has_value());
    EXPECT_EQ(*m.operator_fee, 50_u256);
    EXPECT_TRUE(m.operator_fee_scalar.has_value());
    EXPECT_FALSE(m.da_footprint.has_value());
}

TEST(OpReceiptMeta, OperatorScalarsOmittedWhenBothZero)
{
    OpFeeParams fee{};  // operator scalar/constant both 0
    std::vector<uint8_t> env{0x02};
    const auto m = deriveOpReceiptMeta(isthmusConfig(), fee,
        flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, /*fill_operator_scalars=*/true);
    EXPECT_TRUE(m.operator_fee.has_value());          // 值始终填（FISCO 扩展）
    EXPECT_FALSE(m.operator_fee_scalar.has_value());  // 守卫：全 0 不填 scalar/constant
    EXPECT_FALSE(m.operator_fee_constant.has_value());
}

TEST(OpReceiptMeta, JovianFillsDaFootprint)
{
    OpFeeParams fee{};
    fee.da_footprint_gas_scalar = 2;
    std::vector<uint8_t> env(50, 0x11);
    const auto size = estimatedDaSize({env.data(), env.size()});
    const auto m = deriveOpReceiptMeta(
        jovianConfig(), fee, flzCompressLen({env.data(), env.size()}), 0_u256, 0_u256, false);
    ASSERT_TRUE(m.da_footprint_gas_scalar.has_value());
    EXPECT_EQ(*m.da_footprint_gas_scalar, 2u);
    ASSERT_TRUE(m.da_footprint.has_value());
    EXPECT_EQ(*m.da_footprint, size * 2u);
}

TEST(OpReceiptMeta, EncodeDecodeRoundTripPreservesAllFields)
{
    OpReceiptMeta m;
    m.l1_gas_price = 1000_u256;
    m.l1_blob_base_fee = 2000_u256;
    m.l1_base_fee_scalar = 7;
    m.l1_blob_base_fee_scalar = 9;
    m.l1_fee = 123456_u256;
    m.operator_fee_scalar = 11;
    m.operator_fee_constant = 13;
    m.da_footprint_gas_scalar = 2;
    m.da_footprint = 100;

    auto encoded = encodeOpReceiptMeta(m);
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{encoded.data(), encoded.size()}, decoded),
        nullptr);

    // uint256 fields travel as trimmed big-endian bytes (op-geth hexutil.Big semantics): 1000 ->
    // 0x03e8, 2000 -> 0x07d0, 123456 -> 0x01e240.
    ASSERT_TRUE(decoded.l1_gas_price);
    EXPECT_EQ(*decoded.l1_gas_price, (bcos::bytes{0x03, 0xe8}));
    ASSERT_TRUE(decoded.l1_blob_base_fee);
    EXPECT_EQ(*decoded.l1_blob_base_fee, (bcos::bytes{0x07, 0xd0}));
    ASSERT_TRUE(decoded.l1_fee);
    EXPECT_EQ(*decoded.l1_fee, (bcos::bytes{0x01, 0xe2, 0x40}));
    ASSERT_TRUE(decoded.l1_base_fee_scalar);
    EXPECT_EQ(*decoded.l1_base_fee_scalar, 7u);
    ASSERT_TRUE(decoded.l1_blob_base_fee_scalar);
    EXPECT_EQ(*decoded.l1_blob_base_fee_scalar, 9u);
    ASSERT_TRUE(decoded.operator_fee_scalar);
    EXPECT_EQ(*decoded.operator_fee_scalar, 11u);
    ASSERT_TRUE(decoded.operator_fee_constant);
    EXPECT_EQ(*decoded.operator_fee_constant, 13u);
    ASSERT_TRUE(decoded.da_footprint_gas_scalar);
    EXPECT_EQ(*decoded.da_footprint_gas_scalar, 2u);
    ASSERT_TRUE(decoded.da_footprint);
    EXPECT_EQ(*decoded.da_footprint, 100u);
    // Absent fields stay absent (not zero-valued).
    EXPECT_FALSE(decoded.deposit_nonce);
    EXPECT_FALSE(decoded.deposit_receipt_version);
}

TEST(OpReceiptMeta, EncodeDecodeDistinguishesZeroFromAbsent)
{
    // operator scalar is explicitly 0 — the wire format must carry it (op-geth emits the field
    // when present, even if the value is 0), not collapse it into "absent".
    OpReceiptMeta m;
    m.operator_fee_scalar = 0;
    m.operator_fee_constant = 0;
    m.l1_gas_price = 5_u256;

    auto encoded = encodeOpReceiptMeta(m);
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{encoded.data(), encoded.size()}, decoded),
        nullptr);
    ASSERT_TRUE(decoded.operator_fee_scalar);
    EXPECT_EQ(*decoded.operator_fee_scalar, 0u);
    ASSERT_TRUE(decoded.operator_fee_constant);
    EXPECT_EQ(*decoded.operator_fee_constant, 0u);
    // But fields never set on this meta are absent.
    EXPECT_FALSE(decoded.da_footprint);
    EXPECT_FALSE(decoded.deposit_nonce);
}

TEST(OpReceiptMeta, DepositMetaEncodesNonceAndVersion)
{
    auto encoded = encodeOpDepositMeta(42, 1);
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{encoded.data(), encoded.size()}, decoded),
        nullptr);
    ASSERT_TRUE(decoded.deposit_nonce);
    EXPECT_EQ(*decoded.deposit_nonce, 42u);
    ASSERT_TRUE(decoded.deposit_receipt_version);
    EXPECT_EQ(*decoded.deposit_receipt_version, 1u);
    // Deposit metas carry no L1/operator/DA fields.
    EXPECT_FALSE(decoded.l1_gas_price);
    EXPECT_FALSE(decoded.operator_fee_scalar);
}

TEST(OpReceiptMeta, EmptyMetaEncodesEmptyPresenceList)
{
    OpReceiptMeta m;
    auto encoded = encodeOpReceiptMeta(m);
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{encoded.data(), encoded.size()}, decoded),
        nullptr);
    EXPECT_FALSE(decoded.l1_gas_price);
    EXPECT_FALSE(decoded.l1_fee);
    EXPECT_FALSE(decoded.l1_blob_base_fee);
    EXPECT_FALSE(decoded.l1_base_fee_scalar);
    EXPECT_FALSE(decoded.l1_blob_base_fee_scalar);
    EXPECT_FALSE(decoded.operator_fee_scalar);
    EXPECT_FALSE(decoded.operator_fee_constant);
    EXPECT_FALSE(decoded.da_footprint_gas_scalar);
    EXPECT_FALSE(decoded.da_footprint);
    EXPECT_FALSE(decoded.deposit_nonce);
    EXPECT_FALSE(decoded.deposit_receipt_version);
    EXPECT_FALSE(decoded.l1_gas_used);
    EXPECT_FALSE(decoded.operator_fee);
}

TEST(OpReceiptMeta, DecodeRejectsMalformedInput)
{
    // Garbage that is not a list — must error, not crash.
    std::array<bcos::byte, 2> bad = {0x80, 0x00};
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    EXPECT_NE(
        bcos::codec::rlp::decodeOpReceiptMeta(bcos::bytesConstRef{bad.data(), bad.size()}, decoded),
        nullptr);

    // Presence mask with a bit beyond the field count (kOpReceiptMetaFieldCount = 13, so bit 13
    // = 0x2000 is out of range) — must error. rlp list [0x2000] == 0xc3 0x82 0x20 0x00.
    std::array<bcos::byte, 4> badMask = {0xc3, 0x82, 0x20, 0x00};
    bcos::codec::rlp::OpReceiptMetaFields decoded2;
    EXPECT_NE(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{badMask.data(), badMask.size()}, decoded2),
        nullptr);

    // Trailing bytes after the field list — must error. rlp([0]) would be just 0xc1 0x00; append
    // a stray byte so the list payload (1 byte: the presence mask) is followed by data the list
    // header never claimed.
    std::array<bcos::byte, 3> trailing = {0xc1, 0x00, 0xff};
    bcos::codec::rlp::OpReceiptMetaFields decoded3;
    EXPECT_NE(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{trailing.data(), trailing.size()}, decoded3),
        nullptr);
}

TEST(OpReceiptMeta, WireRoundTripNewFieldsAndOldBlobCompat)
{
    // New fields (indices 11/12) round-trip through the wire codec.
    bcos::codec::rlp::OpReceiptMetaFields fields;
    fields.l1_gas_used = 100;
    fields.operator_fee = bcos::bytes{0x03, 0xe8};  // 0x3e8 = 1000
    auto encoded = bcos::codec::rlp::encodeOpReceiptMeta(fields);
    bcos::codec::rlp::OpReceiptMetaFields decoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{encoded.data(), encoded.size()}, decoded),
        nullptr);
    ASSERT_TRUE(decoded.l1_gas_used);
    EXPECT_EQ(*decoded.l1_gas_used, 100u);
    ASSERT_TRUE(decoded.operator_fee);
    EXPECT_EQ(*decoded.operator_fee, (bcos::bytes{0x03, 0xe8}));

    // An old 11-field blob (mask has no bits 11/12) decodes cleanly with the new codec — the new
    // fields stay absent. Byte-for-byte, encoding only-old-fields is the old format.
    bcos::codec::rlp::OpReceiptMetaFields oldFields;
    oldFields.l1_gas_price = bcos::bytes{0x01};
    auto oldEncoded = bcos::codec::rlp::encodeOpReceiptMeta(oldFields);
    bcos::codec::rlp::OpReceiptMetaFields oldDecoded;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{oldEncoded.data(), oldEncoded.size()}, oldDecoded),
        nullptr);
    EXPECT_FALSE(oldDecoded.l1_gas_used);
    EXPECT_FALSE(oldDecoded.operator_fee);
}
