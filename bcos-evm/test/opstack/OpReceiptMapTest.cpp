// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
// OpReceiptMap — maps an OP-executed evmone receipt into a bcos::protocol::TransactionReceipt.
// These tests pin the two fields mapOpReceipt carries beyond the evmone receipt itself: the
// blockNumber (which the execution layer supplies) and the serialized opReceiptMeta (which the
// RPC layer decodes into op-geth's OP extension fields).

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/engine/OpReceiptMap.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <gtest/gtest.h>

using namespace bcos::evm;
using namespace bcos::evm::engine;
using namespace bcos::evm::opstack;
using intx::operator""_u256;

namespace
{
evmone::state::TransactionReceipt minimalReceipt()
{
    evmone::state::TransactionReceipt r{};
    r.type = evmone::state::Transaction::Type::legacy;
    r.status = EVMC_SUCCESS;
    r.gas_used = 21000;
    r.cumulative_gas_used = 21000;
    return r;
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
}
}  // namespace

TEST(OpReceiptMap, CarriesBlockNumber)
{
    auto factory = makeReceiptFactory();
    auto mapped = mapOpReceipt(minimalReceipt(), factory, /*blockNumber=*/1234, {});
    ASSERT_NE(mapped, nullptr);
    EXPECT_EQ(mapped->blockNumber(), 1234);
    // gasUsed/status still mapped from the evmone receipt.
    EXPECT_EQ(mapped->gasUsed(), 21000);
    EXPECT_EQ(mapped->status(), 0);  // EVMC_SUCCESS -> FISCO success (0)
}

TEST(OpReceiptMap, FailureStatusMapsToOne)
{
    auto factory = makeReceiptFactory();
    auto r = minimalReceipt();
    r.status = EVMC_REVERT;
    auto mapped = mapOpReceipt(r, factory, 1, {});
    EXPECT_EQ(mapped->status(), 1);  // non-success -> generic failure
}

TEST(OpReceiptMap, SerializedMetaSurvivesMap)
{
    auto factory = makeReceiptFactory();
    OpReceiptMeta meta;
    meta.l1_gas_price = 1000_u256;
    meta.l1_fee = 999_u256;
    meta.da_footprint = 42;
    auto metaBytes = encodeOpReceiptMeta(meta);

    auto mapped = mapOpReceipt(minimalReceipt(), factory, 7, metaBytes);
    ASSERT_NE(mapped, nullptr);
    // The RPC layer sees the same serialized bytes it will decode for l1GasPrice/l1Fee/...
    EXPECT_EQ(mapped->opReceiptMeta(), std::string(metaBytes.begin(), metaBytes.end()));
}

TEST(OpReceiptMap, EmptyMetaStaysEmpty)
{
    auto factory = makeReceiptFactory();
    auto mapped = mapOpReceipt(minimalReceipt(), factory, 3, {});
    EXPECT_TRUE(mapped->opReceiptMeta().empty());
}

TEST(OpReceiptMap, DepositMetaRoundTripsThroughReceipt)
{
    auto factory = makeReceiptFactory();
    auto depositMeta = encodeOpDepositMeta(77, 1);
    auto mapped = mapOpReceipt(minimalReceipt(), factory, 5, depositMeta);

    auto view = mapped->opReceiptMeta();
    bcos::codec::rlp::OpReceiptMetaFields fields;
    ASSERT_EQ(bcos::codec::rlp::decodeOpReceiptMeta(
                  bcos::bytesConstRef{(bcos::byte const*)view.data(), view.size()}, fields),
        nullptr);
    ASSERT_TRUE(fields.deposit_nonce);
    EXPECT_EQ(*fields.deposit_nonce, 77u);
    ASSERT_TRUE(fields.deposit_receipt_version);
    EXPECT_EQ(*fields.deposit_receipt_version, 1u);
}
